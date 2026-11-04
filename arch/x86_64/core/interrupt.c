#include <crater/printk.h>
#include <crater/panic.h>
#include <crater/irq.h>
#include <crater/sched.h>

#include <arch/irq_flags.h>
#include <arch/context.h>
#include <arch/asm/linkage.h>

#include <x86_64/idt.h>
#include <x86_64/fault.h>
#include <x86_64/interrupt.h>
#include <x86_64/asm/segment.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/flags.h>

#define EXCEPTION_COUNT 32
#define EXCEPTION_DEFINE(n, eh, f) [n] = { .handler = NULL, .flags = 0, .private = NULL, .arch_specific = { .id = n, .flags = f, .ehandler = eh } }

#define ARCH_ISR_FLAG_NEED_EOI (1 << 0) /* Call the IRQ controller's EOI function */
#define ARCH_ISR_FLAG_PARANOID_GSBASE (1 << 1)  /* Check gsbase */
#define ARCH_ISR_FLAG_EXCEPTION_IRQSOFF (1 << 2) /* Exception should be handled with IRQ's off */

static const char* exception_strings[EXCEPTION_COUNT] = {
	"Division by 0", "Debug", "NMI", "Breakpoint", "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
	"Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
	"Page Fault", NULL, "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Exception",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static _Noreturn void generic_exception(struct isr* isr, struct arch_context* ctx) {
	const char* string = exception_strings[isr->arch_specific.id];
	const int cpl = (ctx->cs & ARCH_X86_64_SEGMENT_CPL3) ? 3 : 0;
	if (likely(string))
		panic("CPU exception %s (%d): RIP: %#lx, CPL: %d, ERR: %#lx", string, isr->arch_specific.id, ctx->rip, cpl, ctx->err_code);
	panic("Unkown exception %d: RIP: %#lx, CPL: %d, ERR: %#lx", isr->arch_specific.id, ctx->rip, cpl, ctx->err_code);
}

extern const uintptr_t isr_table[ARCH_X86_64_IDT_ENTRY_COUNT];
static struct isr* __atomic isr_handlers[ARCH_X86_64_IDT_ENTRY_COUNT] = { 0 };
static SPINLOCK_DEFINE(isr_handlers_lock);

static struct isr exceptions[EXCEPTION_COUNT] = {
	EXCEPTION_DEFINE(ARCH_X86_64_IDT_NMI_VECTOR, generic_exception, ARCH_ISR_FLAG_PARANOID_GSBASE | ARCH_ISR_FLAG_EXCEPTION_IRQSOFF),
	EXCEPTION_DEFINE(ARCH_X86_64_IDT_DOUBLE_FAULT_VECTOR, generic_exception, ARCH_ISR_FLAG_PARANOID_GSBASE | ARCH_ISR_FLAG_EXCEPTION_IRQSOFF),
	EXCEPTION_DEFINE(ARCH_X86_64_IDT_GENERAL_PROTECTION_FAULT_VECTOR, arch_x86_64_general_protection_fault, 0),
	EXCEPTION_DEFINE(ARCH_X86_64_IDT_PAGE_FAULT_VECTOR, arch_x86_64_page_fault, 0),
	EXCEPTION_DEFINE(ARCH_X86_64_IDT_MACHINE_CHECK_VECTOR, generic_exception, ARCH_ISR_FLAG_PARANOID_GSBASE | ARCH_ISR_FLAG_EXCEPTION_IRQSOFF)
};

struct idt_entry {
	u16 handler_low;
	u16 cs;
	u8 ist;
	u8 flags;
	u16 handler_mid;
	u32 handler_high;
	u32 _zero;
} __attribute__((packed, aligned(8)));

static struct idt_entry idt[ARCH_X86_64_IDT_ENTRY_COUNT] = { 0 };

static void load_idt(void) {
	static const struct {
		u16 limit;
		struct idt_entry* idt;
	} __attribute__((packed, aligned(8))) idtr = {
		.limit = sizeof(idt) - 1,
		.idt = idt
	};
	__asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

void arch_x86_64_idt_init(void) {
	/* Only runs on the BSP before any AP's are brought up */
	static bool __atomic init = atomic_init(false);
	if (atomic_exchange_explicit(&init, true, memory_order_relaxed)) {
		load_idt();
		return;
	}

	/* Set up CPU exceptions */
	for (size_t i = 0; i < ARRAY_SIZE(exceptions); i++) {
		struct isr* exception = &exceptions[i];
		if (!exception->arch_specific.ehandler) {
			exception->arch_specific.id = i;
			exception->arch_specific.flags = 0;
			exception->arch_specific.ehandler = generic_exception;
		}
		atomic_store_explicit(&isr_handlers[i], &exceptions[i], memory_order_relaxed);
	}

	/* Now set up all the IDT entries */
	int ist = 0;
	for (size_t i = 0; i < ARRAY_SIZE(idt); i++) {
		struct idt_entry* entry = &idt[i];
		const struct isr* isr = atomic_load_explicit(&isr_handlers[i], memory_order_relaxed);
		*entry = (struct idt_entry){
			.handler_low = isr_table[i] & U16_MAX, .cs = ARCH_X86_64_SEGMENT_KERNEL_CODE,
			.ist = (isr && (isr->arch_specific.flags & ARCH_ISR_FLAG_PARANOID_GSBASE)) ? ++ist : 0, .flags = 0x8e, .handler_mid = (isr_table[i] >> 16) & U16_MAX,
			.handler_high = ((u64)isr_table[i] >> 32) & U32_MAX, ._zero = 0
		};
	}
	bug(ist > ARCH_X86_64_IDT_IST_COUNT);

	load_idt();
}

static void handle_exception(struct isr* isr, struct arch_context* ctx, bool irq) {
	if ((!(ctx->rflags & ARCH_X86_64_RFLAGS_IF) || current_thread()->preempt_count) && irq) {
		printk(PRINTK_EMERG "Trap %u occurred in atomic context\n", isr->arch_specific.id);
		generic_exception(isr, ctx);
	}

	if (likely(irq))
		local_irq_enable();
	isr->arch_specific.ehandler(isr, ctx);
	local_irq_disable();
}

static inline void swapgs(void) {
	__asm__ volatile("swapgs" : : : "memory");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage void arch_x86_64_do_interrupt(struct arch_context* ctx) {
	struct isr* isr = atomic_load_explicit(&isr_handlers[ctx->vector], memory_order_acquire);
	if (unlikely(!isr))
		panic("Unregistered ISR %lu", ctx->vector);

	bug((u64)isr->arch_specific.id != ctx->vector);

	const bool paranoid_gsbase = (isr->arch_specific.flags & ARCH_ISR_FLAG_PARANOID_GSBASE);
	const bool bad_gsbase = paranoid_gsbase ? (arch_x86_64_rdmsr(ARCH_X86_64_MSR_GS_BASE) < KERNEL_SPACE_START) : false;
	if (unlikely(bad_gsbase))
		swapgs();

	const bool exception = isr->arch_specific.id < EXCEPTION_COUNT;
	const bool exception_irqs_on = !(isr->arch_specific.flags & ARCH_ISR_FLAG_EXCEPTION_IRQSOFF);
	if (exception) {
		handle_exception(isr, ctx, exception_irqs_on);
	} else if (isr->handler) {
		preempt_offset(PREEMPT_HARDIRQ_OFFSET);
		isr->handler(isr);
		do_pending_irqs();
		preempt_offset(-PREEMPT_HARDIRQ_OFFSET);
	} else {
		printk(PRINTK_CRIT "int%lu: No handler\n", ctx->vector);
	}

	if (isr->arch_specific.flags & ARCH_ISR_FLAG_NEED_EOI)
		irqctl_eoi(isr);

	if (unlikely(paranoid_gsbase)) {
		if (unlikely(bad_gsbase))
			swapgs();
		return;
	}
	if (unlikely(exception && !exception_irqs_on))
		return;

	if (!exception)
		softirq_execute();

	if (current_cpu()->need_resched && current_thread()->preempt_count == 0)
		schedule_preempt_irq();
	if (ctx->cs & ARCH_X86_64_SEGMENT_CPL3)
		sched_userspace_check(ctx);
}

__diag_pop();

int arch_register_isr(struct isr* isr) {
	int err = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&isr_handlers_lock, &irq_flags);

	/* Check for a free spot while simultaneously looking for a duplicate */
	int id = -1;
	for (size_t i = EXCEPTION_COUNT; i < ARRAY_SIZE(isr_handlers); i++) {
		if (atomic_load_explicit(&isr_handlers[i], memory_order_relaxed) == isr) {
			err = -EEXIST;
			goto out;
		} else if (id == -1 && atomic_load_explicit(&isr_handlers[i], memory_order_relaxed) == NULL) {
			id = i;
		}
	}

	/* Now publish the ISR */
	if (id != -1) {
		isr->arch_specific.id = id;
		isr->arch_specific.flags = ARCH_ISR_FLAG_NEED_EOI;
		atomic_store_explicit(&isr_handlers[id], isr, memory_order_release);
	} else {
		err = -ENOSPC;
	}

out:
	spinlock_release_irq_restore(&isr_handlers_lock, &irq_flags);
	return err;
}

int arch_x86_64_register_isr_vector(struct isr* isr, int vector, isrhandler_t handler, void* private, int flags, bool need_eoi) {
	if (vector < EXCEPTION_COUNT || (size_t)vector >= ARRAY_SIZE(isr_handlers))
		return -EINVAL;

	int err = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&isr_handlers_lock, &irq_flags);

	if (atomic_load(&isr_handlers[vector]) == NULL) {
		__init_isr(isr, handler, private, flags);
		isr->arch_specific.id = vector;
		isr->arch_specific.flags = need_eoi ? ARCH_ISR_FLAG_NEED_EOI : 0;
		atomic_store_explicit(&isr_handlers[vector], isr, memory_order_release);
	} else {
		err = -EEXIST;
	}

	spinlock_release_irq_restore(&isr_handlers_lock, &irq_flags);
	return err;
}

/* Requires CPU synchronization that I do not want to implement right now */
int arch_unregister_isr(struct isr* isr) {
	(void)isr;
	return -ENOSYS;
}

unsigned long arch_local_irq_read(void) {
	return (arch_x86_64_read_rflags() & ARCH_X86_64_RFLAGS_IF) ? ARCH_IRQ_ENABLED : ARCH_IRQ_DISABLED;
}

void arch_local_irq_restore(unsigned long flags) {
	if (flags == ARCH_IRQ_ENABLED)
		__asm__ volatile("sti" : : : "memory");
	else
		__asm__ volatile("cli" : : : "memory");
}
