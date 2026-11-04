#pragma once

#include <uacpi/context.h>

int acpi_glue_init(void);
uacpi_interrupt_ret acpi_pwrbtn_fixed_event(uacpi_handle ctx);
