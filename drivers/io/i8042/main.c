#include <crater/module.h>
#include <crater/irq.h>
#include <crater/printk.h>
#include <crater/input.h>
#include <crater/cmdline.h>

#include <acpi/driver.h>
#include <uacpi/namespace.h>
#include <uacpi/resources.h>
#include <uacpi/io.h>

#include <x86_64/pmio.h>

#include "i8042.h"

#define I8042_KBD_PNP_ID "PNP0303"
#define I8042_DATA 0x60
#define I8042_CMD 0x64

struct i8042_initctx {
	int irq;
	bool found_data, found_command;
};

static uacpi_iteration_decision i8042_resource_it(void* user, uacpi_resource* resource) {
	struct i8042_initctx* ctx = user;
	if (resource->type == UACPI_RESOURCE_TYPE_IRQ) {
		if (resource->irq.num_irqs != 1)
			return UACPI_ITERATION_DECISION_CONTINUE;
		ctx->irq = resource->irq.irqs[0];
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	if (resource->type != UACPI_RESOURCE_TYPE_IO && resource->type != UACPI_RESOURCE_TYPE_FIXED_IO)
		return UACPI_ITERATION_DECISION_CONTINUE;

	/*
	 * Just make sure all the ports are in the namespace, verifying this makes it more safe to access,
	 * but because this is firmware, we cannot rely on this being true.
	 */
	uacpi_u16 base = resource->type == UACPI_RESOURCE_TYPE_IO ? resource->io.minimum : resource->fixed_io.address;

	/*
	 * These could theoretically be anything, but it's unknown if the ports are flipped around, or if it's just the wrong ports.
	 * So check them against known common values.
	 */
	if (base == I8042_IO_DATA)
		ctx->found_data = true;
	else if (base == I8042_IO_COMMAND)
		ctx->found_command = true;

	return UACPI_ITERATION_DECISION_CONTINUE;
}

static int i8042_get_resources(struct uacpi_namespace_node* node, const uacpi_char* device_path, struct i8042_initctx* out_ctx) {
	*out_ctx = (struct i8042_initctx){ .found_command = false, .found_data = false, .irq = -1 };

	uacpi_resources* resources;
	uacpi_status status = uacpi_get_current_resources(node, &resources);
	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_ERR "i8042: %s has no resources\n", device_path);
		return -ENODEV;
	}
	uacpi_for_each_resource(resources, i8042_resource_it, out_ctx);
	uacpi_free_resources(resources);

	if (!out_ctx->found_command || !out_ctx->found_data || out_ctx->irq == -1) {
		printk(PRINTK_ERR "i8042: %s doesn't have all resources to initialize properly\n", device_path);
		return -ENODEV;
	}
	if (unlikely(out_ctx->irq != 1 && out_ctx->irq != 12))
		printk(PRINTK_WARN "i8042: %s has an unusual IRQ number %d\n", device_path, out_ctx->irq);

	return 0;
}

static struct {
	bool p1_available, p2_available;
	bool initialized, failed;
} i8042_init_status = {
	.p1_available = false, .p2_available = false,
	.initialized = false, .failed = false
};

/*
 * Either the keyboard or mouse driver can do this test. Whichever driver init runs first.
 * Once this function has ran once, it does not need to be ran again. The ACPI driver loader
 * runs on a single thread.
 */
