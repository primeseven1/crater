C_SOURCE_FILES += $(shell find ./drivers/acpi/uacpi/source ./drivers/acpi/base ./drivers/acpi/x86_64 -type f -name '*.c')
CFLAGS += -DUACPI_NATIVE_ALLOC_ZEROED -DUACPI_OVERRIDE_TYPES \
	  -I./drivers/acpi/uacpi/include -I./drivers/acpi/include/acpi -I./drivers/acpi/include/
ifeq ($(CONFIG_DRIVER_ACPI_ENABLE_EC), y)
C_SOURCE_FILES += $(shell find ./drivers/acpi/ec -type f -name '*.c')
endif
