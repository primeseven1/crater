#pragma once

#include <crater/limine.h>
#include <x86_64/asm/segment.h>
#include <x86_64/asm/offsets.h>

struct arch_cpu {
	struct arch_cpu* self;
	struct thread* __current_thread; /* For a system call, this is an easy way to get the current thread pointer, NULL until the first context switch */
	u64 __syscall_number; /* On syscall entry, no registers can be used, so this is a place to store it */
	u32 acpi_id, lapic_id;
	struct arch_x86_64_gdt gdt;
	struct arch_x86_64_tss tss;
	struct {
		void* handle;
		u32 ticks_per_1ms;
	} lapic_timer;
};
static_assert(offsetof(struct arch_cpu, __current_thread) == ARCH_X86_64_PERCPU_CURRENT_THREAD_OFFSET);
static_assert(offsetof(struct arch_cpu, __syscall_number) == ARCH_X86_64_PERCPU_SYSCALL_NUMBER_OFFSET);

void arch_x86_64_percpu_ap_init(struct arch_limine_mp_info* cpu_info);
void arch_x86_64_percpu_bsp_init(void);

struct arch_cpu* arch_current_cpu(void);
