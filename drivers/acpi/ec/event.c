#include <crater/printk.h>
#include <crater/string.h>

#include <acpi/sleep.h>
#include <acpi/glue.h>
#include <uacpi/event.h>
#include <uacpi/utilities.h>
#include <uacpi/notify.h>

#include "ec.h"

#define PWRBTN_PNP_ID "PNP0C0C"
#define AC_PNP_ID "ACPI0003"
#define AC_USBC_PNP_ID "PNP0CA0"

static struct ec_device* ec_device;

static uacpi_status pwrbtn_notify(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u64 value) {
	(void)ctx;
	(void)node;

	if (value == 0x80)
		acpi_poweroff();
	else
		printk(PRINTK_WARN "ec: Ignoring unkown value %lx from power button device %s\n", value, PWRBTN_PNP_ID);

	return UACPI_STATUS_OK;
}

/* prevent a gpe storm when plugging in a power cable :D */
static void psr(uacpi_handle handle) {
	struct uacpi_namespace_node* node = handle;
	uacpi_status status = uacpi_eval(node, "_PSR", NULL, NULL);
	if (unlikely(status != UACPI_STATUS_OK)) {
		printk(PRINTK_CRIT "ec: _PSR eval failed, masking GPE\n");
		uacpi_mask_gpe(NULL, ec_device->gpe_index);
	}
}

static uacpi_status ac_notify(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u64 value) {
	(void)ctx;
	if (value == 0x80) {
		uacpi_status s = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, psr, node);
		if (uacpi_likely(s == UACPI_STATUS_OK))
			acpi_wait_for_gpe_work_completion();
	}
	return UACPI_STATUS_OK;
}

static uacpi_iteration_decision install_notify(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
	(void)depth;
	uacpi_status (*handler)(uacpi_handle, uacpi_namespace_node*, uacpi_u64) = user;
	return uacpi_install_notify_handler(node, handler, NULL) == UACPI_STATUS_OK ?
		UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

void ec_install_events(struct ec_device* device) {
	ec_device = device;
	uacpi_find_devices(PWRBTN_PNP_ID, install_notify, pwrbtn_notify);
	uacpi_find_devices(AC_PNP_ID, install_notify, ac_notify);
	uacpi_find_devices(AC_USBC_PNP_ID, install_notify, ac_notify);
}
