#include <crater/common.h>
#include <crater/mm.h>
#include <crater/panic.h>
#include <crater/string.h>
#include <crater/limine.h>
#include <crater/init.h>
#include <crater/proc.h>
#include <crater/printk.h>
#include <crater/vmm.h>
#include <crater/page.h>
#include <crater/tlb.h>

#include <x86_64/asm/ctl.h>
#include <x86_64/asm/cpuid.h>
#include <x86_64/asm/msr.h>

#include "internal.h"

#define PTE_COUNT (PAGE_SIZE / sizeof(arch_pte_t))

static bool supports_nx, supports_pdpe1gb;
static physaddr_t max_physaddr;

static struct page* alloc_table(void) {
	struct page* page = alloc_page(MM_ZONE_NORMAL);
	if (!page)
		return NULL;
	memset(page_hhdm_virtual(page), 0, PAGE_SIZE);
	return page;
}

static struct page* table_page(physaddr_t physical) {
	struct page* page;
	int err = get_page_from_address(physical, &page);
	if (unlikely(err)) {
		if (err == -EACCES)
			bug("Use after free");
		else if (unlikely(err == -ENOMEM))
			bug("Page not backed by physical memory?"); /* Should never ever happen */
		else
			panic("Unhandled error calling get_page_from_address() in function %s(): %d", __func__, err);
	}

	release_page(page); /* Release get_page_from_address() ref */
	return page;
}
static void free_table_physical(physaddr_t physical) {
	release_page(table_page(physical));
}

static arch_pte_t pagetable_template[PTE_COUNT];
static_assert(sizeof(pagetable_template) == PAGE_SIZE);

arch_pte_t* arch_pagetable_new(void) {
	arch_pte_t* ret = page_hhdm_virtual(alloc_page(MM_ZONE_NORMAL));
	if (ret)
		memcpy(ret, pagetable_template, sizeof(pagetable_template));
	return ret;
}

/* Depth is the level of the table (3 = PML4, 2 = PDPT, 1 = PD, 0 = PT) */
static void destroy_depth(arch_pte_t* table, int depth) {
	const int count = (depth == 3) ? PTE_COUNT / 2 : PTE_COUNT;
	for (int i = 0; i < count; i++) {
		const arch_pte_t entry = table[i];
		if (!entry)
			continue;
		if (depth == 0) {
			vm_pagetable_teardown_leaf(entry & ~(0xFFF | PT_NX));
		} else if (entry & PT_HUGEPAGE) {
			bug(depth != 1 && depth != 2);
			const physaddr_t huge_mask = (depth == 2) ? PUD_SIZE - 1 : PMD_SIZE - 1;
			vm_pagetable_teardown_leaf(entry & ~(huge_mask | PT_NX));
		} else if (entry & PT_PRESENT) {
			const physaddr_t address = entry & ~(0xFFF | PT_NX);
			destroy_depth(hhdm_virtual(address), depth - 1);
			free_table_physical(address);
		}
	}
}

void arch_pagetable_free(arch_pte_t* table) {
	destroy_depth(table, 3);
	free_table_physical(hhdm_physical(table));
}

static inline physaddr_t table_physical(arch_pte_t entry) {
	return entry & ~(0xFFF | PT_NX);
}

static inline arch_pte_t* table_virtual(arch_pte_t entry) {
	return hhdm_virtual(table_physical(entry));
}

static bool table_is_empty(physaddr_t table, int depth) {
	const arch_pte_t* table_virtual = hhdm_virtual(table);
	for (size_t i = 0; i < PTE_COUNT; i++) {
		const arch_pte_t pte = table_virtual[i];
		if (!pte)
			continue;
		if (depth == 0 || (pte & PT_HUGEPAGE) || !(pte & PT_PRESENT))
			return false;
		if (!table_is_empty(table_physical(pte), depth - 1))
			return false;
	}
	return true;
}

static void free_empty_tables(struct tlb_batch* tlb_batch, physaddr_t physical, int depth) {
	if (depth > 0) {
		const arch_pte_t* table = hhdm_virtual(physical);
		for (size_t i = 0; i < PTE_COUNT; i++) {
			if (table[i])
				free_empty_tables(tlb_batch, table_physical(table[i]), depth - 1);
		}
	}
	tlb_batch_add_page_table(tlb_batch, table_page(physical));
}

