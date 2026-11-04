#include <crater/init.h>
#include <crater/slab.h>
#include <crater/printk.h>
#include <crater/cmdline.h>
#include <crater/convert.h>

#include <uacpi/uacpi.h>
#include <uacpi/context.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>

#include "internal.h"

#if defined(CONFIG_DRIVER_ACPI_ENABLE_TABLES)

static uacpi_log_level get_loglevel_from_cmdline(void) {
	const char* acpi_log_level = cmdline_get("acpi.loglevel");
	if (!acpi_log_level)
		return UACPI_LOG_ERROR;

	unsigned long long level;
	if (kstrtoull(acpi_log_level, 0, &level) == 0) {
		switch (level) {
		case UACPI_LOG_DEBUG:
		case UACPI_LOG_TRACE:
		case UACPI_LOG_INFO:
		case UACPI_LOG_WARN:
		case UACPI_LOG_ERROR:
			return level;
		}
		printk(PRINTK_ERR "acpi: Invalid loglevel %llu, defaulting to ERROR\n", level);
		return UACPI_LOG_ERROR;
	}

	static const struct {
		const char* string;
		uacpi_log_level level;
	} level_strings[] = {
		{ .string = "DEBUG", .level = UACPI_LOG_DEBUG }, { .string = "TRACE", .level = UACPI_LOG_TRACE },
		{ .string = "INFO", .level = UACPI_LOG_INFO }, { .string = "WARN", .level = UACPI_LOG_WARN },
		{ .string = "ERROR", .level = UACPI_LOG_ERROR }
	};
	for (size_t i = 0; i < ARRAY_SIZE(level_strings); i++) {
		if (strcmp(level_strings[i].string, acpi_log_level) == 0)
			return level_strings[i].level;
	}
	printk(PRINTK_ERR "acpi: Invalid loglevel %s, defaulting to ERROR\n", acpi_log_level);
	return UACPI_LOG_ERROR;
}

#endif /* defined(CONFIG_DRIVER_ACPI_ENABLE_TABLES) */

static void acpi_tables_init(void) {
#ifdef CONFIG_DRIVER_ACPI_ENABLE_TABLES
	uacpi_context_set_log_level(get_loglevel_from_cmdline());
	uacpi_context_set_proactive_table_checksum(UACPI_TRUE);
	uacpi_status status = uacpi_initialize(0);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: uacpi_initialize(0) failed: %s", uacpi_status_to_string(status));
#endif /* CONFIG_DRIVER_ACPI_ENABLE_TABLES */
}

INIT_TASK_DECLARE(heap_init_task, hhdm_init_task, vmm_init_task, cmdline_init_task);
INIT_TASK_DEFINE(acpi_tables_init_task, INIT_TASK_SCOPE_BSP, acpi_tables_init,
		&heap_init_task, &hhdm_init_task, &vmm_init_task, &cmdline_init_task);

static void acpi_init(void) {
#ifdef CONFIG_DRIVER_ACPI_ENABLE_TABLES
	acpi_glue_init();
#endif /* CONFIG_DRIVER_ACPI_ENABLE_TABLES */
#ifdef CONFIG_DRIVER_ACPI_ENABLE_AML
	uacpi_status status = uacpi_namespace_load();
	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_ERR "acpi: uacpi_namespace_load() failed: %s", uacpi_status_to_string(status));
		return;
	}
	status = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_ERR "acpi: uacpi_set_interrupt_model() failed: %s", uacpi_status_to_string(status));
		return;
	}
	status = uacpi_namespace_initialize();
	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_ERR "acpi: uacpi_namespace_initialize() failed: %s", uacpi_status_to_string(status));
		return;
	}

	uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, acpi_pwrbtn_fixed_event, UACPI_NULL);
	uacpi_finalize_gpe_initialization();
#endif /* CONFIG_DRIVER_ACPI_ENABLE_AML */
}

INIT_TASK_DECLARE(sched_init_task, workqueue_init_task, irq_init_task);
INIT_TASK_DEFINE(acpi_init_task, INIT_TASK_SCOPE_BSP, acpi_init,
		&acpi_tables_init_task, &irq_init_task, &sched_init_task, &workqueue_init_task);
