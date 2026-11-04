#include <crater/compiler.h>
#include <crater/init.h>
#include <crater/percpu.h>
#include <crater/sched.h>

#include <arch/tlb.h>
#include <arch/asm/linkage.h>

#include <x86_64/syscall.h>
#include <x86_64/idt.h>
#include <x86_64/e9.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/flags.h>

static void enable_syscall(void) {
	const unsigned long sf_mask = ARCH_X86_64_RFLAGS_CF | ARCH_X86_64_RFLAGS_PF | ARCH_X86_64_RFLAGS_AF | ARCH_X86_64_RFLAGS_ZF |
		ARCH_X86_64_RFLAGS_SF | ARCH_X86_64_RFLAGS_TF | ARCH_X86_64_RFLAGS_IF | ARCH_X86_64_RFLAGS_DF | ARCH_X86_64_RFLAGS_OF |
		ARCH_X86_64_RFLAGS_IOPL | ARCH_X86_64_RFLAGS_NT | ARCH_X86_64_RFLAGS_RF | ARCH_X86_64_RFLAGS_AC | ARCH_X86_64_RFLAGS_ID;
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_SF_MASK, sf_mask);

	const u64 user32_sel = ARCH_X86_64_SEGMENT_USER_CODE_32 | ARCH_X86_64_SEGMENT_CPL3;
	const u64 kernel64_sel = ARCH_X86_64_SEGMENT_KERNEL_CODE | ARCH_X86_64_SEGMENT_CPL0;
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_LSTAR, (uintptr_t)arch_x86_64_syscall_entry);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_CSTAR, (uintptr_t)arch_x86_64_compat_syscall_entry);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_STAR, (user32_sel << 48) | (kernel64_sel << 32));

	arch_x86_64_wrmsr(ARCH_X86_64_MSR_EFER, arch_x86_64_rdmsr(ARCH_X86_64_MSR_EFER) | ARCH_X86_64_MSR_EFER_SCE);
}

__diag_push();
__diag_ignore("-Wmissing-prototypes")

_Noreturn __asmlinkage void arch_x86_64_kernel_ap_main(struct arch_limine_mp_info* cpu_info) {
	arch_tlb_flush_all();
	arch_x86_64_percpu_ap_init(cpu_info);
	enable_syscall();
	sched_assign_id();
	arch_x86_64_gdt_init();
	arch_x86_64_idt_init();
	kernel_ap_main();
}

_Noreturn __asmlinkage void arch_x86_64_kernel_main(void) {
	arch_x86_64_percpu_bsp_init();
	enable_syscall();
	sched_assign_id();
#ifdef CONFIG_ARCH_X86_64_E9HACK
	arch_x86_64_e9_init();
#endif /* CONFIG_ARCH_X86_64_E9HACK */
	arch_x86_64_gdt_init();
	arch_x86_64_idt_init();
	kernel_main();
}

__diag_pop();
