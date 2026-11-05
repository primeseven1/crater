#include <crater/printk.h>
#include <crater/vmm.h>
#include <crater/limine.h>
#include <crater/trace.h>
#include <crater/timekeeper.h>
#include <crater/pci.h>
#include <crater/slab.h>
#include <crater/irq.h>
#include <crater/sched.h>
#include <crater/workqueue.h>

#include <acpi/glue.h>
#include <acpi/common.h>
#include <uacpi/kernel_api.h>

#include "internal.h"

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
	const char* _level = NULL;
	switch (level) {
	case UACPI_LOG_DEBUG:
	case UACPI_LOG_TRACE:
		_level = PRINTK_DBG;
		break;
	case UACPI_LOG_INFO:
		_level = PRINTK_INFO;
		break;
	case UACPI_LOG_WARN:
		_level = PRINTK_WARN;
		break;
	case UACPI_LOG_ERROR:
		_level = PRINTK_ERR;
		break;
	}
	printk("%sacpi: %s", _level, str);
}

void* uacpi_kernel_map(uacpi_phys_addr physical, uacpi_size size) {
	size_t page_offset = physical % PAGE_SIZE;
	physaddr_t _physical = physical - page_offset;
	u8* virtual = vm_map(NULL, size + page_offset, PGPROT_READ | PGPROT_WRITE, VMM_PHYSICAL, &_physical);
	return IS_PTR_ERR(virtual) ? UACPI_MAP_FAILED : virtual + page_offset;
}

void uacpi_kernel_unmap(void* virtual, uacpi_size size) {
	size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	void* _virtual = (u8*)virtual - page_offset;
	int err = vm_unmap(_virtual, size + page_offset, 0);
	if (unlikely(err)) {
		dump_stack();
		printk(PRINTK_ERR "acpi: vm_unmap() failed with error code %i\n", err);
	}
}

static struct limine_rsdp_request __limine_request rsdp_request = {
	.request.id = LIMINE_RSDP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out) {
	if (!rsdp_request.response)
		return UACPI_STATUS_NOT_FOUND;

	int supported_revisions[] = { 0, 1, 2, 3, 4, 5, 6 };
	int revision = limine_match_base_revision(supported_revisions, ARRAY_SIZE(supported_revisions));
	if (unlikely(revision == -1))
		return UACPI_STATUS_INTERNAL_ERROR;

	if (revision == 3)
		*out = rsdp_request.response->physical;
	else
		*out = hhdm_physical(rsdp_request.response->virtual);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out) {
	struct pci_device* device;
	int err = pci_handle_open(address.segment, address.bus, address.device, address.function, &device);
	uacpi_status ret = acpi_errno_to_uacpi_status(err);
	if (ret == UACPI_STATUS_OK)
		*out = device;
	return ret;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
	pci_handle_close((struct pci_device*)handle);
}

