#include <crater/init.h>
#include <crater/timekeeper.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/sched.h>
#include <crater/workqueue.h>

#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/io.h>
#include <uacpi/event.h>
#include <uacpi/opregion.h>
#include <uacpi/resources.h>
#include <uacpi/tables.h>

#include <acpi/common.h>
#include <acpi/driver.h>

#define EC_FLAG_OBF (1 << 0)
#define EC_FLAG_IBF (1 << 1)
#define EC_FLAG_BURST (1 << 4)
#define EC_FLAG_SCI_EVT (1 << 5)
#define EC_FLAG_SMI_EVT (1 << 7)

#define EC_COMMAND_READ 0x80
#define EC_COMMAND_WRITE 0x81
#define EC_COMMAND_BURST_ENABLE 0x82
#define EC_COMMAND_BURST_DISABLE 0x83
#define EC_COMMAND_QUERY 0x84

static inline u8 raw_read(struct acpi_gas* gas) {
	u64 value;
	bug(uacpi_gas_read(gas, &value) != UACPI_STATUS_OK);
	return (u8)value;
}

static inline void raw_write(struct acpi_gas* gas, u8 value) {
	bug(uacpi_gas_write(gas, value) != UACPI_STATUS_OK);
}

/* Wait for a flag to get set */
static int raw_wait(struct acpi_gas* gas, u8 bit, bool value) {
	const int timeout_us = 1000 * 1000;
	const int delay_time = 5;

	/* Most EC transactions finish in a few microseconds, so spin for ~1ms before sleeping */
	for (int i = 0; i < 1000 / delay_time; i++) {
		if (!!(raw_read(gas) & bit) == value)
			return 0;
		udelay(delay_time);
	}

	/* Fall back to sleeping */
	const time_t start = timespec_us(time_fromboot());
	while (1) {
		u8 reg = raw_read(gas);
		if (!!(reg & bit) == value)
			break;
		if (timespec_us(time_fromboot()) - start >= timeout_us) {
			printk(PRINTK_ERR "ec: Timed out after %d us\n", timeout_us);
			return -ETIMEDOUT;
		}
		msleep(1);	
	}
	return 0;
}

struct ec_device {
	uacpi_namespace_node* node;
	u16 gpe_index;
	struct workqueue_item wq_item;
	struct acpi_gas ctrl, data;
	bool global_lock;
	u32 global_lock_seq;
	struct list_node link;
	mutex_t mtx;
};

static inline int wait_ibf(struct ec_device* device) {
	return raw_wait(&device->ctrl, EC_FLAG_IBF, false);
}

static inline int wait_obf(struct ec_device* device) {
	return raw_wait(&device->ctrl, EC_FLAG_OBF, true);
}

static int ec_read(struct ec_device* device, u8 addr, u8* out) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->ctrl, EC_COMMAND_READ);
	timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->data, addr);

	timeout = wait_obf(device);
	if (timeout == 0)
		*out = raw_read(&device->data);
	return timeout;
}

static int ec_write(struct ec_device* device, u8 addr, u8 value) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->ctrl, EC_COMMAND_WRITE);
	timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->data, addr);

	timeout = wait_ibf(device);
	if (timeout == 0)
		raw_write(&device->data, value);
	return timeout;
}

static int ec_burst_enable(struct ec_device* device) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;

	raw_write(&device->ctrl, EC_COMMAND_BURST_ENABLE);
	timeout = wait_obf(device);
	if (timeout == 0) /* discard ACK */
		raw_read(&device->data);
	return timeout;
}

static int ec_burst_disable(struct ec_device* device) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;

	raw_write(&device->ctrl, EC_COMMAND_BURST_DISABLE);
	return wait_ibf(device);
}

static int ec_get_event_index(struct ec_device* device, u8* index) {
	*index = 0;

	/* Check to see if there is an event */
	if (!(raw_read(&device->ctrl) & EC_FLAG_SCI_EVT))
		return 0;

	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;

	/* Now query for the event index */
	raw_write(&device->ctrl, EC_COMMAND_QUERY);
	timeout = wait_obf(device);
	*index = (timeout == 0) ? raw_read(&device->data) : 0;

	return timeout;
}

