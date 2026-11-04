-include .config

ARCH := $(patsubst "%",%,$(CONFIG_ARCH))
VERSION_HEADER := ./include/generated/version.h

ASMFLAGS = -c -MMD -MP -I./include -I./arch/$(ARCH)/include \
	   -include ./include/generated/autoconf.h -include $(VERSION_HEADER)
CFLAGS = -c -MMD -MP -std=c11 -I./include -I./arch/$(ARCH)/include \
	 -include ./include/generated/autoconf.h -include $(VERSION_HEADER) \
	 -ffreestanding -fno-stack-protector -fno-omit-frame-pointer \
	 -fno-optimize-sibling-calls -fno-exceptions -fno-asynchronous-unwind-tables \
	 -Wall -Wextra -Wshadow -Wpointer-arith \
	 -Winline -Wimplicit-fallthrough -Wvla -Walloca \
	 -Wstrict-prototypes -Wmissing-prototypes \
	 -mno-red-zone -mgeneral-regs-only \
	 -O$(CONFIG_OPTIMIZATION)
SPARSE_CFLAGS = -D__BIGGEST_ALIGNMENT__=16 -D__clang_major__ -D__clang_minor__ -D__clang_patchlevel__ # Sparse will see clang version 1.1.1
LDFLAGS = -static -nostdlib --no-dynamic-linker \
	  -ztext -zmax-page-size=0x1000 \
	  -O$(CONFIG_OPTIMIZATION)

ifeq ($(CONFIG_LLVM), y)
CC := clang
LD := ld.lld
CC_MIN_MAJOR := 14
CC_MIN_MINOR := 0
LD_MIN_MAJOR := $(CC_MIN_MAJOR)
LD_MIN_MINOR := $(CC_MIN_MINOR)
ASMFLAGS += --target=$(ARCH)-unknown-none-elf
CFLAGS += -fcolor-diagnostics --target=$(ARCH)-unknown-none-elf
# TODO: Figure out compiler-rt
else
CC := $(ARCH)-elf-gcc
LD := $(ARCH)-elf-ld
CC_MIN_MAJOR := 12
CC_MIN_MINOR := 2
LD_MIN_MAJOR := 2
LD_MIN_MINOR := 39
RTLIB_DIR := $(shell dirname $(shell $(CC) $(CFLAGS) -print-libgcc-file-name))
RTLIB_FLAG := -lgcc
LDFLAGS += -L$(RTLIB_DIR)
endif

ifeq ($(CONFIG_DEBUG), y)
ASMFLAGS += -g
CFLAGS += -g
endif

ifeq ($(CONFIG_KASLR), y)
CFLAGS += -fPIE
LDFLAGS += -pie
else
CFLAGS += -mcmodel=kernel
LDFLAGS += -no-pie
endif

ifeq ($(CONFIG_UBSAN), y)
CFLAGS += -fsanitize=undefined
endif

OUTPUT := ./crater
ifeq ($(CONFIG_KASLR), y)
LDSCRIPT := ./arch/$(ARCH)/kaslr.ld
else
LDSCRIPT := ./arch/$(ARCH)/nokaslr.ld
endif

.PHONY: help all version menuconfig clean

all: version
	$(MAKE) $(OUTPUT)

help:
	@echo "Available targets:"
	@echo "    make menuconfig - Configure the project"
	@echo "    make all        - Build the project"

version:
	@scripts/cc-ver.sh $(CC) $(CC_MIN_MAJOR) $(CC_MIN_MINOR)
	@scripts/ld-ver.sh $(LD) $(LD_MIN_MAJOR) $(LD_MIN_MINOR)
	@scripts/kern-ver.sh $(VERSION_HEADER) 0 1

BUILD_MAKEFILES = $(shell find ./arch/$(ARCH) ./kernel ./drivers ./fs -type f -name 'Makefile')
-include $(BUILD_MAKEFILES)

S_OBJECT_FILES := $(patsubst %.S, %.o, $(S_SOURCE_FILES))
C_OBJECT_FILES := $(patsubst %.c, %.o, $(C_SOURCE_FILES))
H_DEPENDENCIES := $(patsubst %.o, %.d, $(S_OBJECT_FILES) $(C_OBJECT_FILES))
-include $(H_DEPENDENCIES)

menuconfig:
	@scripts/mconf.sh

$(OUTPUT): $(S_OBJECT_FILES) $(C_OBJECT_FILES) $(LDSCRIPT)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) $(S_OBJECT_FILES) $(C_OBJECT_FILES) -T$(LDSCRIPT) -o $(OUTPUT) $(RTLIB_FLAG)

%.o: %.S
	@echo "[AS] $@"
	@$(CC) $(ASMFLAGS) $< -o $@

%.o: %.c
	@echo "[CC] $@"
	@sparse $(CFLAGS) $(SPARSE_CFLAGS) $<
	@$(CC) $(CFLAGS) $< -o $@

clean:
	@echo "[CLEAN] ."
	@rm -f $(S_OBJECT_FILES) $(C_OBJECT_FILES) $(H_DEPENDENCIES) $(OUTPUT)
