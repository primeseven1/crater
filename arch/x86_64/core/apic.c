#include <crater/common.h>
#include <crater/compiler.h>
#include <crater/spinlock.h>
#include <crater/interrupt.h>
#include <crater/irq.h>
#include <crater/percpu.h>
#include <crater/vmm.h>
#include <crater/printk.h>
#include <crater/timekeeper.h>
#include <crater/timer.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#include <arch/io.h>
#include <arch/processor.h>
#include <x86_64/idt.h>
#include <x86_64/interrupt.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/cpuid.h>

#include "internal.h"

enum ioapic_reg { 
	IOAPIC_REG_ID = 0,
	IOAPIC_REG_VERSION = 1,
	IOAPIC_REG_PRIORITY = 2,
	IOAPIC_REG_REDTBL_BASE = 0x10
};

enum lapic_reg {
	LAPIC_REG_ID = 0x20,
	LAPIC_REG_EOI = 0xB0,
	LAPIC_REG_SPURIOUS = 0xF0,
	LAPIC_REG_ISR_BASE = 0x100,
	LAPIC_REG_TMR_BASE = 0x180,
	LAPIC_REG_IRR_BASE = 0x200,
	LAPIC_REG_LVT_TIMER = 0x320,
	LAPIC_REG_LVT_THERMAL = 0x330,
	LAPIC_REG_LVT_PERFORMANCE = 0x340,
	LAPIC_REG_LVT_LINT0 = 0x350,
	LAPIC_REG_LVT_LINT1 = 0x360,
	LAPIC_REG_LVT_ERROR = 0x370,
	LAPIC_REG_X2_ICR = 0x300,
	LAPIC_REG_ICR_LOW = 0x300,
	LAPIC_REG_ICR_HIGH = 0x310,
	LAPIC_REG_TIMER_INITIAL = 0x380,
	LAPIC_REG_TIMER_CURRENT = 0x390,
	LAPIC_REG_TIMER_DIVIDE = 0x3E0
};

#define APIC_BASE_FLAG_BSP (1 << 8)
#define APIC_BASE_FLAG_X2_ENABLE (1 << 10)
#define APIC_BASE_FLAG_ENABLE (1 << 11)

#define LAPIC_LVT_MASKED (1 << 16)
#define LAPIC_LVT_DELIVERY_NMI (0b100 << 8)
#define LAPIC_LVT_TIMER_ONESHOT 0
#define LAPIC_LVT_TIMER_PERIODIC (1 << 17)

#define APIC_TRIGGER_EDGE 0
#define APIC_TRIGGER_LEVEL 1
#define APIC_POLARITY_ACTIVE_HIGH 0
#define APIC_POLARITY_ACTIVE_LOW 1

/* IPI delivery status */
#define LAPIC_ICR_DELIV_STATUS (1u << 12)

/* IPI delivery mode and destination modes */
#define APIC_DM_FIXED 0
#define APIC_DM_NMI 4
#define APIC_DEST_PHYSICAL 0
#define APIC_DEST_LOGICAL 1

/* IPI shorthands */
#define APIC_SHRT_CPU_TARGET 0
#define APIC_SHRT_CPU_SELF 1
#define APIC_SHRT_CPU_ALL 2
#define APIC_SHRT_CPU_OTHERS 3

static u32 __iomem* lapic_address = NULL;
struct ioapic_desc {
	u32 __iomem* address;
	u32 id, version, gsi_base, gsi_top;
};
static struct ioapic_desc* ioapics;
static unsigned long ioapic_count;
static SPINLOCK_DEFINE(ioapic_lock);
static bool __atomic use_x2apic = atomic_init(false);

static struct acpi_madt* madt = NULL;

static unsigned long get_entry_count(u8 type) {
	struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*)(madt + 1);

	unsigned long count = 0;
	while (entry < (struct acpi_entry_hdr*)((u8*)madt + madt->hdr.length)) {
		if (entry->type == type)
			count++;
		entry = (struct acpi_entry_hdr*)((u8*)entry + entry->length);
	}

	return count;
}

