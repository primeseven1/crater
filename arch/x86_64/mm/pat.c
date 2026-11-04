#include <crater/init.h>
#include <crater/page.h>
#include <crater/panic.h>
#include <crater/printk.h>

#include <x86_64/asm/cpuid.h>
#include <x86_64/asm/msr.h>

#include "internal.h"

struct pat_map_entry {
	enum pt_flags flags;
	bool use_pat_bit, supported;
};
static struct pat_map_entry pat_map[PAT_TYPE_COUNT] = {
	[PAT_TYPE_UC] = { .flags = PT_NONE, .use_pat_bit = false, .supported = false },
	[PAT_TYPE_WC] = { .flags = PT_NONE, .use_pat_bit = false, .supported = false },
	[PAT_TYPE_WT] = { .flags = PT_NONE, .use_pat_bit = false, .supported = false },
	[PAT_TYPE_WP] = { .flags = PT_NONE, .use_pat_bit = false, .supported = false },
	[PAT_TYPE_WB] = { .flags = PT_NONE, .use_pat_bit = false, .supported = false },
	[PAT_TYPE_UC_MINUS] = { .flags = PT_NONE, .use_pat_bit = false, .supported = false }
};

static enum pat_type read_pat_entry(u64 pat, unsigned int index) {
	if (index >= 8)
		return PAT_TYPE_UNKNOWN;

	u8 type = (pat >> (index * 8)) & 0xFF;
	switch (type) {
	case PAT_TYPE_UC:
	case PAT_TYPE_WC:
	case PAT_TYPE_WT:
	case PAT_TYPE_WP:
	case PAT_TYPE_WB:
	case PAT_TYPE_UC_MINUS:
		break;
	default:
		return PAT_TYPE_UNKNOWN;
	}

	return (enum pat_type)type;
}

int pat_type_to_pt_flags(enum pat_type type, bool hugepage, enum pt_flags* flags) {
	if (type >= ARRAY_SIZE(pat_map) || type < 0)
		return -ENOSYS;
	if (!pat_map[type].supported)
		return -ENOTSUP;

	*flags = pat_map[type].flags;
	if (pat_map[type].use_pat_bit)
		*flags |= (hugepage) ? PT_HUGEPAGE_PAT : PT_4K_PAT;
	return 0;
}

static bool supports_pat = false;
static u64 expected_pat = U64_MAX;

static void pat_init(void) {
	u32 edx, _unused;
	arch_x86_64_cpuid(CPUID_LEAF_FEATURE_BITS, 0, &_unused, &_unused, &_unused, &edx);
	supports_pat = !!(edx & (1 << 16));
	if (unlikely(!supports_pat)) {
		pat_map[PAT_TYPE_WB] = (struct pat_map_entry){ .flags = PT_NONE, .use_pat_bit = false, .supported = true };
		pat_map[PAT_TYPE_WT] = (struct pat_map_entry){ .flags = PT_WRITETHROUGH, .use_pat_bit = false, .supported = true };
		pat_map[PAT_TYPE_UC_MINUS] = (struct pat_map_entry){ .flags = PT_CACHE_DISABLE, .use_pat_bit = false, .supported = true };
		return;
	}

	expected_pat = arch_x86_64_rdmsr(ARCH_X86_64_MSR_PAT);
	for (size_t i = 0; i < PAT_ENTRY_COUNT; i++) {
		const enum pat_type type = read_pat_entry(expected_pat, i);
		if (unlikely(type == PAT_TYPE_UNKNOWN))
			continue;
		if (pat_map[type].supported) {
			if (likely(i != PAT_ENTRY_COUNT - 2 && i != PAT_ENTRY_COUNT - 1)) /* Limine leaves the last two entries whatever it wants */
				printk(PRINTK_WARN "pat: Pat type %d appeared again for entry %zu\n", type, i);
			continue;
		}

		/* PAT is indexed like this: (PAT << 2) | (UC << 1) | (WT) */
		pat_map[type].supported = true;
		if (i & (1 << 0))
			pat_map[type].flags |= PT_WRITETHROUGH;
		if (i & (1 << 1))
			pat_map[type].flags |= PT_CACHE_DISABLE;
		if (i & (1 << 2))
			pat_map[type].use_pat_bit = true; /* This bit can change based on if it's a hugepage or not, which is why it's not just OR'd into the flags */
	}

	/*
	 * This checks to make sure that the bootloader has proper compliance. If it doesn't, we don't try to correct it here since it's a very touchy operation to do,
	 * and it's not likely to happen. If a critical caching mode like WB or UC is missing somehow, the kernel will fail to boot anyway (from arch_pagetable_map() returning -ENOTSUP).
	 */
	const u64 limine_expected_pat = (PAT_TYPE_WB) | (PAT_TYPE_WT << 8) | (PAT_TYPE_UC_MINUS << 16) | (PAT_TYPE_UC << 24) | ((u64)PAT_TYPE_WP << 32) | ((u64)PAT_TYPE_WC << 40);
	const u64 mask = 0xFFFFFFFFFFFF;
	if (unlikely((expected_pat & mask) != (limine_expected_pat & mask)))
		printk(PRINTK_WARN "pat: Bootloader did not configure the PAT correctly (expected %#lx, got %#lx)\n", limine_expected_pat, expected_pat);
}

static void pat_ap_init(void) {
	if (likely(supports_pat)) {
		const u64 msr = arch_x86_64_rdmsr(ARCH_X86_64_MSR_PAT);
		if (expected_pat != msr)
			panic("Bootloader gave a mismatched PAT");
	}
}

INIT_TASK_DEFINE(arch_x86_64_pat_init_task, INIT_TASK_SCOPE_BSP, pat_init);
INIT_TASK_DEFINE(arch_x86_64_pat_ap_init_task, INIT_TASK_SCOPE_AP, pat_ap_init);