static inline bool is_virtual_canonical(uintptr_t virtual) {
	return ((virtual >> 47 == 0) || (virtual >> 47 == 0x1FFFF));
}

static inline void pagetable_get_indexes(uintptr_t virtual, unsigned int* indexes, size_t index_arr_size) {
	bug(index_arr_size != 4);
	indexes[0] = virtual >> 39 & 0x01FF;
	indexes[1] = virtual >> 30 & 0x01FF;
	indexes[2] = virtual >> 21 & 0x01FF;
	indexes[3] = virtual >> 12 & 0x01FF;
}

static enum pt_flags arch_pte_flags_to_pt_flags(arch_pte_flags_t pte_flags) {
	enum pt_flags pt_flags = PT_NONE;
	if (pte_flags & ARCH_PTE_FLAG_READ)
		pt_flags |= PT_PRESENT;
	if (pte_flags & ARCH_PTE_FLAG_WRITE)
		pt_flags |= PT_READ_WRITE;
	if (pte_flags & ARCH_PTE_FLAG_USER)
		pt_flags |= PT_USER_SUPERVISOR;
	if (supports_nx && !(pte_flags & ARCH_PTE_FLAG_EXEC))
		pt_flags |= PT_NX;

	if (pte_flags & ARCH_PTE_FLAG_WT)
		pt_flags |= PT_WRITETHROUGH;
	else if (pte_flags & ARCH_PTE_FLAG_UC)
		pt_flags |= PT_CACHE_DISABLE;

	if (pte_flags & (ARCH_PTE_FLAG_HUGETLB_2MB | ARCH_PTE_FLAG_HUGETLB_1GB))
		pt_flags |= PT_HUGEPAGE;

	return pt_flags;
}

static inline bool args_ok(uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags, size_t page_size) {
	arch_pte_flags_t both_hugetlb_sizes = ARCH_PTE_FLAG_HUGETLB_2MB | ARCH_PTE_FLAG_HUGETLB_1GB;
	if ((pte_flags & both_hugetlb_sizes) == both_hugetlb_sizes)
		return false;

	if (virtual % page_size || physical % page_size) /* Here we don't need to check for valid page sizes, since this file controls it */
		return false;
	if (!is_virtual_canonical(virtual) || physical > max_physaddr)
		return false;

	const arch_pte_flags_t cache_mask = ARCH_PTE_FLAG_WT | ARCH_PTE_FLAG_UC | ARCH_PTE_FLAG_WC;
	switch (pte_flags & cache_mask) {
	case 0: /* Write-back */
	case ARCH_PTE_FLAG_WT:
	case ARCH_PTE_FLAG_UC:
	case ARCH_PTE_FLAG_WC: /* Only one cache flag is set */
		break;
	default: /* More than one cache flag is set */
		return false;
	}
	return true;
}