static void* get_entry(u8 type, unsigned long n) {
	struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*)(madt + 1);
	while (entry < (struct acpi_entry_hdr*)((u8*)madt + madt->hdr.length)) {
		if (entry->type == type) {
			if (n-- == 0)
				return entry;
		}

		entry = (struct acpi_entry_hdr*)((u8*)entry + entry->length);
	}

	return NULL;
}

static struct ioapic_desc* get_ioapic_gsi(u32 gsi) {
	for (unsigned long i = 0; i < ioapic_count; i++) {
		if (ioapics[i].gsi_base <= gsi && ioapics[i].gsi_top > gsi)
			return &ioapics[i];
	}

	return NULL;
}

static inline u32 ioapic_read(u32 __iomem* ioapic, enum ioapic_reg reg) {
	u32 __iomem* io = ioapic;
	writel(io, reg);
	return readl(io + 4);
}

static inline void ioapic_write(u32 __iomem* ioapic, enum ioapic_reg reg, u32 x) {
	u32 __iomem* io = ioapic;
	writel(io, reg);
	writel(io + 4, x);
}

static inline u64 lapic_read(enum lapic_reg reg) {
	if (!atomic_load(&use_x2apic)) {
		u32 __iomem* io = (u32 __iomem*)((u8 __iomem*)lapic_address + reg);
		return readl(io);
	}
	return arch_x86_64_rdmsr(ARCH_X86_64_MSR_X2APIC_BASE + (reg >> 4));
}

static inline void lapic_write(enum lapic_reg reg, u64 x) {
	if (!atomic_load(&use_x2apic)) {
		u32 __iomem* io = (u32 __iomem*)((u8 __iomem*)lapic_address + reg);
		writel(io, x);
	} else {
		arch_x86_64_wrmsr(ARCH_X86_64_MSR_X2APIC_BASE + (reg >> 4), x);
	}
}

struct ioapic_redtbl_entry {
	u8 vector;
	u8 delivery;
	u8 destmode;
	u8 polarity;
	u8 trigger;
	u8 masked;
	u8 dest;
};

static void ioapic_redtbl_write(u32 __iomem* ioapic, u8 entry, const struct ioapic_redtbl_entry* re) {
	u32 x = re->vector;

	x |= (re->delivery & 0b111) << 8;
	x |= (re->destmode & 1) << 11;
	x |= (re->polarity & 1) << 13;
	x |= (re->trigger & 1) << 15;
	x |= (re->masked & 1) << 16;

	ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + entry * 2, x);
	ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + 1 + (entry * 2), (u32)re->dest << 24);
}

static void ioapic_redtbl_read(u32 __iomem* ioapic, u8 entry, struct ioapic_redtbl_entry* out) {
	u32 low = ioapic_read(ioapic, IOAPIC_REG_REDTBL_BASE + entry * 2);
	u32 high = ioapic_read(ioapic, IOAPIC_REG_REDTBL_BASE + 1 + entry * 2);
	out->vector = low & 0xFF;
	out->delivery = (low >> 8) & 0b111;
	out->destmode = (low >> 11) & 1;
	out->polarity = (low >> 13) & 1;
	out->trigger = (low >> 15) & 1;
	out->masked = (low >> 16) & 1;
	out->dest = (high >> 24) & 0xFF;
}

static void apic_eoi(const struct isr* isr) {
	(void)isr;
	lapic_write(LAPIC_REG_EOI, 0);
}