static uacpi_status pci_read(struct pci_device* device, uacpi_size offset, uacpi_u32* out, uacpi_size count) {
	union {
		u8 out8;
		u16 out16;
		u32 out32;
	} _out;

	int err;

	switch (count) {
	case sizeof(u8):
		err = pci_read_config_byte(device, offset, &_out.out8);
		*out = _out.out8;
		break;
	case sizeof(u16):
		err = pci_read_config_word(device, offset, &_out.out16);
		*out = _out.out16;
		break;
	case sizeof(u32):
		err = pci_read_config_dword(device, offset, &_out.out32);
		*out = _out.out32;
		break;
	default:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return acpi_errno_to_uacpi_status(err);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
	uacpi_u32 out;
	uacpi_status status = pci_read((struct pci_device*)device, offset, &out, sizeof(u8));
	*value = out;
	return status;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
	uacpi_u32 out;
	uacpi_status status = pci_read((struct pci_device*)device, offset, &out, sizeof(u16));
	*value = out;
	return status;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
	return pci_read((struct pci_device*)device, offset, value, sizeof(u32));
}

static uacpi_status pci_write(struct pci_device* device, uacpi_size offset, uacpi_u32 value, uacpi_size count) {
	int err;

	switch (count) {
	case sizeof(u8):
		err = pci_write_config_byte(device, offset, (u8)value);
		break;
	case sizeof(u16):
		err = pci_write_config_word(device, offset, (u16)value);
		break;
	case sizeof(u32):
		err = pci_write_config_dword(device, offset, (u32)value);
		break;
	default:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return acpi_errno_to_uacpi_status(err);
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
	return pci_write((struct pci_device*)device, offset, value, sizeof(u8));
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
	return pci_write((struct pci_device*)device, offset, value, sizeof(u16));
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
	return pci_write((struct pci_device*)device, offset, value, sizeof(u32));
}

void* uacpi_kernel_alloc(uacpi_size size) {
	return kmalloc(size, MM_ZONE_NORMAL);
}

void* uacpi_kernel_alloc_zeroed(uacpi_size size) {
	return kzalloc(size, MM_ZONE_NORMAL);
}

void uacpi_kernel_free(void* ptr) {
	if (ptr)
		kfree(ptr);
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
	struct timespec ts = time_fromboot();
	time_t ns = ts.tv_sec * 1000000000 + ts.tv_nsec;
	return ns;
}

void uacpi_kernel_stall(uacpi_u8 us) {
	udelay(us);
}

void uacpi_kernel_sleep(uacpi_u64 ms) {
	msleep(ms);
}

uacpi_handle uacpi_kernel_create_semaphore(uacpi_u32 initial_units) {
	struct semaphore* sem = kmalloc(sizeof(*sem), MM_ZONE_NORMAL);
	if (sem)
		semaphore_init(sem, initial_units);
	return sem;
}

void uacpi_kernel_free_semaphore(uacpi_handle handle) {
	kfree((struct semaphore*)handle);
}

uacpi_handle uacpi_kernel_create_work_item(void) {
	return kmalloc(sizeof(struct workqueue_item), MM_ZONE_NORMAL);
}

void uacpi_kernel_free_work_item(uacpi_handle handle) {
	kfree(handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
	return current_thread();
}

static_assert(sizeof(uacpi_interrupt_state) == sizeof(unsigned long), "sizeof(uacpi_interrupt_state) == sizeof(unsigned long)");

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {
	return local_irq_save();
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state) {
	local_irq_restore(state);
}

uacpi_status uacpi_kernel_wait_for_semaphore(uacpi_handle handle, uacpi_u16 timeout) {
	struct semaphore* sem = handle;
	if (semaphore_try(sem)) /* This code can run before a scheduler is up. So try the semaphore first before attempting to sleep */
		return UACPI_STATUS_OK;
	if (timeout == 0)
		return UACPI_STATUS_TIMEOUT;

	int err = (timeout == 0xFFFF) ? semaphore_wait(sem, 0) : semaphore_wait_timed(sem, timeout, 0);
	return acpi_errno_to_uacpi_status(err);
}

void uacpi_kernel_signal_semaphore(uacpi_handle handle) {
	semaphore_signal((struct semaphore*)handle);
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* request) {
	switch (request->type) {
	case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
		break;
	case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
		printk(PRINTK_CRIT "acpi: fatal firmware error: type=%d code=%d arg=%ld\n",
				request->fatal.type, request->fatal.code, request->fatal.arg);
		break;
	}
	return UACPI_STATUS_OK;
}

static LIST_HEAD_DEFINE(irq_list);
static MUTEX_DEFINE(irq_list_lock);

struct uacpi_irq_ctx {
	unsigned int irqnum;
	uacpi_interrupt_handler handler;
	uacpi_handle ctx;
	struct list_node node;
};

static irqreturn_t uacpi_irq(unsigned int number, void* arg) {
	(void)number;
	struct uacpi_irq_ctx* actx = arg;
	uacpi_interrupt_ret iret = actx->handler(actx->ctx);
	return iret == UACPI_INTERRUPT_HANDLED ? IRQ_HANDLED : IRQ_NONE;
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
	struct uacpi_irq_ctx* actx = kmalloc(sizeof(*actx), MM_ZONE_NORMAL);
	if (!actx)
		return UACPI_STATUS_OUT_OF_MEMORY;

	actx->irqnum = irq;
	actx->handler = handler;
	actx->ctx = ctx;
	list_node_init(&actx->node);

	int err = request_irq(irq, uacpi_irq, IRQ_FLAG_SHARED | IRQ_FLAG_TRIGGER_LOW, "acpi", actx);
	if (err) {
		kfree(actx);
		return acpi_errno_to_uacpi_status(err);
	}

	*out_irq_handle = actx;
	mutex_acquire(&irq_list_lock);
	list_add(&irq_list, &actx->node);
	mutex_release(&irq_list_lock);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
	(void)handler;
	struct uacpi_irq_ctx* actx = irq_handle;
	mutex_acquire(&irq_list_lock);
	free_irq(actx->irqnum, actx);
	list_remove(&actx->node);
	mutex_release(&irq_list_lock);
	kfree(actx);
	return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
	spinlock_t* lock = kmalloc(sizeof(*lock), MM_ZONE_NORMAL);
	if (lock)
		spinlock_init(lock);
	return lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
	kfree((spinlock_t*)handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
	unsigned long flags;
	spinlock_t* lock = handle;
	spinlock_acquire_irq_save(lock, &flags);
	return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
	spinlock_release_irq_restore((spinlock_t*)handle, &flags);
}

static_assert(__builtin_types_compatible_p(uacpi_cpu_flags, unsigned long), "__builtin_types_compatible_p(uacpi_cpu_flags, unsigned long)");

static struct workqueue* gpe_workqueue;
static struct workqueue* notify_workqueue;

void uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_handle work_item, uacpi_work_handler handler, uacpi_handle ctx) {
	struct workqueue_item* item = work_item;
	*item = (struct workqueue_item){ .fn = handler, .arg = ctx, .link = LIST_NODE_INITIALIZER };
	struct workqueue* const queue = (type == UACPI_WORK_GPE_EXECUTION) ? gpe_workqueue : notify_workqueue;
	workqueue_schedule(queue, item);
}

static inline void acpi_disable_irqs_locked(void) {
	struct uacpi_irq_ctx* ctx;
	list_for_each_entry(ctx, &irq_list, node)
		disable_irq(ctx->irqnum);
}

static inline void acpi_enable_irqs_locked(void) {
	struct uacpi_irq_ctx* ctx;
	list_for_each_entry(ctx, &irq_list, node)
		enable_irq(ctx->irqnum);
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
	mutex_acquire(&irq_list_lock);

	acpi_disable_irqs_locked(); /* Synchronizes IRQ's */
	workqueue_synchronize(gpe_workqueue);
	workqueue_synchronize(notify_workqueue);
	acpi_enable_irqs_locked();

	mutex_release(&irq_list_lock);
	return UACPI_STATUS_OK;
}

void acpi_wait_for_gpe_work_completion(void) {
	workqueue_synchronize(gpe_workqueue);
}

void acpi_glue_init(void) {
	gpe_workqueue = workqueue_create(TOPOLOGY_BSP | TOPOLOGY_NO_MIGRATE, "acpi_gpe");
	notify_workqueue = workqueue_create(0, "acpi_notify");
	if (!gpe_workqueue || !notify_workqueue)
		panic("Failed to create ACPI workqueues");
}
