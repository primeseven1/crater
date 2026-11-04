#include <crater/printk.h>
#include <crater/string.h>
#include <crater/list.h>

#include <acpi/driver.h>
#include <uacpi/acpi.h>

static LIST_HEAD_DEFINE(acpi_driver_list);

void acpi_driver_register(struct acpi_driver* driver) {
	list_node_init(&driver->link);
	list_add(&acpi_driver_list, &driver->link);
}

static struct acpi_driver* find_by_hid(const uacpi_id_string* hid) {
	struct acpi_driver* driver;
	list_for_each_entry(driver, &acpi_driver_list, link) {
		const char* const* ids = driver->pnp_ids;
		while (*ids) {
			if (strcmp(*ids, hid->value) == 0)
				return driver;
			ids++;
		}
	}
	return NULL;
}

static struct acpi_driver* find_by_cid(const uacpi_pnp_id_list* cid) {
	struct acpi_driver* driver;
	list_for_each_entry(driver, &acpi_driver_list, link) {
		const char* const* ids = driver->pnp_ids;
		while (*ids) {
			for (uacpi_u32 i = 0; i < cid->num_ids; i++) {
				if (strcmp(*ids, cid->ids[i].value) == 0)
					return driver;
			}
			ids++;
		}
	}
	return NULL;
}

/* https://osdev.wiki/wiki/UACPI */
static uacpi_iteration_decision init_device(void* ctx, uacpi_namespace_node* node, uacpi_u32 depth) {
	(void)depth;
	(void)ctx;

	uacpi_namespace_node_info* info;
	uacpi_status status = uacpi_get_namespace_node_info(node, &info);
	if (uacpi_unlikely_error(status)) {
		const uacpi_char* path = uacpi_namespace_node_generate_absolute_path(node);
		printk(PRINTK_ERR "acpi: Failed to get information for node %s: %s",
				path, uacpi_status_to_string(status));
		uacpi_free_absolute_path(path);
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	struct acpi_driver* driver = NULL;
	if (info->flags & UACPI_NS_NODE_INFO_HAS_HID)
		driver = find_by_hid(&info->hid);
	if (!driver && (info->flags & UACPI_NS_NODE_INFO_HAS_CID))
		driver = find_by_cid(&info->cid);
	if (driver)
		driver->probe(node, info);

	uacpi_free_namespace_node_info(info);
	return UACPI_ITERATION_DECISION_CONTINUE;
}

void acpi_drivers_load(void) {
	uacpi_status status = uacpi_namespace_for_each_child(uacpi_namespace_root(), init_device,
			UACPI_NULL, UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, UACPI_NULL);
	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_CRIT "acpi: uacpi_namespace_for_each_child() failed in acpi_drivers_load(): %s",
				uacpi_status_to_string(status));
	}
}
