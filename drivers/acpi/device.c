#include <crater/device.h>
#include <crater/vfs.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/irq.h>
#include <crater/smp.h>
#include <crater/workqueue.h>

#include <uacpi/sleep.h>
#include <acpi/glue.h>

static void poweroff_work(uacpi_handle ctx) {
	(void)ctx;
	uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);

	local_irq_disable();
	smp_send_stop();
	printk_disable_ringbuffer_and_flush();

	if (uacpi_unlikely_error(status))
		goto die;

	status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: Cannot enter sleep state S5: %s\n", uacpi_status_to_string(status));

die:
	printk(PRINTK_EMERG "acpi: You have to turn your computer off manually.\n");
	while (1)
		arch_cpu_idle();
}

static void reboot_work(uacpi_handle ctx) {
	(void)ctx;

	local_irq_disable();
	smp_send_stop();
	printk_disable_ringbuffer_and_flush();

	uacpi_status status = uacpi_reboot();
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: Cannot reboot: %s\n", uacpi_status_to_string(status));

	printk(PRINTK_EMERG "acpi: You have to reboot manually.\n");
	while (1)
		arch_cpu_idle();
}

static int acpi_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;

	char cmd;
	int err = uio_move(&cmd, sizeof(cmd), uio);
	if (err)
		return err;

	struct workqueue_item wq_item;
	switch (cmd) {
	case 'p':
		uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, &wq_item, poweroff_work, NULL);
		acpi_wait_for_gpe_work_completion();
		bug("Unreachable");
	case 'r':
		uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, &wq_item, reboot_work, NULL);
		acpi_wait_for_gpe_work_completion();
		bug("Unreachable");
	}

	return 0;
}

static void acpi_inactive(struct chrdev* chrdev) {
	(void)chrdev;
	bug("Unreachable");
}

static const struct chrdev_ops acpi_chrdev_ops = {
	.open = NULL,
	.close = NULL,
	.read = NULL,
	.write = acpi_write,
	.ioctl = NULL,
	.inactive = acpi_inactive
};

static struct chrdev acpi_chrdev = {
	.major = 2,
	.minor = 0,
	.ops = &acpi_chrdev_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

#ifdef CONFIG_DRIVER_ACPI_ENABLE_AML

static void acpi_device_init(void) {
	int err = chrdev_register(&acpi_chrdev, "acpi", 0600);
	if (err)
		printk(PRINTK_ERR "acpi: Failed to register ACPI device\n");
}

INIT_TASK_DECLARE(acpi_init_task);
INIT_TASK_DEFINE(acpi_device_init_task, INIT_TASK_SCOPE_BSP, acpi_device_init, &acpi_init_task);


#endif /* CONFIG_DRIVER_ACPI_ENABLE_AML */