/* Check if an EC device is working */
static bool ec_ok(struct ec_device* device) {
	int timeout = wait_ibf(device);
	if (timeout)
		return false;

	/* Issue a read command and see if it works */
	raw_write(&device->ctrl, EC_COMMAND_READ);
	timeout = wait_ibf(device);
	if (timeout)
		return false;

	/* Now read from address 0 and see if that works */
	raw_write(&device->data, 0);
	timeout = wait_obf(device);
	if (timeout)
		return false;

	/* It worked, so just discard the data */
	raw_read(&device->data);
	return true;
}

static inline void ec_lock(struct ec_device* device) {
	mutex_acquire(&device->mtx);
	if (device->global_lock)
		bug(uacpi_acquire_global_lock(0xFFFF, &device->global_lock_seq) != 0);
}

static inline void ec_unlock(struct ec_device* device) {
	if (device->global_lock)
		bug(uacpi_release_global_lock(device->global_lock_seq) != UACPI_STATUS_OK);
	mutex_release(&device->mtx);
}

static u8 ec_get_event(struct ec_device* device) {
	u8 index;
	ec_lock(device);
	int err = ec_get_event_index(device, &index);
	ec_unlock(device);

	if (err) {
		if (err == -ETIMEDOUT)
			printk(PRINTK_ERR "ec: Hardware timeout getting EC event\n");
		else
			printk(PRINTK_ERR "ec: Unknown error getting EC event: %d\n", err);
		return 0;
	}
	return index;
}

static void ec_work(uacpi_handle handle) {
	struct ec_device* device = handle;
	
	u8 event = ec_get_event(device);
	while (event) {
		const char* hex_chars = "0123456789ABCDEF";

		char method_name[5] = { '_', 'Q', hex_chars[(event >> 4) & 0xF], hex_chars[event & 0xF], '\0' };
		uacpi_status status = uacpi_eval(device->node, method_name, UACPI_NULL, UACPI_NULL);
		if (uacpi_unlikely_error(status))
			printk(PRINTK_ERR "ec: Failed to execute method %s: %s\n", method_name, uacpi_status_to_string(status));

		event = ec_get_event(device);
	}

	uacpi_finish_handling_gpe(NULL, device->gpe_index);
}

static uacpi_status ec_aml_rw(uacpi_region_op op, uacpi_region_rw_data* data) {
	if (data->byte_width != 1)
		return UACPI_STATUS_INVALID_ARGUMENT;

	int timeout;
	u8 d;

	struct ec_device* device = data->handler_context;
	ec_lock(device);

	const bool burst_on = ec_burst_enable(device) == 0;

	if (op == UACPI_REGION_OP_READ) {
		timeout = ec_read(device, data->offset, &d);
		if (timeout == 0)
			data->value = d;
		else
			data->value = 0;
	} else if (op == UACPI_REGION_OP_WRITE) {
		timeout = ec_write(device, data->offset, data->value);
	} else {
		bug("invalid EC op"); /* Silence the warning about timeout being uninitialized */
	}

	if (likely(burst_on))
		ec_burst_disable(device);

	ec_unlock(device);
	return timeout ? UACPI_STATUS_HARDWARE_TIMEOUT : UACPI_STATUS_OK;
}

static uacpi_status ec_aml_handle_region(uacpi_region_op op, uacpi_handle op_data) {
	switch (op) {
	case UACPI_REGION_OP_ATTACH:
	case UACPI_REGION_OP_DETACH:
		return UACPI_STATUS_OK;
	case UACPI_REGION_OP_READ:
	case UACPI_REGION_OP_WRITE:
		return ec_aml_rw(op, op_data);
	default:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}
}

