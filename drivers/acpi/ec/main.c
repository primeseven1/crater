#include <crater/module.h>
#include <crater/slab.h>
#include <crater/printk.h>

#include <acpi/driver.h>
#include <uacpi/namespace.h>
#include <uacpi/resources.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>
#include <uacpi/tables.h>

#include "ec.h"

static const char* pnp_ids[] = {
	"PNP0C09",
	NULL
};

struct ec_init_ctx {
	struct acpi_gas ctrl, data;
	bool need_ctrl, need_data;
};

static inline bool needs_global_lock(uacpi_namespace_node* node) {
	uacpi_u64 value;
	uacpi_status status = uacpi_eval_simple_integer(node, "_GLK", &value);
	return (status == UACPI_STATUS_OK) ? !!value : false;
}

static inline i32 get_gpe(uacpi_namespace_node* node) {
	uacpi_u64 value;
	uacpi_status status = uacpi_eval_simple_integer(node, "_GPE", &value);
	if (status != UACPI_STATUS_OK)
		return -ENODEV;
	return value;
}

/* Looks for the data and control IO addresses */
static uacpi_iteration_decision ec_resource_it(void* user, uacpi_resource* resource) {
	struct ec_init_ctx* ctx = user;
	struct acpi_gas* reg = ctx->need_data ? &ctx->data : &ctx->ctrl;
	uacpi_bool* need = ctx->need_data ? &ctx->need_data : &ctx->need_ctrl;

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

	*need = false;
	reg->access_size = 0;
	reg->register_bit_offset = 0;
	reg->address_space_id = UACPI_ADDRESS_SPACE_SYSTEM_IO;

	/* This function looks for need_data and need_ctrl in that order, so only check need_ctrl */
	return !ctx->need_ctrl ? UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

static inline void ec_enable(struct ec_device* device) {
	ec_install_handlers(device);
	ec_install_events(device);
	bug(uacpi_enable_gpe(UACPI_NULL, device->gpe_index) != UACPI_STATUS_OK);
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

static int ec_init_from_namespace(uacpi_namespace_node* node, uacpi_namespace_node_info* info) {
	(void)info;

	/* Sometimes the EC is described multiple times, for some reason */
	static bool found_real = false;
	if (found_real)
		return 0;

	struct ec_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->node = node;
	atomic_store(&device->handling_events, false);
	device->global_lock = needs_global_lock(node);
	mutex_init(&device->mtx);

	int err = -ENODEV;
	const uacpi_char* device_path = uacpi_namespace_node_generate_absolute_path(node);

	i32 gpe = get_gpe(node);
	if (gpe < 0) {
		printk("ec: %s has no GPE\n", device_path);
		goto out;
	}
	device->gpe_index = (u16)gpe;

	/* Now get the IO addresses for the EC */
	uacpi_resources* resources;
	uacpi_status status = uacpi_get_current_resources(node, &resources);
	if (status != UACPI_STATUS_OK) {
		printk(PRINTK_ERR "ec: %s has no resources\n", device_path);
		goto out;
	}

	/* Now loop through the resources to find the control and data ports */
	struct ec_init_ctx init_ctx = { .need_ctrl = true, .need_data = true };
	status = uacpi_for_each_resource(resources, ec_resource_it, &init_ctx);
	uacpi_free_resources(resources);
	if (init_ctx.need_data || init_ctx.need_ctrl || status != UACPI_STATUS_OK) {
		printk(PRINTK_ERR "ec: %s doesn't have all ports\n", device_path);
		goto out;
	}
	device->ctrl = init_ctx.ctrl;
	device->data = init_ctx.data;

	/* namespace isn't guarunteed to have the IO addresses in order, so make sure the IO addresses aren't swapped */
	if (unlikely(!ec_fixup_regs_and_check_sanity(device)))
		goto out;

	found_real = true;
	ec_enable(device);
	printk("ec: Device %s at IO %lx,%lx GPE: %u, from namespace\n", device_path, device->data.address, device->ctrl.address, device->gpe_index);
	err = 0;
out:
	uacpi_free_absolute_path(device_path);
	if (err)
		kfree(device);
	return err;
}

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
	atomic_store(&device->handling_events, false);
	device->ctrl = ecdt->ec_control;
	device->data = ecdt->ec_data;
	device->global_lock = needs_global_lock(ec_node);
	mutex_init(&device->mtx);

	int err = -ENODEV;
	const uacpi_char* device_path = uacpi_namespace_node_generate_absolute_path(ec_node);

	i32 gpe = get_gpe(ec_node);
	if (gpe < 0) {
		printk(PRINTK_ERR "ec: %s has no GPE\n", device_path);
		goto out;
	}
	device->gpe_index = (u16)gpe;

	/* here we choose not to trust the firmware, make sure they aren't swapped */
	if (unlikely(!ec_fixup_regs_and_check_sanity(device)))
		goto out;

	ec_enable(device);
	printk("ec: Device %s at IO %lx,%lx GPE: %u, from ECDT\n", device_path, device->data.address, device->ctrl.address, device->gpe_index);
	err = 0;
out:
	uacpi_free_absolute_path(device_path);
	if (err)
		kfree(device);
	return err;
}

static int ec_module_init(void) {
	uacpi_table tbl;
	if (uacpi_table_find_by_signature(ACPI_ECDT_SIGNATURE, &tbl) == UACPI_STATUS_OK)
		return ec_init_from_ecdt(tbl.ptr);

	/* If there is no ECDT, then the namespace must be used */
	acpi_driver_register(&ec_driver);
	return 0;
}

INIT_TASK_DECLARE(acpi_tables_init_task, acpi_init_task, heap_init_task);
MODULE("acpi_ec", ec_module_init, NULL, &acpi_tables_init_task, &acpi_init_task, &heap_init_task);
