#include <crater/atomic.h>
#include <crater/timekeeper.h>
#include <crater/printk.h>
#include <crater/panic.h>
#include <crater/sched.h>
#include <crater/irq.h>

#include <arch/processor.h>

#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/sleep.h>
#include <acpi/sleep.h>

#include "internal.h"

#if defined(CONFIG_DRIVER_ACPI_ENABLE_TABLES) || defined(CONFIG_DRIVER_ACPI_ENABLE_AML)
static bool __atomic poweroff_status = atomic_init(false);
#endif /* defined(CONFIG_DRIVER_ACPI_ENABLE_TABLES) || defined(CONFIG_DRIVER_ACPI_ENABLE_AML) */

#ifdef CONFIG_DRIVER_ACPI_ENABLE_AML

static void acpi_poweroff_work(uacpi_handle ctx) {
	(void)ctx;
	uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);

	local_irq_disable();
	smp_send_stop();
	printk_disable_ringbuffer_and_flush();

	if (uacpi_unlikely_error(status))
		goto die;

	printk(PRINTK_CRIT "acpi: powering off...\n");
	udelay(1000000);
	status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: Cannot enter sleep state S5: %s\n", uacpi_status_to_string(status));

die:
	printk(PRINTK_EMERG "acpi: You have to turn your computer off manually.\n");
	while (1)
		arch_cpu_idle();
}

uacpi_interrupt_ret acpi_pwrbtn_fixed_event(uacpi_handle ctx) {
	if (atomic_exchange(&poweroff_status, true))
		return UACPI_INTERRUPT_HANDLED;

	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_poweroff_work, ctx);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_CRIT "acpi: poweroff event failed!\n");

	return UACPI_INTERRUPT_HANDLED;
}

_Noreturn void acpi_poweroff(void) {
	while (atomic_exchange(&poweroff_status, true))
		schedule();

	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_poweroff_work, NULL);
	if (uacpi_likely_success(status))
		uacpi_kernel_wait_for_work_completion();

	panic("poweroff");
}

#endif /* CONFIG_DRIVER_ACPI_ENABLE_AML */

#ifdef CONFIG_DRIVER_ACPI_ENABLE_TABLES

static void acpi_reboot_work(uacpi_handle ctx) {
	(void)ctx;

	local_irq_disable();
	smp_send_stop();
	printk_disable_ringbuffer_and_flush();

	printk(PRINTK_CRIT "acpi: rebooting...\n");
	udelay(1000000);
	uacpi_status status = uacpi_reboot();
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: Cannot reboot: %s\n", uacpi_status_to_string(status));

	printk(PRINTK_EMERG "acpi: You have to reboot manually.\n");
	while (1)
		arch_cpu_idle();
}

_Noreturn void acpi_reboot(void) {
	while (atomic_exchange(&poweroff_status, true))
		schedule();

	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_reboot_work, NULL);
	if (uacpi_likely_success(status))
		uacpi_kernel_wait_for_work_completion();

	panic("reboot");
}

#endif /* CONFIG_DRIVER_ACPI_ENABLE_TABLES */
