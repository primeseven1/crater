#include <crater/sched.h>
#include <crater/proc.h>
#include <crater/string.h>
#include <crater/slab.h>
#include <crater/usercopy.h>

#include <x86_64/asm/cpuid.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/ctl.h>

#include "internal.h"

#define XCR0_X87 (1ull << 0)
#define XCR0_SSE (1ull << 1)
#define XCR0_AVX (1ull << 2)
#define XCR0_AVX512 (7ull << 5)
#define XCR0_MASK (XCR0_AVX512 | XCR0_AVX | XCR0_SSE | XCR0_X87)

static enum {
	NONE,
	FXSAVE,
	XSAVE,
	XSAVEOPT
} xsave_method = NONE;
static u32 xsave_size = 0;
static struct slab_cache* xsave_cache = NULL;

static void save_extended_context(struct arch_context_extended* region) {
	region->user_gsbase = (void*)(uintptr_t)arch_x86_64_rdmsr(ARCH_X86_64_MSR_KERNEL_GS_BASE);
	region->user_fsbase = (void*)(uintptr_t)arch_x86_64_rdmsr(ARCH_X86_64_MSR_FS_BASE);
	switch (xsave_method) {
	case FXSAVE:
		__asm__ volatile("fxsaveq (%0)" : : "r"(region->un.fxsave_region) : "memory");
		break;
	case XSAVE:
		__asm__ volatile("xsaveq (%0)" : : "r"(region->un.xsave_region), "a"(-1), "d"(-1) : "memory");
		break;
	case XSAVEOPT:
		__asm__ volatile("xsaveoptq (%0)" : : "r"(region->un.xsave_region), "a"(-1), "d"(-1) : "memory");
		break;
	default:
		bug("Invalid xsave method");
	}
}

static void restore_extended_context(struct arch_context_extended* region) {
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_KERNEL_GS_BASE, (uintptr_t)region->user_gsbase);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_FS_BASE, (uintptr_t)region->user_fsbase);
	switch (xsave_method) {
	case FXSAVE:
		__asm__ volatile("fxrstorq (%0)" : : "r"(region->un.fxsave_region) : "memory");
		break;
	case XSAVE:
	case XSAVEOPT:
		__asm__ volatile("xrstorq (%0)" : : "r"(region->un.xsave_region), "a"(-1), "d"(-1) : "memory");
		break;
	default:
		bug("Invalid xsave method");
	}
}

void arch_context_switch(struct thread* current, struct thread* next) {
	save_extended_context(&current->context.arch_extended_context);
	restore_extended_context(&next->context.arch_extended_context);

	struct arch_cpu* const acpu = arch_current_cpu();
	acpu->tss.rsp[0] = next->kernel_stack_top;
	acpu->__current_thread = next;

	if (next->context.arch_context.cs & ARCH_X86_64_SEGMENT_CPL3)
		sched_userspace_check(&next->context.arch_context);

	context_switch_gp_regs(&current->context.arch_context, &next->context.arch_context, &current->topology.on_cpu);
}

int arch_context_init(struct context* context) {
	void* xsave = slab_cache_alloc(xsave_cache);
	if (!xsave)
		return -ENOMEM;

	memset(xsave, 0, xsave_size);
	struct arch_x86_64_fxsave_context* fxsave = xsave;
	fxsave->fcw = 0x037f;
	fxsave->mxcsr = 0x1f80;

	memset(&context->arch_context, 0, sizeof(context->arch_context));
	context->arch_extended_context.un.xsave_region = xsave;
	context->arch_extended_context.user_gsbase = NULL;
	context->arch_extended_context.user_fsbase = NULL;

	return 0;
}

void arch_context_destroy(struct context* ctx) {
	slab_cache_free(xsave_cache, ctx->arch_extended_context.un.xsave_region);
}

void arch_context_prepare_execution(struct arch_context* ctx, uintptr_t ip, uintptr_t sp) {
	const bool is_user = IS_USER_ADDRESS(ip);
	bug(IS_USER_ADDRESS(sp) != is_user);
	const u64 cs = is_user ? ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3 : ARCH_X86_64_SEGMENT_KERNEL_CODE | ARCH_X86_64_SEGMENT_CPL0;
	const u64 ds = is_user ? ARCH_X86_64_SEGMENT_USER_DATA | ARCH_X86_64_SEGMENT_CPL3 : ARCH_X86_64_SEGMENT_KERNEL_DATA | ARCH_X86_64_SEGMENT_CPL0;
	ctx->ds = ds;
	ctx->es = ds;
	ctx->rip = ip;
	ctx->cs = cs;
	ctx->rflags = RFLAGS_DEFAULT;
	ctx->rsp = sp;
	ctx->ss = ds;
}

static void enable_sse(void) {
	unsigned long ctl0 = arch_x86_64_ctl0_read();
	ctl0 &= ~ARCH_X86_64_CTL0_EM;
	ctl0 |= ARCH_X86_64_CTL0_MP;
	arch_x86_64_ctl0_write(ctl0);
	arch_x86_64_ctl4_write(arch_x86_64_ctl4_read() | ARCH_X86_64_CTL4_OSFXSR | ARCH_X86_64_CTL4_OSXMMEXCEPT);
}

static inline void xsetbv(u32 index, u64 value) {
	__asm__ volatile("xsetbv" : : "c"(index), "a"((u32)value), "d"((u32)(value >> 32)) : "memory");
}

static inline void enable_avx(void) {
	arch_x86_64_ctl4_write(arch_x86_64_ctl4_read() | ARCH_X86_64_CTL4_OSXSAVE);
	u32 eax, edx, _unused;
	arch_x86_64_cpuid(0x0d, 0, &eax, &_unused, &_unused, &edx);
	xsetbv(0, (((u64)edx << 32) | eax) & XCR0_MASK);
}

static void context_ap_init(void) {
	enable_sse();
	if (xsave_method == XSAVE || xsave_method == XSAVEOPT)
		enable_avx();
}

static void context_init(void) {
	enable_sse();

	u32 max_cpuid_leaf, xsave_supported, _unused;
	arch_x86_64_cpuid(0, 0, &max_cpuid_leaf, &_unused, &_unused, &_unused);
	arch_x86_64_cpuid(1, 0, &_unused, &_unused, &xsave_supported, &_unused);
	xsave_supported &= (1 << 26);

	size_t xsave_region_align;
	if (xsave_supported && max_cpuid_leaf >= 0x0d) {
		enable_avx();
		u32 xsave_flags;
		arch_x86_64_cpuid(0x0d, 1, &xsave_flags, &_unused, &_unused, &_unused);
		arch_x86_64_cpuid(0x0d, 0, &_unused, &xsave_size, &_unused, &_unused);
		xsave_method = (xsave_flags & (1 << 0)) ? XSAVEOPT : XSAVE;
		xsave_region_align = 64;
	} else {
		xsave_method = FXSAVE;
		xsave_size = sizeof(struct arch_x86_64_fxsave_context);
		xsave_region_align = alignof(struct arch_x86_64_fxsave_context);
	}

	xsave_cache = slab_cache_create(xsave_size, xsave_region_align, MM_ZONE_NORMAL, NULL, NULL);
	if (!xsave_cache)
		out_of_memory();
}

INIT_TASK_DECLARE(zones_init_task);
INIT_TASK_DEFINE(arch_context_init_task, INIT_TASK_SCOPE_BSP, context_init, &zones_init_task);
INIT_TASK_DEFINE(arch_context_ap_init_task, INIT_TASK_SCOPE_AP, context_ap_init);