static int walk_pagetable(arch_pte_t* pagetable, uintptr_t virtual, bool create, bool user, size_t* page_size, arch_pte_t** ret) {
	*ret = NULL;

	unsigned int indexes[4];
	pagetable_get_indexes(virtual, indexes, ARRAY_SIZE(indexes));

	struct page* new_tables[3] = { NULL, NULL, NULL };
	size_t new_count = 0;
	arch_pte_t* graft_pte = NULL;
	arch_pte_t graft_value = 0;

	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(indexes) - 1; i++) {
		/* Check to see if we want either a 1GiB or 2MiB page */
		if ((*page_size == PUD_SIZE && i == 1) || (*page_size == PMD_SIZE && i == 2)) {
			*ret = &pagetable[indexes[i]];
			goto out;
		}

		/* 
		 * If not present, a new page table needs to be allocated. 
		 * Otherwise, handle the case where it could be a hugepage
		 */
		if (!(pagetable[indexes[i]] & PT_PRESENT)) {
			if (!create) {
				err = -ENOENT;
				goto out;
			}
			struct page* new = alloc_table();
			if (!new) {
				err = -ENOMEM;
				goto out;
			}
			new_tables[new_count++] = new;

			arch_pte_t value = page_to_physaddr(new) | PT_PRESENT | PT_READ_WRITE | (user ? PT_USER_SUPERVISOR : 0);
			if (graft_pte) {
				pagetable[indexes[i]] = value;
			} else {
				graft_pte = &pagetable[indexes[i]];
				graft_value = value;
			}

			pagetable = page_hhdm_virtual(new);
			continue;
		} else if (pagetable[indexes[i]] & PT_HUGEPAGE) {
			bug(i != 1 && i != 2); /* Make sure the hugepage bit is not set at an invalid level */

			/* 
			 * Make sure we're not returning a PTE that points to another page table, 
			 * a *page_size of zero means that the caller doesn't care if it's a hugepage or not.
			 *
			 * If *page_size is zero, then write the page size so that way the caller knows 
			 * if it needs it for some reason.
			 */
			size_t _page_size = (i == 1) ? PUD_SIZE : PMD_SIZE;
			if (_page_size != *page_size) {
				if (*page_size != 0) {
					err = -EEXIST;
					goto out;
				}
				*page_size = _page_size;
			}

			*ret = &pagetable[indexes[i]];
			goto out;
		}

		if (user && !(pagetable[indexes[i]] & PT_USER_SUPERVISOR)) {
			err = -EFAULT;
			printk(PRINTK_WARN "pagetable: Attempted to make user space mapping near kernel space mapping\n");
			goto out;
		}

		pagetable = table_virtual(pagetable[indexes[i]]);
	}

	*page_size = PAGE_SIZE;
	*ret = &pagetable[indexes[3]];
out:
	if (err == 0) {
		if (graft_pte)
			*graft_pte = graft_value; /* Link into the live tree all at once */
	} else {
		while (new_count--)
			release_page(new_tables[new_count]);
	}
	return err;
}

static enum pat_type get_pat_type_from_pte_flags(arch_pte_flags_t pte_flags) {
	if (pte_flags & ARCH_PTE_FLAG_WT)
		return PAT_TYPE_WT;
	if (pte_flags & ARCH_PTE_FLAG_UC)
		return PAT_TYPE_UC_MINUS; /* mtrr can override */
	if (pte_flags & ARCH_PTE_FLAG_WC)
		return PAT_TYPE_WC;
	return PAT_TYPE_WB;
}

/* This is okay to call before args_ok() */
static inline size_t get_page_size(arch_pte_flags_t pte_flags) {
	if (pte_flags & ARCH_PTE_FLAG_HUGETLB_2MB)
		return PMD_SIZE;
	else if (pte_flags & ARCH_PTE_FLAG_HUGETLB_1GB)
		return PUD_SIZE;
	return PAGE_SIZE;
}

int arch_pagetable_map(struct tlb_batch* tlb_batch, uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags) {
	size_t page_size = get_page_size(pte_flags);
	if (!args_ok(virtual, physical, pte_flags, page_size))
		return -EINVAL;
	if (!arch_supports_page_size(page_size))
		return -EOPNOTSUPP;

	enum pt_flags pat_flags;
	int err = pat_type_to_pt_flags(get_pat_type_from_pte_flags(pte_flags), page_size != PAGE_SIZE, &pat_flags);
	if (err)
		return err;

	arch_pte_t* pte;
	err = walk_pagetable(tlb_batch->pagetable, virtual, true, !!(pte_flags & ARCH_PTE_FLAG_USER), &page_size, &pte);
	if (err)
		return err;
	if (*pte) {
		if (page_size == PAGE_SIZE || (*pte & PT_HUGEPAGE) || !(*pte & PT_PRESENT))
			return -EEXIST;

		const physaddr_t table = table_physical(*pte);
		const int depth = (page_size == PUD_SIZE) ? 1 : 0;
		if (!table_is_empty(table, depth))
			return -EEXIST;

		free_empty_tables(tlb_batch, table, depth);
	}

	enum pt_flags pt_flags = arch_pte_flags_to_pt_flags(pte_flags) | pat_flags;
	if (!physical)
		pt_flags |= PT_NULL_MAPPING;
	*pte = physical | pt_flags;
	return 0;
}

