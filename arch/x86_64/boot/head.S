#include <arch/asm/linkage.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/flags.h>

.code64
.text
ASM_FUNCTION_START(zero_bss, ASM_LINKAGE_LOCAL):
	leaq _ld_kernel_bss_start(%rip), %rdi
	leaq _ld_kernel_bss_end(%rip), %rcx

	subq %rdi, %rcx /* Get the byte count */
	movq %rcx, %rdx /* Save the original number of bytes */
	xorq %rax, %rax /* The value to store (0) */

	/* Now zero out the memory 64 bits at a time */
	shrq $3, %rcx /* Divide byte count by 8 to get the number of qwords */
	rep stosq

	/* Now zero out the remaining bytes */
	movq %rdx, %rcx /* Get back the original number of bytes */
	andq $7, %rcx /* Now get the remainder of rcx when dividing by 8 */
	rep stosb

	ret
ASM_FUNCTION_END(zero_bss)

ASM_FUNCTION_START(_start, ASM_LINKAGE_GLOBAL):
	cli
	cld

	/* Align the stack and push null frame */
	andq $-16, %rsp
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	call zero_bss /* The bootloader has probably already done this, do it again anyway */
	call arch_x86_64_kernel_main

	ud2
ASM_FUNCTION_END(_start)

/* __asmlinkage void ap_start(struct arch_limine_mp_info* cpu_info) */
ASM_FUNCTION_START(ap_start, ASM_LINKAGE_GLOBAL):
	cli
	cld

	/* Align the stack and push null frame */
	andq $-16, %rsp
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	/* Limine CPU info is already in RDI */
	call arch_x86_64_kernel_ap_main

	ud2
ASM_FUNCTION_END(ap_start)