/* Sets an IRQ entry in the redirection table, the IOAPIC is expected to be locked */
static int ioapic_set_irq(u32 irq, u8 vector, u8 processor, bool masked, u8 polarity, u8 trigger) {
	const u8 type = ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE;
	unsigned long count = get_entry_count(type);
	for (unsigned long i = 0; i < count; i++) {
		struct acpi_madt_interrupt_source_override* override = get_entry(type, i);
		if (override->source != irq)
			continue;

		polarity = ((override->flags & 0b11) == 0b11) ? APIC_POLARITY_ACTIVE_LOW : APIC_POLARITY_ACTIVE_HIGH;
		trigger = (((override->flags >> 2) & 0b11) == 0b11) ? APIC_TRIGGER_LEVEL : APIC_TRIGGER_EDGE;

		irq = override->gsi;
		break;
	}

	struct ioapic_desc* desc = get_ioapic_gsi(irq);
	if (unlikely(!desc))
		return -ENOENT;
	
	irq = irq - desc->gsi_base;
	struct ioapic_redtbl_entry re = {
		.vector = vector,
		.masked = masked,
		.polarity = polarity,
		.trigger = trigger,
		.dest = processor,
		.destmode = 0,
		.delivery = 0
	};
	ioapic_redtbl_write(desc->address, irq, &re);

	return 0;
}

static int ioapic_get_irq(u32 irq, struct ioapic_redtbl_entry* re) {
	const u8 type = ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE;
	unsigned long count = get_entry_count(type);
	for (unsigned long i = 0; i < count; i++) {
		struct acpi_madt_interrupt_source_override* override = get_entry(type, i);
		if (override->source != irq)
			continue;

		irq = override->gsi;
		break;
	}

	struct ioapic_desc* desc = get_ioapic_gsi(irq);
	if (unlikely(!desc))
		return -ENOENT;

	irq = irq - desc->gsi_base;
	ioapic_redtbl_read(desc->address, irq, re);
	return 0;
}

/* Should be used with the IRR/ISR registers to check if an interrupt is pending or in service */
static inline bool __lapic_vec_test(unsigned int reg, u8 vector) {
	int bank = vector >> 5;
	u32 mask = 1u << (vector & 31);
	return (lapic_read(reg + bank * 0x10) & mask) != 0;
}

static inline bool lapic_is_pending(int vector) {
	return __lapic_vec_test(LAPIC_REG_IRR_BASE, vector);
}

static bool apic_is_pending(unsigned int irq) {
	struct ioapic_redtbl_entry re;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&ioapic_lock, &irq_flags);
	int err = ioapic_get_irq(irq, &re);
	spinlock_release_irq_restore(&ioapic_lock, &irq_flags);
	if (err)
		return false;

	return lapic_is_pending(re.vector);
}

static int apic_enable_irq(unsigned int irq) {
	struct ioapic_redtbl_entry re;

	spinlock_acquire(&ioapic_lock);
	int err = ioapic_get_irq(irq, &re);
	if (err == 0)
		err = ioapic_set_irq(irq, re.vector, re.dest, false, re.polarity, re.trigger);
	spinlock_release(&ioapic_lock);

	return err;
}

static int apic_disable_irq(unsigned int irq) {
	struct ioapic_redtbl_entry re;

	spinlock_acquire(&ioapic_lock);
	int err = ioapic_get_irq(irq, &re);
	if (err == 0)
		err = ioapic_set_irq(irq, re.vector, re.dest, true, re.polarity, re.trigger);
	spinlock_release(&ioapic_lock);

	return err;
}

static int apic_install(unsigned int irq, const struct cpu* cpu, const struct isr* isr, int flags) {
	unsigned int t = flags & IRQ_FLAG_TRIGGER_MASK;

	u8 polarity = (t == IRQ_FLAG_TRIGGER_RISING || t == IRQ_FLAG_TRIGGER_HIGH) ? APIC_POLARITY_ACTIVE_HIGH : APIC_POLARITY_ACTIVE_LOW;
	u8 trigger = (t == IRQ_FLAG_TRIGGER_RISING || t == IRQ_FLAG_TRIGGER_FALLING) ? APIC_TRIGGER_EDGE : APIC_TRIGGER_LEVEL;
	spinlock_acquire(&ioapic_lock);
	int err = ioapic_set_irq(irq, isr->arch_specific.id, cpu->arch_specific.lapic_id, true, polarity, trigger);
	spinlock_release(&ioapic_lock);

	return err;
}

