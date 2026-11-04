ifeq ($(CONFIG_ARCH_X86_64_RETPOLINE), y)
ifeq ($(CONFIG_LLVM), y)
CFLAGS += -mretpoline -mretpoline-external-thunk
else
CFLAGS += -mindirect-branch=thunk-extern -mindirect-branch-register
endif
endif

S_SOURCE_FILES += $(shell find ./arch/x86_64 -type f -name '*.S')
C_SOURCE_FILES += $(shell find ./arch/x86_64 -type f -name '*.c')
