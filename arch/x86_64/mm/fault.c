#include <crater/sched.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/format.h>
#include <crater/vmm.h>
#include <crater/proc.h>
#include <crater/signal.h>
#include <x86_64/fault.h>
#include "internal.h"

#define PAGE_FAULT_WAS_PRESENT (1 << 0)
#define PAGE_FAULT_CAUSED_BY_WRITE (1 << 1)
#define PAGE_FAULT_IN_USERSPACE (1 << 2)
#define PAGE_FAULT_RESERVED_PTE_BIT_SET (1 << 3)
#define PAGE_FAULT_CAUSED_BY_INSTRUCTION_FETCH (1 << 4)
#define PAGE_FAULT_CAUSED_BY_PK_VIOLATION (1 << 5)
#define PAGE_FAULT_CAUSED_BY_SHADOW_STACK (1 << 6)
#define PAGE_FAULT_CAUSED_BY_SGX (1 << 15)

void arch_x86_64_general_protection_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;

	if (ctx->cs == (ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3)) {
		printk("pid%d: general protection fault at RIP %#.20lx\n", current_proc()->pid, ctx->rip);
		proc_terminate(SIGSEGV); /* TODO: No signals yet */
	} else if (current_thread()->in_usercopy && usercopy_context_fixup_fault(ctx)) {
		return;
	}

	panic("General protection fault at RIP %#.20lx", ctx->rip);
}

void arch_x86_64_page_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;
	bug(ctx->err_code & (PAGE_FAULT_RESERVED_PTE_BIT_SET | PAGE_FAULT_CAUSED_BY_PK_VIOLATION | PAGE_FAULT_CAUSED_BY_SHADOW_STACK | PAGE_FAULT_CAUSED_BY_SGX));

	int vm_fault_flags = 0;
	if (ctx->err_code & PAGE_FAULT_CAUSED_BY_WRITE)
		vm_fault_flags |= VM_FAULT_WRITE;
	if (ctx->err_code & PAGE_FAULT_CAUSED_BY_INSTRUCTION_FETCH)
		vm_fault_flags |= VM_FAULT_EXEC;
	if (ctx->err_code & PAGE_FAULT_WAS_PRESENT)
		vm_fault_flags |= VM_FAULT_PRESENT;

	const bool user = (ctx->cs == (ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3));
	if (user || current_thread()->in_usercopy) {
		const int err = vm_fault(ctx->cr2, vm_fault_flags);
		if (err == 0) {
			return;
		} else if (err == -ENOMEM) {
			out_of_memory();
			return; /* Let the page fault happen again */
		} else if (err != -EFAULT) {
			panic("Unhandled vm_fault() error: %d\n", err);
		}
	}

	if (user) {
		printk(PRINTK_INFO "pid%d: segmentation fault at RIP %#.20lx\n", current_proc()->pid, ctx->rip);
		proc_terminate(SIGSEGV); /* TODO: No signals yet */
	} else if (current_thread()->in_usercopy && usercopy_context_fixup_fault(ctx)) {
		return;
	} else {
		panic("Page fault at RIP %#.20lx, CR2 %#.20lx", ctx->rip, ctx->cr2);
	}
}
