#include <crater/percpu.h>
#include <crater/limine.h>
#include <crater/panic.h>

#include <arch/processor.h>
#include <arch/percpu.h>
#include <x86_64/asm/msr.h>

#include "internal.h"

static struct limine_mp_request __limine_request mp_request = {
	.request.id = LIMINE_MP_REQUEST,
	.request.revision = 0,
	.arch_specific_response = NULL,
#ifdef CONFIG_ARCH_X86_64_X2APIC
	.flags = ARCH_X86_64_LIMINE_MP_REQUEST_X2APIC
#else /* CONFIG_ARCH_X86_64_X2APIC */
	.flags = 0
#endif /* CONFIG_ARCH_X86_64_X2APIC */
};

#ifdef CONFIG_SMP

u32 arch_get_cpu_count(void) {
	return mp_request.arch_specific_response->cpu_count;
}

#else

static void halt(struct arch_limine_mp_info* mp_info) {
	(void)mp_info;
	while (1)
		arch_cpu_idle();
}

#endif /* CONFIG_SMP */

void arch_start_cpus(void) {
	struct arch_cpu* acpu_current = &current_cpu()->arch_specific;

	struct arch_limine_mp_response* response = mp_request.arch_specific_response;
	bug(response->cpu_count >= U32_MAX);

	for (u32 i = 0; i < response->cpu_count; i++) {
		struct arch_limine_mp_info* limine_cpuinfo = response->cpus[i];
		if (acpu_current->lapic_id == limine_cpuinfo->lapic_id)
			continue;
#ifdef CONFIG_SMP
		atomic_store(&limine_cpuinfo->goto_address, ap_start);
#else
		atomic_store(&limine_cpuinfo->goto_address, halt);
#endif /* CONFIG_SMP */
	}
}

static inline void set_cpu(struct arch_cpu* acpu) {
	acpu->self = acpu;
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_GS_BASE, (uintptr_t)acpu);
}

static struct cpu bsp_cpu = { 0 };

void arch_x86_64_percpu_ap_init(struct arch_limine_mp_info* cpu_info) {
	struct page* page = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(struct cpu)));

	struct cpu* cpu = page_hhdm_virtual(page);
	memset(cpu, 0, sizeof(*cpu));
	set_cpu(&cpu->arch_specific);

	struct arch_cpu* acpu = &cpu->arch_specific;
	acpu->lapic_id = cpu_info->lapic_id;
	acpu->acpi_id = cpu_info->processor_id;
}

void arch_x86_64_percpu_bsp_init(void) {
	struct arch_cpu* acpu = &bsp_cpu.arch_specific;
	set_cpu(acpu);

	struct arch_limine_mp_response* response = mp_request.arch_specific_response;
	bug(response->cpu_count >= U32_MAX);

	acpu->lapic_id = response->bsp_lapic_id;
	for (u32 i = 0; i < response->cpu_count; i++) {
		struct arch_limine_mp_info* limine_cpuinfo = response->cpus[i];
		if (acpu->lapic_id == limine_cpuinfo->lapic_id) {
			acpu->acpi_id = limine_cpuinfo->processor_id;
			break;
		}
	}
}

struct arch_cpu* arch_current_cpu(void) {
	struct arch_cpu* cpu;
	__asm__("movq %%gs:%c1, %0" : "=r"(cpu) : "i"(offsetof(struct arch_cpu, self)));
	return cpu;
}