static uacpi_interrupt_ret ec_handle_event_irq(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u16 index) {
	(void)index;
	(void)node;
	
	struct ec_device* device = ctx;
	uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, &device->wq_item, ec_work, device);

	return UACPI_INTERRUPT_HANDLED;
}

static uacpi_status ec_enable(struct ec_device* device) {
	uacpi_status status = uacpi_install_address_space_handler(device->node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_aml_handle_region, device);
	if (uacpi_unlikely_error(status))
		return status;

	status = uacpi_install_gpe_handler(UACPI_NULL, device->gpe_index, UACPI_GPE_TRIGGERING_LEVEL, ec_handle_event_irq, device);
	if (uacpi_unlikely_error(status)) {
		bug(uacpi_uninstall_address_space_handler(device->node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_aml_handle_region) != UACPI_STATUS_OK);
		return status;
	}

	status = uacpi_enable_gpe(UACPI_NULL, device->gpe_index);
	if (uacpi_unlikely_error(status)) {
		bug(uacpi_uninstall_gpe_handler(UACPI_NULL, device->gpe_index, ec_handle_event_irq) != UACPI_STATUS_OK);
		bug(uacpi_uninstall_address_space_handler(device->node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_aml_handle_region) != UACPI_STATUS_OK);
		return status;
	}
	return UACPI_STATUS_OK;
}

static inline bool needs_global_lock(uacpi_namespace_node* node) {
	uacpi_u64 value;
	uacpi_status status = uacpi_eval_simple_integer(node, "_GLK", &value);
	return (status == UACPI_STATUS_OK) ? !!value : false;
}

struct ec_init_ctx {
	struct acpi_gas ctrl, data;
	bool need_ctrl, need_data;
};