int arch_pagetable_update(struct tlb_batch* tlb_batch, uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags) {
	const size_t expected_page_size = get_page_size(pte_flags);
	if (!args_ok(virtual, physical, pte_flags, expected_page_size))
		return -EINVAL;
	if (!arch_supports_page_size(expected_page_size))
		return -EOPNOTSUPP;

	arch_pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(tlb_batch->pagetable, virtual, false, !!(pte_flags & ARCH_PTE_FLAG_USER), &page_size, &pte);
	if (err)
		return err;

	if (expected_page_size != page_size)
		return -EEXIST;

	enum pt_flags pat_flags;
	err = pat_type_to_pt_flags(get_pat_type_from_pte_flags(pte_flags), expected_page_size != PAGE_SIZE, &pat_flags);
	if (err)
		return err;

	enum pt_flags pt_flags = arch_pte_flags_to_pt_flags(pte_flags) | pat_flags;
	if (!physical)
		pt_flags |= PT_NULL_MAPPING;
	*pte = physical | pt_flags;
	return 0;
}

int arch_pagetable_unmap(struct tlb_batch* tlb_batch, uintptr_t virtual, size_t* page_size) {
	if (!is_virtual_canonical(virtual))
		return -EINVAL;

	/*
	 * We won't check the alignment of the pointer if *page_size == 0 with the PTE entry.
	 * The caller is expected to handle this case, and we help the caller do that by writing the size
	 * unmapped back to *page_size.
	 */
	size_t align = *page_size != 0 ? *page_size : PAGE_SIZE;
	if (!arch_supports_page_size(align))
		return -EOPNOTSUPP;
	if (virtual % align)
		return -EINVAL;

	/* walk_pagetable() will write the page size of the entry at *page_size if zero. Otherwise -EEXIST is returned if page sizes do not match */
	arch_pte_t* pte;
	int err = walk_pagetable(tlb_batch->pagetable, virtual, false, false, page_size, &pte);
	if (err)
		return err;

	if (!(*pte))
		return -ENOENT;

	/* Do not free page tables here. This only needs to be checked when mapping hugepages */
	*pte = 0;
	return 0;
}

int arch_pagetable_get_physical(arch_pte_t* pagetable, uintptr_t virtual, physaddr_t* out) {
	if (!is_virtual_canonical(virtual))
		return -EINVAL;

	arch_pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, false, &page_size, &pte);
	if (err)
		return err;

	if (!(*pte))
		return -ENOENT;

	*out = (*pte & ~((page_size - 1) | PT_NX)) + ((uintptr_t)virtual & (page_size - 1));
	return 0;
}

arch_pte_t* arch_pagetable_get_cpu_current(void) {
	return hhdm_virtual(arch_x86_64_ctl3_read());
}

void arch_pagetable_switch(arch_pte_t* pagetable) {
	arch_x86_64_ctl3_write(hhdm_physical(pagetable));
}

size_t arch_pagetable_iterate_range(arch_pte_t* pagetable, uintptr_t virtual, uintptr_t* next) {
	if (!is_virtual_canonical(virtual)) {
		*next = ARCH_KERNEL_SPACE_START;
		return 0;
	}

	unsigned int indexes[4];
	pagetable_get_indexes(virtual, indexes, ARRAY_SIZE(indexes));

	static const size_t span[4] = { 1ull << 39, 1ull << 30, 1ull << 21, 1ull << 12 };
	for (int level = 0; level < 4; level++) {
		arch_pte_t entry = pagetable[indexes[level]];
		bool leaf = (level == 3 || ((level == 1 || level == 2) && (entry & PT_HUGEPAGE)));
		if (leaf) {
			*next = virtual + span[level];
			return entry ? span[level] : 0;
		}
		if (!(entry & PT_PRESENT)) {
			*next = virtual + span[level];
			return 0;
		}
		pagetable = table_virtual(entry);
	}

	bug("unreachable");
}

bool arch_supports_page_size(size_t page_size) {
	return (page_size == PAGE_SIZE || page_size == PMD_SIZE || (page_size == PUD_SIZE && supports_pdpe1gb));
}