static int apic_uninstall(unsigned int irq) {
	spinlock_acquire(&ioapic_lock);
	int err = ioapic_set_irq(irq, ARCH_X86_64_IDT_SPURIOUS_VECTOR, 0, true, 0, 0);
	spinlock_release(&ioapic_lock);
	return err;
}

static void lapic_x1_send_ipi(u32 cpu, u8 vector, u8 delivm, u8 trigger, u8 shorthand) {
	while (lapic_read(LAPIC_REG_ICR_LOW) & LAPIC_ICR_DELIV_STATUS)
		arch_cpu_relax();

	if (shorthand == APIC_SHRT_CPU_TARGET)
		lapic_write(LAPIC_REG_ICR_HIGH, (cpu & 0xFF) << 24);

	u32 low = vector;
	low |= ((u32)delivm & 7) << 8;
	low |= ((u32)APIC_DEST_PHYSICAL) << 11;
	low |= ((u32)trigger & 1) << 15;
	low |= ((u32)shorthand & 3) << 18;
	lapic_write(LAPIC_REG_ICR_LOW, low);

	while (lapic_read(LAPIC_REG_ICR_LOW) & LAPIC_ICR_DELIV_STATUS)
		arch_cpu_relax();
}

static void lapic_x2_send_ipi(u32 cpu, u8 vector, u8 delivm, u8 trigger, u8 shorthand) {
	u64 icr = vector;
	icr |= ((u64)delivm & 7) << 8;
	icr |= ((u64)APIC_DEST_PHYSICAL) << 11;
	icr |= ((u64)trigger & 1) << 15;
	icr |= ((u64)shorthand & 3) << 18;
	if (shorthand == APIC_SHRT_CPU_TARGET)
		icr |= ((u64)cpu) << 32;
	lapic_write(LAPIC_REG_X2_ICR, icr);
}

static int apic_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags) {
	(void)flags;
	if (atomic_load(&use_x2apic))
		lapic_x2_send_ipi(cpu->arch_specific.lapic_id, isr->arch_specific.id, APIC_DM_FIXED, APIC_TRIGGER_EDGE, APIC_SHRT_CPU_TARGET);
	else
		lapic_x1_send_ipi(cpu->arch_specific.lapic_id, isr->arch_specific.id, APIC_DM_FIXED, APIC_TRIGGER_EDGE, APIC_SHRT_CPU_TARGET);
	return 0;
}

static int apic_ap_init(void) {
	lapic_write(LAPIC_REG_LVT_THERMAL, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_PERFORMANCE, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_LINT1, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);

	unsigned long nmi_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI);
	for (unsigned long i = 0; i < nmi_count; i++) {
		struct acpi_madt_lapic_nmi* nmi = get_entry(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI, i);
		if (nmi->uid == current_cpu()->arch_specific.acpi_id || nmi->uid == 0xff) {
			lapic_write(LAPIC_REG_LVT_LINT0 + 0x10 * nmi->lint, 
					ARCH_X86_64_IDT_NMI_VECTOR | LAPIC_LVT_DELIVERY_NMI | (nmi->flags << 12));
		}
	}

	lapic_write(LAPIC_REG_SPURIOUS, ARCH_X86_64_IDT_SPURIOUS_VECTOR | 0x100);
	return 0;
}

