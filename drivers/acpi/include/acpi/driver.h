#pragma once

#include <uacpi/namespace.h>
#include <uacpi/utilities.h>
#include <crater/list.h>

struct acpi_driver {
	const char* name;
	int (*probe)(uacpi_namespace_node*, uacpi_namespace_node_info*);
	const char* const* pnp_ids;
	struct list_node link;
};

void acpi_driver_register(struct acpi_driver* driver);
void acpi_drivers_load(void);