static struct limine_paging_mode_request __limine_request paging_mode = {
	.request.id = LIMINE_PAGING_MODE_REQUEST,
	.request.revision = 1,
	.min_mode = ARCH_X86_64_LIMINE_PAGING_MODE_4LVL,
	.max_mode = ARCH_X86_64_LIMINE_PAGING_MODE_4LVL,
	.mode = ARCH_X86_64_LIMINE_PAGING_MODE_4LVL,
	.response = NULL
};

static physaddr_t bsp_bootloader_pagetable = 0;

static inline bool enable_nxe_and_get_previous_state(void) {
	u64 efer = arch_x86_64_rdmsr(ARCH_X86_64_MSR_EFER);
	bool enabled = efer & ARCH_X86_64_MSR_EFER_NXE;
	if (unlikely(!enabled))
		arch_x86_64_wrmsr(ARCH_X86_64_MSR_EFER, efer | ARCH_X86_64_MSR_EFER_NXE);
	return enabled;
}

static void pagetable_init(void) {
	u32 ecx, _unused;
	arch_x86_64_cpuid(0x07, 0, &_unused, &_unused, &ecx, &_unused);

	/*
	 * Check if the CPU supports level 5 page tables. If so, check if it's enabled in CR4.
	 * Even though the bootloader is very unlikely to make a mistake like this, checking this prevents
	 * a triple fault in the event that it does.
	 */
	bool level4 = ecx & (1 << 16) ? !(arch_x86_64_ctl4_read() & ARCH_X86_64_CTL4_LA57) : true;
	if (unlikely(!level4))
		panic("Bootloader chose wrong paging mode!");

	u32 edx;
	arch_x86_64_cpuid(CPUID_EXT_LEAF_FEATURE_BITS, 0, &_unused, &_unused, &_unused, &edx);
	supports_nx = !!(edx & (1 << 20));
	supports_pdpe1gb = !!(edx & (1 << 26));
	if (supports_nx && !enable_nxe_and_get_previous_state()) /* TODO: Make non-executable sections have the NX bit when enable_nxe returns false */
		printk(PRINTK_WARN "pagetable: NX bit supported but not enabled by the bootloader\n");

	u32 eax;
	arch_x86_64_cpuid(CPUID_EXT_LEAF_ADDRESS_SIZES, 0, &eax, &_unused, &_unused, &_unused);
	max_physaddr = (1ull << (eax & 0xFF)) - 1;

	/* Allocate all higher half L4 tables */
	arch_pte_t* l4 = hhdm_virtual(arch_x86_64_ctl3_read());
	size_t i = 0;
	for (; i < PTE_COUNT / 2; i++) {
		if (unlikely(l4[i] != 0)) {
			printk(PRINTK_WARN "pagetable: Lower half page table entry %zu not zero\n", i);
			l4[i] = 0;
		}
	}
	for (; i < PTE_COUNT; i++) {
		if (!(l4[i] & PT_PRESENT)) {
			struct page* page = alloc_table();
			if (unlikely(!page))
				out_of_memory();
			l4[i] = page_to_physaddr(page) | PT_PRESENT | PT_READ_WRITE;
		}
	}
	memcpy(pagetable_template, l4, sizeof(pagetable_template));
	bsp_bootloader_pagetable = hhdm_physical(l4);

	/* Page table changed, flush just in case */
	arch_x86_64_ctl3_write(arch_x86_64_ctl3_read());
}

static void pagetable_ap_init(void) {
	const physaddr_t current_pagetable = arch_x86_64_ctl3_read();
	if (unlikely(bsp_bootloader_pagetable != current_pagetable)) /* If this is true, chances are the CPU already faulted before this could even run */
		panic("AP page table is not the same as the BSP");
	if (supports_nx)
		enable_nxe_and_get_previous_state();
}

INIT_TASK_DECLARE(arch_x86_64_pat_init_task, arch_x86_64_pat_ap_init_task, zones_init_task);
INIT_TASK_DEFINE(arch_pagetable_init_task, INIT_TASK_SCOPE_BSP, pagetable_init, &arch_x86_64_pat_init_task, &zones_init_task);
INIT_TASK_DEFINE(arch_pagetable_ap_init_task, INIT_TASK_SCOPE_AP, pagetable_ap_init, &arch_x86_64_pat_ap_init_task);