static int ioapic_init(void) {
	uacpi_table table;
	uacpi_status err = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &table);
	if (err != UACPI_STATUS_OK)
		goto err_nocleanup;

	madt = table.ptr;
	ioapic_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	ioapics = kzalloc(sizeof(*ioapics) * ioapic_count, MM_ZONE_NORMAL);
	if (!ioapics) {
		err = UACPI_STATUS_OUT_OF_MEMORY;
		goto err;
	}

	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct acpi_madt_ioapic* entry = get_entry(ACPI_MADT_ENTRY_TYPE_IOAPIC, i);
		if (unlikely(!entry)) {
			err = UACPI_STATUS_NOT_FOUND;
			goto err;
		}

		if (unlikely(entry->address % PAGE_SIZE != 0)) {
			err = UACPI_STATUS_MAPPING_FAILED;
			goto err;
		}
		ioapics[i].address = iomap(entry->address, PAGE_SIZE, PGPROT_UC);
		if (unlikely(IS_PTR_ERR(ioapics[i].address))) {
			err = UACPI_STATUS_MAPPING_FAILED;
			goto err;
		}

		/* Record the IRQ's this IOAPIC handles */
		u32 version = ioapic_read(ioapics[i].address, IOAPIC_REG_VERSION);
		ioapics[i].id = (ioapic_read(ioapics[i].address, IOAPIC_REG_ID) >> 24) & 0x0F;
		ioapics[i].version = version & 0xFF;
		ioapics[i].gsi_base = entry->gsi_base;
		ioapics[i].gsi_top = ioapics[i].gsi_base + (version >> 16 & 0xFF) + 1;
		printk(PRINTK_DBG "ioapic%lu: id %u, version %u, gsi %u-%u\n",
				i, ioapics[i].id, ioapics[i].version, ioapics[i].gsi_base, ioapics[i].gsi_top);
	}

	return 0;
err:
	if (ioapics) {
		for (unsigned long i = 0; i < ioapic_count; i++) {
			if (ioapics[i].address)
				iounmap(ioapics[i].address, PAGE_SIZE);
		}
		kfree(ioapics);
	}

	uacpi_table_unref(&table);
	madt = NULL;
err_nocleanup:
	switch (err) {
	case UACPI_STATUS_UNIMPLEMENTED:
		return -ENOSYS;
	case UACPI_STATUS_OUT_OF_MEMORY:
	case UACPI_STATUS_MAPPING_FAILED:
		return -ENOMEM;
	default:
		return -ENOTSUP;
	}
}

static void ioapic_mask_all(void) {
	i8259_initialize_and_mask();

	ioapic_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct ioapic_desc* ioapic = &ioapics[i];
		const struct ioapic_redtbl_entry re = {
			.vector = ARCH_X86_64_IDT_SPURIOUS_VECTOR,
			.delivery = 0,
			.dest = 0,
			.masked = 1,
			.polarity = 0,
			.trigger = 0,
			.destmode = 0
		};
		for (u32 j = ioapic->gsi_base; j < ioapic->gsi_top; j++)
			ioapic_redtbl_write(ioapic->address, j - ioapic->gsi_base, &re);
	}
}

static void spurious(struct isr* isr) {
	static unsigned long __atomic count = atomic_init(0);
	u32 x = lapic_read(LAPIC_REG_ISR_BASE + ((ARCH_X86_64_IDT_SPURIOUS_VECTOR & ~0x1f) >> 1));
	if (x & (1 << (ARCH_X86_64_IDT_SPURIOUS_VECTOR & 0x1f))) {
		apic_eoi(isr);
	} else {
		long _count = atomic_add_fetch_explicit(&count, 1, memory_order_relaxed);
		if (_count & (_count - 1))
			printk(PRINTK_WARN "apic: Spurious IRQ (count %lu)\n", _count);
	}
}

static void create_spurious_isr_if_needed(void) {
	static struct isr* spurious_isr = NULL;
	if (spurious_isr)
		return;

	spurious_isr = alloc_isr();
	if (!spurious_isr)
		out_of_memory();
	int err = arch_x86_64_register_isr_vector(spurious_isr, ARCH_X86_64_IDT_SPURIOUS_VECTOR, spurious, NULL, ISR_FLAG_TYPE_LIRQ, false);
	if (unlikely(err))
		panic("Failed to register spurious ISR vector: %d\n", err);
}

