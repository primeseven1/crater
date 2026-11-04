#pragma once

#define ARCH_X86_64_RFLAGS_CF (1 << 0) /* Carry flag */
#define ARCH_X86_64_RFLAGS_RSVD1_SET (1 << 1)
#define ARCH_X86_64_RFLAGS_PF (1 << 2) /* Parity flag */
#define ARCH_X86_64_RFLAGS_RSVD3 (1 << 3)
#define ARCH_X86_64_RFLAGS_AF (1 << 4) /* Auxiliary carry flag */
#define ARCH_X86_64_RFLAGS_RSVD5 (1 << 5)
#define ARCH_X86_64_RFLAGS_ZF (1 << 6) /* Zero flag */
#define ARCH_X86_64_RFLAGS_SF (1 << 7) /* Sign flag */
#define ARCH_X86_64_RFLAGS_TF (1 << 8) /* Trap flag */
#define ARCH_X86_64_RFLAGS_IF (1 << 9) /* Interrupt flag */
#define ARCH_X86_64_RFLAGS_DF (1 << 10) /* Direction flag */
#define ARCH_X86_64_RFLAGS_OF (1 << 11) /* Overflow flag */
#define ARCH_X86_64_RFLAGS_IOPL (0b11 << 12) /* IOPL Mask */
#define ARCH_X86_64_RFLAGS_NT (1 << 14) /* Nested task */
#define ARCH_X86_64_RFLAGS_MD (1 << 15) /* Mode flag (NEC V-series only) */
#define ARCH_X86_64_RFLAGS_RF (1 << 16) /* Resume flag */
#define ARCH_X86_64_RFLAGS_VM (1 << 17) /* V8086 mode */
#define ARCH_X86_64_RFLAGS_AC (1 << 18) /* Alignment check for ring 3 */
#define ARCH_X86_64_RFLAGS_VIF (1 << 19) /* Virtual interrupt flag */
#define ARCH_X86_64_RFLAGS_VIP (1 << 20) /* Virtual interrupt pending */
#define ARCH_X86_64_RFLAGS_ID (1 << 21) /* Able to use CPUID instruction */

#ifndef __ASSEMBLER__

static inline unsigned long arch_x86_64_read_rflags(void) {
	unsigned long flags;
	__asm__("pushfq\n\t"
			"popq %0"
			: "=r"(flags)
			:
			: "memory");
	return flags;
}

#endif /* __ASSEMBLER__ */
