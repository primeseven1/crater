C_SOURCE_FILES += $(shell find ./drivers/acpi/uacpi/source -type f -name '*.c')
C_SOURCE_FILES += $(shell find ./drivers/acpi/$(ARCH) -type f -name '*.c')
C_SOURCE_FILES += ./drivers/acpi/device.c ./drivers/acpi/driver.c ./drivers/acpi/glue.c ./drivers/acpi/init.c
CFLAGS += -DUACPI_NATIVE_ALLOC_ZEROED -DUACPI_OVERRIDE_TYPES \
	  -I./drivers/acpi/uacpi/include -I./drivers/acpi/include/acpi -I./drivers/acpi/include/
ifeq ($(CONFIG_DRIVER_ACPI_ENABLE_EC), y)
C_SOURCE_FILES += ./drivers/acpi/ec.c
endif