/* Looks for the data and control IO addresses */
static uacpi_iteration_decision ec_resource_it(void* user, uacpi_resource* resource) {
	struct ec_init_ctx* ctx = user;
	struct acpi_gas* reg = ctx->need_data ? &ctx->data : &ctx->ctrl;

	switch (resource->type) {
	case UACPI_RESOURCE_TYPE_IO:
		reg->address = resource->io.minimum;
		reg->register_bit_width = resource->io.length * 8;
		break;
	case UACPI_RESOURCE_TYPE_FIXED_IO:
		reg->address = resource->fixed_io.address;
		reg->register_bit_width = resource->fixed_io.length * 8;
		break;
	default:
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	if (ctx->need_data)
		ctx->need_data = false;
	else
		ctx->need_ctrl = false;
	reg->access_size = 0;
	reg->register_bit_offset = 0;
	reg->address_space_id = UACPI_ADDRESS_SPACE_SYSTEM_IO;

	/* This function looks for need_data and need_ctrl in that order, so only check need_ctrl */
	return !ctx->need_ctrl ? UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

static bool ec_fixup_regs_and_check_sanity(struct ec_device* device) {
	bool ok = true;
	ec_lock(device);

	if (!ec_ok(device)) {
		struct acpi_gas tmp = device->data;
		device->data = device->ctrl;
		device->ctrl = tmp;
		if (unlikely(!ec_ok(device)))
			ok = false;
	}

	ec_unlock(device);
	return ok;
}

static LIST_HEAD_DEFINE(ec_device_list);
static struct ec_device* ecdt_device = NULL;

static int ec_init_from_namespace(uacpi_namespace_node* node, uacpi_namespace_node_info* info) {
	(void)info;

	struct ec_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->node = node;
	device->global_lock = needs_global_lock(node);
	mutex_init(&device->mtx);

	const uacpi_char* device_path = uacpi_namespace_node_generate_absolute_path(node);

	u16 gpe;
	uacpi_status status = acpi_eval_gpe(node, &gpe);
	if (status != UACPI_STATUS_OK)
		goto out;
	device->gpe_index = gpe;

	/* Now get the IO addresses for the EC */
	uacpi_resources* resources;
	status = uacpi_get_current_resources(node, &resources);
	if (status != UACPI_STATUS_OK)
		goto out;

	/* Now loop through the resources to find the control and data ports */
	struct ec_init_ctx init_ctx = { .need_ctrl = true, .need_data = true };
	status = uacpi_for_each_resource(resources, ec_resource_it, &init_ctx);
	uacpi_free_resources(resources);
	if (init_ctx.need_data || init_ctx.need_ctrl) {
		if (status == UACPI_STATUS_OK)
			status = UACPI_STATUS_NOT_FOUND;
		goto out;
	}
	device->ctrl = init_ctx.ctrl;
	device->data = init_ctx.data;

	if (ecdt_device && device->node == ecdt_device->node)
		goto out;

	/* namespace isn't guaranteed to have the IO addresses in order, so make sure the IO addresses aren't swapped */
	if (unlikely(!ec_fixup_regs_and_check_sanity(device))) {
		status = UACPI_STATUS_INTERNAL_ERROR; /* Becomes -EIO when acpi_uacpi_status_to_errno() converts it */
		goto out;
	}

	status = ec_enable(device);
	if (status != UACPI_STATUS_OK)
		goto out;

	list_add(&ec_device_list, &device->link);	
	printk("ec: Device %s at IO %lx,%lx GPE: %u, from namespace\n", device_path, device->data.address, device->ctrl.address, device->gpe_index);
	device = NULL;
out:
	uacpi_free_absolute_path(device_path);
	kfree(device);
	return acpi_uacpi_status_to_errno(status);
}

static const char* pnp_ids[] = {
	"PNP0C09",
	NULL
};

static struct acpi_driver ec_driver = {
	.name = "ec",
	.pnp_ids = pnp_ids,
	.probe = ec_init_from_namespace
};

static int ec_init_from_ecdt(const struct acpi_ecdt* ecdt) {
	struct ec_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	uacpi_namespace_node* ec_node;
	uacpi_status status = uacpi_namespace_node_find(UACPI_NULL, ecdt->ec_id, &ec_node);
	if (status != UACPI_STATUS_OK) {
		kfree(device);
		return -ENODEV;
	}

	device->node = ec_node;
	device->ctrl = ecdt->ec_control;
	device->data = ecdt->ec_data;
	device->global_lock = needs_global_lock(ec_node);
	mutex_init(&device->mtx);

	const uacpi_char* device_path = uacpi_namespace_node_generate_absolute_path(ec_node);

	u16 gpe;
	status = acpi_eval_gpe(ec_node, &gpe);
	if (status != UACPI_STATUS_OK)
		goto out;
	device->gpe_index = gpe;

	/* here we choose not to trust the firmware, make sure the ECDT gave us a working device */
	if (unlikely(!ec_fixup_regs_and_check_sanity(device))) {
		status = UACPI_STATUS_INTERNAL_ERROR; /* Becomes -EIO when acpi_uacpi_status_to_errno() converts it */
		goto out;
	}

	status = ec_enable(device);
	if (status != UACPI_STATUS_OK)
		goto out;

	ecdt_device = device;
	list_add(&ec_device_list, &device->link);
	printk("ec: Device %s at IO %lx,%lx GPE: %u, from ECDT\n", device_path, device->data.address, device->ctrl.address, device->gpe_index);
	device = NULL;
out:
	uacpi_free_absolute_path(device_path);
	kfree(device);
	return acpi_uacpi_status_to_errno(status);
}

static void acpi_ec_init(void) {
	struct uacpi_table tbl;
	if (uacpi_table_find_by_signature(ACPI_ECDT_SIGNATURE, &tbl) == UACPI_STATUS_OK) {
		int err = ec_init_from_ecdt(tbl.ptr);
		if (unlikely(err))
			printk(PRINTK_ERR "ec: Failed to initialize from ECDT: %d\n", err);
	}
	acpi_driver_register(&ec_driver);
}

INIT_TASK_DECLARE(acpi_init_task, heap_init_task);
INIT_TASK_DEFINE(acpi_ec_init_task, INIT_TASK_SCOPE_BSP, acpi_ec_init, &acpi_init_task, &heap_init_task);
