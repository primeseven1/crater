#include <crater/printk.h>
#include <crater/panic.h>
#include <uacpi/event.h>
#include <uacpi/opregion.h>
#include "ec.h"

static u8 get_event(struct ec_device* device) {
	ec_lock(device);

	u8 index;
	int err = ec_get_event(device, &index);

	ec_unlock(device);

	if (err) {
		printk(PRINTK_ERR "ec: ec_get_event error: %d\n", err);
		return 0;
	}
	return index;
}

static void handle_ec_query(uacpi_handle handle) {
	struct ec_device* device = handle;

	while (1) {
		u8 event = get_event(device);
		if (event == 0)
			break;
		const char* hex_chars = "0123456789ABCDEF";
		char method_name[5] = { '_', 'Q', hex_chars[(event >> 4) & 0xF], hex_chars[event & 0xF], '\0' };
		uacpi_status status = uacpi_eval(device->node, method_name, UACPI_NULL, UACPI_NULL);
		if (uacpi_unlikely_error(status))
			printk(PRINTK_ERR "ec: uacpi_eval() failed executing method %s: %s\n", method_name, uacpi_status_to_string(status));
	}

	atomic_store(&device->handling_events, false);
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
	
	uacpi_interrupt_ret ret = UACPI_INTERRUPT_HANDLED;

	struct ec_device* device = ctx;
	if (atomic_exchange(&device->handling_events, true) == false)
		uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, NULL, handle_ec_query, device);

	return ret;
}

void ec_install_handlers(struct ec_device* device) {
	bug(uacpi_install_address_space_handler(device->node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_aml_handle_region, device) != UACPI_STATUS_OK);
	bug(uacpi_install_gpe_handler(NULL, device->gpe_index, UACPI_GPE_TRIGGERING_LEVEL, ec_handle_event_irq, device) != UACPI_STATUS_OK);
}
