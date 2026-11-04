#include <crater/compiler.h>
#include <crater/vmm.h>
#include <crater/timekeeper.h>
#include <crater/printk.h>
#include <crater/panic.h>

#include <arch/io.h>

#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#define HPET_TIMER_CONFIG(t) (0x20 + 0x4 * (t))
#define HPET_TIMER_COMPARATOR(t) (0x21 + 0x4 * (t))
#define HPET_TIMER_FSBROUTE(t) (0x22 + 0x4 * (t))

enum hpet_reg {
	HPET_REG_CAPS = 0x00,
	HPET_REG_CONFIG = 0x02,
	HPET_REG_INTSTATUS = 0x04,
	HPET_REG_COUNTER = 0x1e
};

#define HPET_TIMER_CONFIG_LEVEL_TRIGGER (1ul << 1)
#define HPET_TIMER_CONFIG_IRQENABLE (1ul << 2)
#define HPET_TIMER_CONFIG_PERIODIC (1ul << 3)
#define HPET_TIMER_CONFIG_FORCE32BIT (1ul << 8)
#define HPET_TIMER_CONFIG_FSBENABLE (1ul << 14)
#define HPET_TIMER_CONFIG_FSBCAPABLE (1ul << 15)

#define HPET_CAP_LEGACY (1 << 15)

static struct acpi_hpet* hpet;
static u64 __iomem* hpet_virtual;
static bool hpet_32bit = false;

static inline u64 hpet_read(enum hpet_reg reg) {
	return readq(hpet_virtual + reg);
}

static inline void hpet_write(enum hpet_reg reg, u64 x) {
	writeq(hpet_virtual + reg, x);
}

static time_t ticks(void) {
	u64 value = hpet_read(HPET_REG_COUNTER);
	return hpet_32bit ? value & U32_MAX : value; /* early only for 32 bit, so wrap around should never happen */
}

static struct timekeeper_source _hpet_source;
static struct timekeeper_source* hpet_source = NULL;

static int init(struct timekeeper* self, struct timekeeper_source** out) {
	if (hpet_source) {
		*out = hpet_source;
		return 0;
	}

	uacpi_table table;
	uacpi_status status = uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &table);
	if (status != UACPI_STATUS_OK) {
		if (status == UACPI_STATUS_MAPPING_FAILED || status == UACPI_STATUS_OUT_OF_MEMORY)
			return -ENOMEM;
		return -ENODEV;
	}

	hpet = table.ptr;
	if (hpet->address.address_space_id != ACPI_AS_ID_SYS_MEM)
		return -ENXIO;
	physaddr_t address = hpet->address.address;
	hpet_virtual = iomap(address, PAGE_SIZE, PGPROT_UC);
	if (IS_PTR_ERR(hpet_virtual))
		return PTR_ERR(hpet_virtual);

	/* After this point, we need cleanup on error */
	int err;

	/* Get the capabilities and the clock period, and use the clock period to get the frequency */
	u64 caps = hpet_read(HPET_REG_CAPS);
	u32 femtoperiod = caps >> 32;
	if (unlikely(femtoperiod == 0)) {
		err = -EIO;
		goto err_cleanup;
	}
	_hpet_source.fb_freq = 1000000000000000ull / femtoperiod;
	_hpet_source.ticks = ticks;
	_hpet_source.private = NULL;

	/* Make sure the HPET is disabled and set the initial count to zero */
	hpet_write(HPET_REG_CONFIG, 0);
	hpet_write(HPET_REG_COUNTER, 0);

	/* If only a 32 bit counter, this can ONLY be used for early boot */
	if (!(hpet->block_id & ACPI_HPET_COUNT_SIZE_CAP)) {
		self->flags |= TIMEKEEPER_FLAG_EARLY_ONLY;
		hpet_32bit = true;
	}

	hpet_source = &_hpet_source;
	*out = hpet_source;

	/* Enable the HPET */
	hpet_write(HPET_REG_CONFIG, 1);
	return 0;
err_cleanup:
	bug(iounmap(hpet_virtual, PAGE_SIZE) != 0);
	hpet_virtual = NULL;
	uacpi_table_unref(&table);
	return err;
}

INIT_TASK_DECLARE(vmm_init_task, acpi_tables_init_task);
static INIT_TASK_ARRAY_DEFINE(hpet_depends, &vmm_init_task, &acpi_tables_init_task);

static struct timekeeper __timekeeper hpet_timekeeper = {
	.name = "hpet",
	.init = init,
	.rating = 60, /* Slow to access, but very accurate */
	.flags = TIMEKEEPER_FLAG_EARLY,
	.dependencies = hpet_depends
};
