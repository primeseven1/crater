#include <arch/asm/linkage.h>
#include <arch/asm/errno.h>
#include <x86_64/asm/segment.h>
#include <x86_64/asm/offsets.h>
#include <x86_64/asm/flags.h>

#ifdef CONFIG_KASLR /* Relocations cannot happen in .rodata. When KASLR is enabled, put the table in .data */
.data
#else /* CONFIG_KASLR */
.section .rodata
#endif /* CONFIG_KASLR */
.align 8
syscall_table:
	.quad syscall_exit
	.quad syscall_yield
	.quad syscall_write
syscall_table_end:
.equ syscall_count, (syscall_table_end - syscall_table) / 8

.code64
.text

.macro restore_context
	popq %rbx
	movq %rbx, %ds
	popq %rbx
	movq %rbx, %es
	addq $8, %rsp /* Skip CR2 */
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
	addq $16, %rsp /* Skip the IDT vector and error code */
.endm

.macro swapgs_if_needed_exit
	cmpq $ARCH_X86_64_SEGMENT_KERNEL_CODE, 8(%rsp)
	je 1f
	swapgs
1:
.endm

ASM_FUNCTION_START(arch_x86_64_syscall_entry, ASM_LINKAGE_GLOBAL):
	swapgs
	lfence /* spectre v1 */

	movq %rax, %gs:ARCH_X86_64_PERCPU_SYSCALL_NUMBER_OFFSET
	movq %gs:ARCH_X86_64_PERCPU_CURRENT_THREAD_OFFSET, %rax
	movq 0(%rax), %rax /* Kernel stack top */
	xchg %rax, %rsp /* Switch to the kernel stack */

	andq $-16, %rsp /* Align the stack */

	/* Push user interrupt frame, 7 values */
	pushq $(ARCH_X86_64_SEGMENT_USER_DATA | ARCH_X86_64_SEGMENT_CPL3)
	pushq %rax /* User stack pointer */
	pushq %r11 /* The syscall instruction saves rflags in r11 */
	pushq $(ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3)
	pushq %rcx /* The syscall instruction saves rip in rcx */
	pushq $0 /* Error code (nothing) */
	pushq $0 /* IDT vector (nothing) */

	movq %gs:ARCH_X86_64_PERCPU_SYSCALL_NUMBER_OFFSET, %rax

	/* Now the general purpose registers, 18 values */
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
	pushq $0 /* Do not care about CR2 */
	movq %es, %rbx
	pushq %rbx
	movq %ds, %rbx
	pushq %rbx

	movq $(ARCH_X86_64_SEGMENT_KERNEL_DATA | ARCH_X86_64_SEGMENT_CPL0), %rbx
	movq %rbx, %es
	movq %rbx, %ds

	/* Prepare syscall arguments */
	pushq %r9 /* 7th argument, aligns the stack to 16 */
	movq %r8, %r9
	movq %r10, %r8
	movq %rdx, %rcx
	movq %rsi, %rdx
	movq %rdi, %rsi
	leaq 8(%rsp), %rdi /* Context argument */

	xorq %rbp, %rbp
	sti

	cmpq $syscall_count, %rax
	jae .invalid
	lfence /* spectre v1 */
	leaq syscall_table(%rip), %r10
	movq (%r10, %rax, 8), %r10
#ifdef CONFIG_ARCH_X86_64_RETPOLINE
	call __x86_indirect_thunk_r10 /* spectre v2 */
#else
	call *%r10
#endif /* CONFIG_ARCH_X86_64_RETPOLINE */
	jmp .exit_work
.invalid:
	call syscall_invalid
.exit_work:
	addq $8, %rsp /* Discard 7th argument */
	movq %rax, ARCH_X86_64_CTX_RAX_OFFSET(%rsp) /* Save the return value */
	cli

	/* Prepare argument and align the stack (again) */
	movq %rsp, %rdi
	subq $8, %rsp
	call sched_userspace_check
	addq $8, %rsp

	/*
	 * Check to see if the return address is non-canonical. If so, using sysretq will cause a #GP in ring 0 on the user stack.
	 * If LA57 is enabled, this breaks
	 */
	movq ARCH_X86_64_CTX_RIP_OFFSET(%rsp), %rcx
	movq %rcx, %rbx
	shlq $(64 - 48), %rbx
	sarq $(64 - 48), %rbx
	cmpq %rcx, %rbx
	jne .gpf

	/* Check to make sure the frame has the same selectors as from the IA32_STAR MSR */
	movq ARCH_X86_64_CTX_CS_OFFSET(%rsp), %rbx
	cmpq $(ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3), %rbx
	jne .exit_iret
	movq ARCH_X86_64_CTX_SS_OFFSET(%rsp), %rbx
	cmpq $(ARCH_X86_64_SEGMENT_USER_DATA | ARCH_X86_64_SEGMENT_CPL3), %rbx
	jne .exit_iret

	/*
	 * Losing RF re-arms an instruction breakpoint at the return address, which will trap forever.
	 * TF is restored, but traps at a different point than iretq, so it takes the iret return path as well.
	 */
	movq ARCH_X86_64_CTX_RFLAGS_OFFSET(%rsp), %rbx
	testq $(ARCH_X86_64_RFLAGS_RF | ARCH_X86_64_RFLAGS_TF), %rbx
	jnz .exit_iret

	/*
	 * The sysretq path overwrites rcx and r11 with rip and rflags. If something has changed the register in the frame (sigreturn, ptrace, etc),
	 * only iretq will hand those values back to userspace.
	 */
	cmpq ARCH_X86_64_CTX_R11_OFFSET(%rsp), %rbx
	jne .exit_iret
	movq ARCH_X86_64_CTX_RIP_OFFSET(%rsp), %rcx
	cmpq ARCH_X86_64_CTX_RCX_OFFSET(%rsp), %rcx
	jne .exit_iret

	restore_context
	movq 0(%rsp), %rcx /* RIP to RCX */
	movq 16(%rsp), %r11 /* RFLAGS to R11 */
	movq 24(%rsp), %rsp /* sysretq does not restore the stack pointer */
	swapgs
	sysretq
.exit_iret:
	restore_context
	swapgs_if_needed_exit
	iretq
.gpf:
	restore_context
	pushq $0 /* Error code */
	swapgs /* #GP handler will do swapgs when it checks the CS */
	jmp *(isr_table + 13 * 8)(%rip) /* Jump to the #GP handler, with a user frame */
ASM_FUNCTION_END(arch_x86_64_syscall_entry)

/*
 * Compatibility mode system calls (AMD only) are unsupported, so return -ENOSYS and let userspace deal with it.
 * This does execute on the user stack, so don't touch anything except eax
 */
ASM_FUNCTION_START(arch_x86_64_compat_syscall_entry, ASM_LINKAGE_GLOBAL):
	movl $-ENOSYS, %eax
	sysretl
ASM_FUNCTION_END(arch_x86_64_compat_syscall_entry)
