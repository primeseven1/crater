#include <crater/sched.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/format.h>
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

static inline bool try_fixup_usercopy_fault(struct arch_context* ctx) {
	if (current_thread()->in_usercopy) {
		if (likely(usercopy_context_fixup_fault(ctx)))
			return true;
		printk(PRINTK_CRIT "mm: Could not fixup usercopy fault in usercopy context\n");
	}
	return false;
}

static int format_reason(char* buf, size_t bufsize, u64 err) {
	static const struct {
		u64 flag;
		const char* set, *clear;
	} flags[] = {
		{ PAGE_FAULT_WAS_PRESENT, "present", NULL }, { PAGE_FAULT_CAUSED_BY_WRITE, "write", "read" },
		{ PAGE_FAULT_IN_USERSPACE, "user", "kernel" }, { PAGE_FAULT_RESERVED_PTE_BIT_SET, "reserved_pte", NULL },
		{ PAGE_FAULT_CAUSED_BY_INSTRUCTION_FETCH, "instruction_fetch", NULL }, { PAGE_FAULT_CAUSED_BY_PK_VIOLATION, "pk_violation", NULL },
		{ PAGE_FAULT_CAUSED_BY_SHADOW_STACK, "shadow_stack", NULL }, { PAGE_FAULT_CAUSED_BY_SGX, "sgx", NULL }
	};

	if (bufsize == 0)
		return -EOVERFLOW;

	buf[0] = '\0';
	for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); i++) {
		const char* string = (err & flags[i].flag) ? flags[i].set : flags[i].clear;
		if (!string)
			continue;

		if (strlen(buf) > 0) {
			size_t count = strlcat(buf, ",", bufsize);
			if (count != strlen(buf))
				return -EOVERFLOW;
		}
		size_t count = strlcat(buf, string, bufsize);
		if (count != strlen(buf))
			return -EOVERFLOW;
	}

	return 0;
}

void arch_x86_64_general_protection_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;

	/* Can happen when dereferncing a non-canonical address */
	if (try_fixup_usercopy_fault(ctx))
		return;

	panic("General protection fault");
}

void arch_x86_64_page_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;
	if (try_fixup_usercopy_fault(ctx))
		return;

	char buf[64];
	int err = format_reason(buf, sizeof(buf), ctx->err_code);
	if (unlikely(err))
		printk(PRINTK_WARN "mm: Failed to format page fault reason: %d\n", err);

	printk(PRINTK_CRIT "Page fault at RIP %#.20lx, CR2 %#.20lx (%s)\n", ctx->rip, ctx->cr2, buf);
	panic("Page fault");
}