static int apic_x1_bsp_init(void) {
	if (!ioapics) {
		int err = ioapic_init();
		if (err)
			return err;
	}

	/*
	 * Check if limine has enabled x2apic already. If so, x1 cannot be used.
	 *
	 * Since x2apic has a higher rating than x1, this shouldn't happen. But if for some reason that's not the case,
	 * we add this check anyway.
	 */
	if (arch_x86_64_rdmsr(ARCH_X86_64_MSR_APIC_BASE) & APIC_BASE_FLAG_X2_ENABLE)
		return -ENXIO;

	physaddr_t lapic_physical = ROUND_DOWN(arch_x86_64_rdmsr(ARCH_X86_64_MSR_APIC_BASE), PAGE_SIZE);
	lapic_address = iomap(lapic_physical, PAGE_SIZE, PGPROT_UC);
	if (unlikely(IS_PTR_ERR(lapic_address)))
		return -ENOMEM;

	create_spurious_isr_if_needed();
	ioapic_mask_all();
	return apic_ap_init();
}

static const struct irqctl_ops x1_ops = {
	.init_bsp = apic_x1_bsp_init,
	.init_ap = apic_ap_init,
	.install = apic_install,
	.uninstall = apic_uninstall,
	.send_ipi = apic_send_ipi,
	.enable = apic_enable_irq,
	.disable = apic_disable_irq,
	.eoi = apic_eoi,
	.is_pending = apic_is_pending
};

INIT_TASK_DECLARE(vmm_init_task, acpi_tables_init_task);
static INIT_TASK_ARRAY_DEFINE(apic_irqctl_depends, &vmm_init_task, &acpi_tables_init_task);

static struct irqctl __irqctl apic_x1 = {
	.name = "apic_x1",
	.ops = &x1_ops,
	.rating = 75,
	.dependencies = apic_irqctl_depends
};

static int apic_x2_bsp_init(void) {
	if (!ioapics) {
		int err = ioapic_init();
		if (err)
			return err;
	}

	/*
	 * Limine enables x2apic automatically, so just check if it is enabled
	 * via the MSR.
	 */
	if (!(arch_x86_64_rdmsr(ARCH_X86_64_MSR_APIC_BASE) & APIC_BASE_FLAG_X2_ENABLE))
		return -ENODEV;

	create_spurious_isr_if_needed();
	ioapic_mask_all();
	atomic_store(&use_x2apic, true);
	int err = apic_ap_init();
	if (err != 0)
		atomic_store(&use_x2apic, false);
	return err;
}

static struct irqctl_ops x2_ops = {
	.init_bsp = apic_x2_bsp_init,
	.init_ap = apic_ap_init,
	.install = apic_install,
	.uninstall = apic_uninstall,
	.send_ipi = apic_send_ipi,
	.enable = apic_enable_irq,
	.disable = apic_disable_irq,
	.eoi = apic_eoi,
	.is_pending = apic_is_pending
};

static struct irqctl __irqctl apic_x2 = {
	.name = "apic_x2",
	.ops = &x2_ops,
	.rating = 100,
	.dependencies = apic_irqctl_depends
};

static bool lapic_timer_probe(void) {
	u32 _unused, edx;
	arch_x86_64_cpuid(0x01, 0, &_unused, &_unused, &_unused, &edx);
	return likely(!!(edx & (1 << 9)));
}

static void lapic_timer_handler(struct isr* isr) {
	current_cpu()->arch_specific.lapic_timer.handle = NULL;
	do_timer_events(isr->private);
}

