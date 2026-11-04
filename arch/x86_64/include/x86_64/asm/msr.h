#pragma once

#define ARCH_X86_64_MSR_APIC_BASE 0x1B
#define ARCH_X86_64_MSR_PAT 0x277
#define ARCH_X86_64_MSR_X2APIC_BASE 0x800
#define ARCH_X86_64_MSR_EFER 0xC0000080
#define ARCH_X86_64_MSR_STAR 0xC0000081
#define ARCH_X86_64_MSR_LSTAR 0xC0000082
#define ARCH_X86_64_MSR_CSTAR 0xC0000083
#define ARCH_X86_64_MSR_SF_MASK 0xC0000084
#define ARCH_X86_64_MSR_FS_BASE 0xC0000100
#define ARCH_X86_64_MSR_GS_BASE 0xC0000101
#define ARCH_X86_64_MSR_KERNEL_GS_BASE 0xC0000102

#define ARCH_X86_64_MSR_EFER_SCE (1 << 0)
#define ARCH_X86_64_MSR_EFER_LME (1 << 8)
#define ARCH_X86_64_MSR_EFER_LMA (1 << 10)
#define ARCH_X86_64_MSR_EFER_NXE (1 << 11)
#define ARCH_X86_64_MSR_EFER_SVME (1 << 12)
#define ARCH_X86_64_MSR_EFER_LMSLE (1 << 13)
#define ARCH_X86_64_MSR_EFER_FFXSR (1 << 14)
#define ARCH_X86_64_MSR_EFER_TCE (1 << 15)

#ifndef __ASSEMBLER__

#include <crater/types.h>

/**
 * @brief Read from an MSR
 *
 * This function does zero validation. Make sure you are reading
 * from a valid MSR, or you can crash the system or get garbage values.
 *
 * @param msr The MSR to read from
 * @return The value of the MSR
 */
static inline u64 arch_x86_64_rdmsr(u32 msr) {
	u32 low, high;
	__asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
	return ((u64)high << 32) | low;
}

/**
 * @brief Write to an MSR
 *
 * This function does absolutely zero validation. Be careful as you can
 * crash the system very easily.
 *
 * @param msr The MSR to write to
 * @param val The value to write to the MSR
 */
static inline void arch_x86_64_wrmsr(u32 msr, u64 val) {
	u32 low = val & U32_MAX;
	u32 high = val >> 32;
	__asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

#endif /* __ASSEMBLER__ */
