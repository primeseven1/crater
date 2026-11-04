#pragma once

#define ARCH_X86_64_CTL0_PE (1 << 0)
#define ARCH_X86_64_CTL0_MP (1 << 1)
#define ARCH_X86_64_CTL0_EM (1 << 2)
#define ARCH_X86_64_CTL0_TS (1 << 3)
#define ARCH_X86_64_CTL0_ET (1 << 4)
#define ARCH_X86_64_CTL0_NE (1 << 5)
#define ARCH_X86_64_CTL0_WP (1 << 16)
#define ARCH_X86_64_CTL0_NW (1 << 29)
#define ARCH_X86_64_CTL0_CD (1 << 30)
#define ARCH_X86_64_CTL0_PG (1 << 31)

#define ARCH_X86_64_CTL4_VME (1 << 0)
#define ARCH_X86_64_CTL4_PVI (1 << 1)
#define ARCH_X86_64_CTL4_TSD (1 << 2)
#define ARCH_X86_64_CTL4_DE (1 << 3)
#define ARCH_X86_64_CTL4_PSE (1 << 4)
#define ARCH_X86_64_CTL4_PAE (1 << 5)
#define ARCH_X86_64_CTL4_MCE (1 << 6)
#define ARCH_X86_64_CTL4_PGE (1 << 7)
#define ARCH_X86_64_CTL4_PCE (1 << 8)
#define ARCH_X86_64_CTL4_OSFXSR (1 << 9)
#define ARCH_X86_64_CTL4_OSXMMEXCEPT (1 << 10)
#define ARCH_X86_64_CTL4_UIMP (1 << 11)
#define ARCH_X86_64_CTL4_LA57 (1 << 12)
#define ARCH_X86_64_CTL4_VMXE (1 << 13)
#define ARCH_X86_64_CTL4_SMXE (1 << 14)
#define ARCH_X86_64_CTL4_FSGSBASE (1 << 16)
#define ARCH_X86_64_CTL4_PCIDE (1 << 17)
#define ARCH_X86_64_CTL4_OSXSAVE (1 << 18)
#define ARCH_X86_64_CTL4_SMEP (1 << 20)
#define ARCH_X86_64_CTL4_SMAP (1 << 21)
#define ARCH_X86_64_CTL4_PKE (1 << 22)
#define ARCH_X86_64_CTL4_CET (1 << 23)
#define ARCH_X86_64_CTL4_PKS (1 << 24)

#ifndef __ASSEMBLER__

#include <crater/types.h>

/**
 * @brief Read from the CR0 flags register
 * @return The value of the register
 */
static inline unsigned long arch_x86_64_ctl0_read(void) {
	unsigned long ret;
	__asm__("mov %%cr0, %0" : "=r"(ret));
	return ret;
}

/**
 * @brief Write to the CR0 flags register
 * @return The new value of the register
 */
static inline void arch_x86_64_ctl0_write(unsigned long flags) {
	__asm__ volatile("mov %0, %%cr0" : : "r"(flags) : "memory");
}

/**
 * @brief Read the page fault address
 * @return The address that caused the fault
 */
static inline uintptr_t arch_x86_64_ctl2_read(void) {
	uintptr_t ret;
	__asm__("mov %%cr2, %0" : "=r"(ret) : : "memory");
	return ret;
}

/**
 * @brief Read the top level page table address
 * @return The top level page table
 */
static inline physaddr_t arch_x86_64_ctl3_read(void) {
	unsigned long ret;
	__asm__("mov %%cr3, %0" : "=r"(ret));
	return (physaddr_t)ret;
}

/**
 * @brief Set the CPU's top level page table
 * @param pagetable The physical address of the page table
 */
static inline void arch_x86_64_ctl3_write(physaddr_t pagetable) {
	__asm__ volatile("mov %0, %%cr3" : : "r"((unsigned long)pagetable) : "memory");
}

/**
 * @brief Read from the CR4 flags register
 * @return The value of the register
 */
static inline unsigned long arch_x86_64_ctl4_read(void) {
	unsigned long ret;
	__asm__("mov %%cr4, %0" : "=r"(ret));
	return ret;
}

/**
 * @brief Write to the CR4 flags register
 * @param flags The new flags to write
 */
static inline void arch_x86_64_ctl4_write(unsigned long flags) {
	__asm__ volatile("mov %0, %%cr4" : : "r"(flags) : "memory");
}

#endif /* __ASSEMBLER__ */
