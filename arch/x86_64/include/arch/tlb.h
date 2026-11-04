#pragma once

#include <crater/types.h>
#include <crater/page.h>
#include <x86_64/asm/ctl.h>

static inline void arch_x86_64_invlpg(uintptr_t virtual) {
	__asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

static inline void arch_tlb_flush_all(void) {
	arch_x86_64_ctl3_write(arch_x86_64_ctl3_read()); /* Global pages disabled, this is fine */
}

static inline void arch_tlb_flush_count(uintptr_t virtual, size_t page_count) {
	for (size_t i = 0; i < page_count; i++)
		arch_x86_64_invlpg(virtual + i * PAGE_SIZE);
}