static int lapic_timer_init(struct timer* self) {
	(void)self;

	/* Since this is the go-to interrupt controller on x86, this probably will never happen */
	if (unlikely(!lapic_address && !atomic_load(&use_x2apic)))
		return -ENODEV;

	struct isr* isr = alloc_isr();
	if (!isr)
		return -ENOMEM;
	int err = register_isr(isr, lapic_timer_handler, self, ISR_FLAG_TYPE_LIRQ);
	if (err) {
		free_isr(isr);
		return err;
	}

	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03); /* Set divisor to 16 */
	lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED); /* Make sure the LAPIC isn't in periodic mode, and mask the interrupt */

	u64 total_ticks = 0;
	const time_t sample_time_ms = 10;
	const int sample_count = 5;
	for (int i = 0; i < sample_count; i++) {
		lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX);
		mdelay(sample_time_ms); /* This uses another hardware backed timekeeper (usually hpet) */
		u32 ticks = U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT);
		total_ticks += ticks;
	}

	struct cpu* cpu = current_cpu();
	cpu->arch_specific.lapic_timer.ticks_per_1ms = (total_ticks / sample_count) / sample_time_ms;
	cpu->arch_specific.lapic_timer.handle = NULL;

	/* Enable timer with the ISR vector */
	lapic_write(LAPIC_REG_LVT_TIMER, isr->arch_specific.id | LAPIC_LVT_TIMER_ONESHOT);
	return 0;
}

static int get_ticks(struct timespec ts, u32* ticks) {
	time_t ms = timespec_ms(ts);

	u64 _ticks;
	if (__builtin_mul_overflow(ms, current_cpu()->arch_specific.lapic_timer.ticks_per_1ms, &_ticks))
		return -ERANGE;
	if (_ticks > U32_MAX)
		return -ERANGE;

	if (_ticks == 0)
		*ticks = 1;
	else
		*ticks = _ticks;
	return 0;
}

static inline void arm(void* handle, u32 ticks) {
	current_cpu()->arch_specific.lapic_timer.handle = handle;
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);
}

static int lapic_timer_arm(struct timer* self, struct timespec fromnow, void* handle) {
	(void)self;
	struct arch_cpu* acpu = &current_cpu()->arch_specific;

	u32 ticks;
	int err = get_ticks(fromnow, &ticks);
	if (err)
		return err;

	if (acpu->lapic_timer.handle == handle)
		return 0;
	else if (acpu->lapic_timer.handle)
		return -EBUSY;
	arm(handle, ticks);
	return 0;
}

static int lapic_timer_rearm(struct timer* self, struct timespec fromnow, void* handle) {
	(void)self;

	u32 ticks;
	int err = get_ticks(fromnow, &ticks);
	if (err)
		return err;

	arm(handle, ticks);
	return 0;
}

static void lapic_timer_cancel(struct timer* self, void* handle) {
	(void)self;
	struct arch_cpu* acpu = &current_cpu()->arch_specific;
	if (acpu->lapic_timer.handle == handle) {
		acpu->lapic_timer.handle = NULL;
		lapic_write(LAPIC_REG_TIMER_INITIAL, 0);
	}
}

static const struct timer_ops lapic_timer_ops = {
	.probe = lapic_timer_probe,
	.init = lapic_timer_init,
	.arm = lapic_timer_arm,
	.rearm = lapic_timer_rearm,
	.cancel = lapic_timer_cancel
};

/*
 * irq_init_task will ensure the LAPIC is mapped to virtual memory, if the APIC is selected.
 * If the APIC is not selected, the LAPIC timer shouldn't be used.
 */
INIT_TASK_DECLARE(irq_init_task, timekeeper_init_task, heap_init_task, irq_ap_init_task, timekeeper_ap_init_task);
static INIT_TASK_ARRAY_DEFINE(lapic_timer_depends,
		&irq_init_task, &timekeeper_init_task, &heap_init_task,
		&irq_ap_init_task, &timekeeper_ap_init_task);

static struct timer __timer lapic_timer = {
	.name = "lapic",
	.flags = TIMER_FLAG_PERCPU,
	.ops = &lapic_timer_ops,
	.probe_dependencies = NULL,
	.init_dependencies = lapic_timer_depends,
	.cpus_initialized = atomic_init(0)
};
