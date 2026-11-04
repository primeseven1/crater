#include <crater/init.h>
#include <crater/printk.h>

#include <arch/usercopy.h>
#include <x86_64/asm/cpuid.h>
#include <x86_64/asm/ctl.h>

#include "internal.h"

struct ld_extable_entry {
	i32 fault_rip_relative, fixup_rip_relative;
} __attribute__((packed));

extern const struct ld_extable_entry _ld_arch_x86_64_kernel_extable_start[];
extern const struct ld_extable_entry _ld_arch_x86_64_kernel_extable_end[];

bool usercopy_context_fixup_fault(struct arch_context* context) {
	const uintptr_t fault_rip = context->rip;
	const size_t extable_entry_count = _ld_arch_x86_64_kernel_extable_end - _ld_arch_x86_64_kernel_extable_start;
	for (size_t i = 0; i < extable_entry_count; i++) {
		const struct ld_extable_entry* entry = &_ld_arch_x86_64_kernel_extable_start[i];
		uintptr_t fault = (uintptr_t)entry + offsetof(struct ld_extable_entry, fault_rip_relative) + entry->fault_rip_relative;
		uintptr_t fixup = (uintptr_t)entry + offsetof(struct ld_extable_entry, fixup_rip_relative) + entry->fixup_rip_relative;
		if (fault_rip == fault) {
			context->rip = fixup;
			return true;
		}
	}
	return false;
}

static bool smep, smap;

void arch_usercopy_enter(void) {
	if (smap)
		__asm__ volatile("stac" : : : "cc", "memory");
}

void arch_usercopy_exit(void) {
	if (smap)
		__asm__ volatile("clac" : : : "cc", "memory");
}

static void sm_protections_init(void) {
	u32 eax, _unused;
	arch_x86_64_cpuid(0x00, 0, &eax, &_unused, &_unused, &_unused);
	if (eax < 0x07)
		return;
	u32 ebx;
	arch_x86_64_cpuid(0x07, 0, &_unused, &ebx, &_unused, &_unused);

	unsigned long or = 0;
	smep = !!(ebx & (1 << 7));
	if (smep)
		or |= ARCH_X86_64_CTL4_SMEP;
	smap = !!(ebx & (1 << 20));
	if (smap)
		or |= ARCH_X86_64_CTL4_SMAP;

	if (or)
		arch_x86_64_ctl4_write(arch_x86_64_ctl4_read() | or);
}

static void sm_protections_ap_init(void) {
	const unsigned long or = (smep ? ARCH_X86_64_CTL4_SMEP : 0) | (smap ? ARCH_X86_64_CTL4_SMAP : 0);
	if (or)
		arch_x86_64_ctl4_write(arch_x86_64_ctl4_read() | or);
}

INIT_TASK_DEFINE(arch_x86_64_sm_protections_init_task, INIT_TASK_SCOPE_BSP, sm_protections_init);
INIT_TASK_DEFINE(arch_x86_64_sm_protections_ap_init_task, INIT_TASK_SCOPE_AP, sm_protections_ap_init);
