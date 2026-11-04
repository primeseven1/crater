#include <arch/asm/linkage.h>
#include <x86_64/asm/segment.h>

.text
.code64

.macro isr_no_err n
.text
ASM_FUNCTION_START(isr_\n, ASM_LINKAGE_LOCAL):
	pushq $0
	pushq $\n
	jmp isr_common
ASM_FUNCTION_END(isr_\n)
.previous
.endm

.macro isr_err n
.text
ASM_FUNCTION_START(isr_\n, ASM_LINKAGE_LOCAL):
	pushq $\n
	jmp isr_common
ASM_FUNCTION_END(isr_\n)
.previous
.endm

.macro swapgs_if_needed
	cmpq $ARCH_X86_64_SEGMENT_KERNEL_CODE, 24(%rsp)
	je 1f
	swapgs
	lfence
1:
.endm

ASM_FUNCTION_START(isr_common, ASM_LINKAGE_GLOBAL):
	cld
	swapgs_if_needed

	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %rax
	movq %cr2, %rax
	pushq %rax
	movq %es, %rax
	pushq %rax
	movq %ds, %rax
	pushq %rax
	movq $ARCH_X86_64_SEGMENT_KERNEL_DATA, %rax
	movq %rax, %es
	movq %rax, %ds

	/* struct arch_context* */
	movq %rsp, %rdi

	/* Align stack and push null frame */
	movq %rsp, %rbx
	andq $15, %rbx
	subq %rbx, %rsp
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	call arch_x86_64_do_interrupt

	/* Remove fake stack frame and realign the stack */
	leaq 16(%rsp,%rbx), %rsp

	popq %rax
	movq %rax, %ds
	popq %rax
	movq %rax, %es
	popq %rax
	movq %rax, %cr2
	popq %rax
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15

	swapgs_if_needed
	addq $16, %rsp /* Remove vector + error code */
	iretq
ASM_FUNCTION_END(isr_common)

.altmacro
.set i, 0
.rept 256
.if ((i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30 || i == 8)
	isr_err %i
.else
	isr_no_err %i
.endif
	.set i, i+1
.endr

/* No, I could not just inline this without the assembler bitching */
.macro isr_table_define n
.quad isr_\n
.endm

/* With KASLR, there can't be relocations in .rodata */
#ifdef CONFIG_KASLR
.section .data
#else /* CONFIG_KASLR */
.section .rodata
#endif /* CONFIG_KASLR */
.globl isr_table
isr_table:
.altmacro
.set i, 0
.rept 256
	isr_table_define %i
	.set i, i+1
.endr
