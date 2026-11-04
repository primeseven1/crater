#pragma once

#ifdef CONFIG_DRIVER_ACPI_ENABLE_AML
_Noreturn void acpi_poweroff(void);
#endif /* CONFIG_DRIVER_ACPI_ENABLE_AML */
#ifdef CONFIG_DRIVER_ACPI_ENABLE_TABLES
_Noreturn void acpi_reboot(void);
#endif /* CONFIG_DRIVER_ACPI_ENABLE_TABLES */
