#include <crater/percpu.h>
#include <crater/string.h>
#include <crater/vmm.h>
#include <crater/init.h>
#include <x86_64/asm/segment.h>
#include <x86_64/idt.h>

static const struct arch_x86_64_segment_descriptor base[6] = {
	{ .limit_low = 0x0000, .base_low = 0, .base_middle = 0, .access = 0x00, .flags = 0x00, .base_high = 0 }, /* null */
	{ .limit_low = 0xFFFF, .base_low = 0, .base_middle = 0, .access = 0x9B, .flags = 0xAF, .base_high = 0 }, /* kernel code */
	{ .limit_low = 0xFFFF, .base_low = 0, .base_middle = 0, .access = 0x93, .flags = 0xCF, .base_high = 0 }, /* kernel data */
	{ .limit_low = 0xFFFF, .base_low = 0, .base_middle = 0, .access = 0xFB, .flags = 0xCF, .base_high = 0 }, /* user code (32-bit) */
	{ .limit_low = 0xFFFF, .base_low = 0, .base_middle = 0, .access = 0xF3, .flags = 0xCF, .base_high = 0 }, /* user data */
	{ .limit_low = 0xFFFF, .base_low = 0, .base_middle = 0, .access = 0xFB, .flags = 0xAF, .base_high = 0 }  /* user code (64-bit) */
};

static void reload_gdt(const struct arch_x86_64_gdt* gdt) {
	const struct {
		u16 limit;
		const struct arch_x86_64_gdt* gdt;
	} __attribute__((packed, aligned(8))) gdtr = {
		.limit = sizeof(*gdt) - 1,
		.gdt = gdt
	};

	register void* lretq_rip;
	__asm__ volatile("lgdt %1\n\t"
			"swapgs\n\t"
			"movq %2, %%gs\n\t"
			"movq %2, %%fs\n\t"
			"swapgs\n\t"
			"movq %3, %%ds\n\t"
			"movq %3, %%ss\n\t"
			"movq %3, %%es\n\t"
			"leaq 1f(%%rip), %0\n\t"
			"pushq %4\n\t"
			"pushq %0\n\t"
			"lretq\n"
			"1:\n\t"
			"ltr %5"
			: "=&r"(lretq_rip)
			: "m"(gdtr), "r"((u64)0), "r"((u64)ARCH_X86_64_SEGMENT_KERNEL_DATA), "i"((u64)ARCH_X86_64_SEGMENT_KERNEL_CODE), "r"((u16)ARCH_X86_64_SEGMENT_TASK_STATE)
			: "memory", "cc");
}

void arch_x86_64_gdt_init(void) {
	struct cpu* const cpu = current_cpu();

	struct arch_x86_64_tss* const tss = &cpu->arch_specific.tss;
	memset(tss, 0, sizeof(*tss));
	tss->iomap_base = sizeof(*tss);
	struct arch_x86_64_gdt* const gdt = &cpu->arch_specific.gdt;
	memcpy(gdt->base, base, sizeof(gdt->base));

	const uintptr_t ptr = (uintptr_t)tss;
	cpu->arch_specific.gdt.tss = (struct arch_x86_64_tss_descriptor){
		.desc = (struct arch_x86_64_segment_descriptor){
			.limit_low = (sizeof(struct arch_x86_64_tss) - 1) & U16_MAX,
			.base_low = ptr & U16_MAX, .base_middle = (ptr >> 16) & U8_MAX, .access = 0x89,
			.flags = 0, .base_high = (ptr >> 24) & U8_MAX
		},
		.base_high = ((u64)ptr >> 32) & U32_MAX, ._unused = 0
	};

	reload_gdt(gdt);
}

static void ist_init(void) {
	struct cpu* const cpu = current_cpu();
	struct arch_x86_64_tss* const tss = &cpu->arch_specific.tss;
	for (int i = 0; i < ARCH_X86_64_IDT_IST_COUNT; i++) {
		tss->ist[i] = alloc_stack();
		if (IS_PTR_ERR(tss->ist[i])) {
			int err = PTR_ERR(tss->ist[i]);
			if (err == -ENOMEM)
				out_of_memory();
			else
				panic("Failed to allocate IST stack: %d", err);
		}
	}
}

INIT_TASK_DECLARE(vmm_init_task, vmm_ap_init_task);
INIT_TASK_DEFINE(arch_x86_64_ist_init_task, INIT_TASK_SCOPE_BSP, ist_init, &vmm_init_task);
INIT_TASK_DEFINE(arch_x86_64_ist_ap_init_task, INIT_TASK_SCOPE_AP, ist_init, &vmm_ap_init_task);