static int i8042_init(void) {
	if (i8042_init_status.failed)
		return -ENODEV;
	if (i8042_init_status.initialized)
		return 0;
	i8042_init_status.initialized = true;

	i8042_command_write(I8042_COMMAND_DISABLE_P1);
	i8042_command_write(I8042_COMMAND_DISABLE_P2);
	i8042_flush_outbuffer();

	i8042_command_write(I8042_COMMAND_GET_CFG);
	u8 config = i8042_data_read();
	config &= ~(I8042_CFG_P1_IRQ_ENABLED | I8042_CFG_P2_IRQ_ENABLED | I8042_CFG_TRANSLATION);
	i8042_command_write(I8042_COMMAND_SET_CFG);
	i8042_data_write(config);

	/* Test the controller itself, and then write the configuration back in case it was changed */
	i8042_command_write(I8042_COMMAND_SELFTEST);
	u8 result = i8042_data_read();
	if (result != I8042_SELFTEST_OK) {
		i8042_init_status.failed = true;
		printk(PRINTK_ERR "i8042: selftest failed! (expected 0, got %x)\n", result);
		return -ENODEV;
	}
	i8042_command_write(I8042_COMMAND_SET_CFG);
	i8042_data_write(config);
	i8042_flush_outbuffer();

	/* Now check for port 2 */
	i8042_command_write(I8042_COMMAND_ENABLE_P2);
	bool check_p2 = false;
	i8042_command_write(I8042_COMMAND_GET_CFG);
	config = i8042_data_read();
	if ((config & I8042_CFG_P2_CLOCK) == 0)
		check_p2 = true;
	i8042_command_write(I8042_COMMAND_DISABLE_P2);

	/* Now run the self test on both ports */
	i8042_command_write(I8042_COMMAND_SELFTEST_P1);
	result = i8042_data_read();
	if (result == 0)
		i8042_init_status.p1_available = true;
	else
		printk(PRINTK_ERR "i8042: port 1 self test failed (expected 0, got %x)\n", result);
	if (check_p2) {
		i8042_command_write(I8042_COMMAND_SELFTEST_P2);
		result = i8042_data_read();
		if (result == 0)
			i8042_init_status.p2_available = true;
		else
			printk(PRINTK_ERR "i8042: port 2 self test failed (expected 0, got %x)", result);
	}

	if (!i8042_init_status.p1_available && !i8042_init_status.p2_available) {
		printk(PRINTK_ERR "i8042: no working ports\n");
		i8042_init_status.failed = true;
		return -ENODEV;
	}

	return 0;
}

static bool check_namespace(void) {
	const char* cmdline = cmdline_get("i8042.useacpi");
	if (cmdline && (strcmp(cmdline, "n") == 0 || strcmp(cmdline, "0") == 0))
		return false;
	return true;
}

static int i8042_keyboard_init(uacpi_namespace_node* node, uacpi_namespace_node_info* info) {
	(void)info;

	struct keyboard* kbd = keyboard_create();
	if (!kbd)
		return -ENOMEM;
	struct i8042_keyboard* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device) {
		keyboard_destroy(kbd);
		return -ENOMEM;
	}
	*device = (struct i8042_keyboard){
		.irq = 1,
		.in_extended = false,
		.kbd = kbd
	};

	/* Some firmware doesn't expose the i8042 device in the ACPI namespace */
	int err;
	const uacpi_char* path = UACPI_NULL;
	if (check_namespace()) {
		path = uacpi_namespace_node_generate_absolute_path(node);
		struct i8042_initctx initctx;
		err = i8042_get_resources(node, path, &initctx);
		if (err)
			goto out;
		device->irq = initctx.irq;
	}

	err = i8042_init();
	if (err)
		goto out;
	if (unlikely(!i8042_init_status.p1_available))
		goto out;

	i8042_command_write(I8042_COMMAND_ENABLE_P1);
	i8042_disable_scanning(1);
	i8042_flush_outbuffer();

	if (i8042_port_reset_selftest(1) == 0)
		i8042_enable_scanning(1);

	err = i8042_setup_keyboard_irq(device);
	if (err)
		goto out;
	i8042_command_write(I8042_COMMAND_GET_CFG);
	u8 conf = i8042_data_read();
	conf |= I8042_CFG_P1_IRQ_ENABLED | I8042_CFG_TRANSLATION;
	i8042_command_write(I8042_COMMAND_SET_CFG);
	i8042_data_write(conf);

	printk("i8042: Device %s at IO %x,%x IRQ %u\n", path, I8042_IO_DATA, I8042_IO_COMMAND, device->irq);
out:
	uacpi_free_absolute_path(path);
	if (err) {
		keyboard_destroy(kbd);
		kfree(device);
	}
	return err;
}

static const char* pnp_ids[] = {
	I8042_KBD_PNP_ID,
	NULL
};

static struct acpi_driver i8042_kbd_driver = {
	.name = "i8042_kbd",
	.probe = i8042_keyboard_init,
	.pnp_ids = pnp_ids
};

static int i8042_module_init(void) {
	acpi_driver_register(&i8042_kbd_driver);
	return 0;
}

INIT_TASK_DECLARE(acpi_tables_init_task, irq_init_task, heap_init_task);
MODULE("input_i8042", i8042_module_init, NULL, &acpi_tables_init_task, &irq_init_task, &heap_init_task);
