#include <crater/common.h>
#include <crater/compiler.h>
#include <crater/panic.h>
#include <crater/init.h>
#include <crater/vmm.h>
#include <crater/slab.h>
#include "internal.h"

static struct slab_cache* vma_cache;

static struct vm_area* vma_alloc(void) {
	struct vm_area* const ret = slab_cache_alloc(vma_cache);
	if (ret) {
		ret->start = 0;
		ret->end = 0;
		ret->page_size = 0;
		ret->prot = PGPROT_NONE;
		ret->vmm_flags = 0;
		rbtree_node_init(&ret->rbtree_link);
		list_node_init(&ret->list_link);
	}
	return ret;
}

static inline void vma_free(struct vm_area* vma) {
	if (vma)
		slab_cache_free(vma_cache, vma);
}

struct vm_area* vma_next(struct mm* mm, struct vm_area* vma) {
	if (list_is_last(&mm->vma_list, &vma->list_link))
		return NULL;
	return list_next_entry(vma, list_link);
}

struct vm_area* vma_prev(struct mm* mm, struct vm_area* vma) {
	if (list_is_first(&mm->vma_list, &vma->list_link))
		return NULL;
	return list_prev_entry(vma, list_link);
}

/*
 * Finds the first VMA ending after an address. The returned address does not nessecarily contain the address.
 * If the address falls in a hole, this will return the next VMA above it if there is one.
 */
static struct vm_area* vma_find(struct mm* mm, uintptr_t address) {
	struct rbtree_node* node = mm->vma_rbtree.root;

	struct vm_area* ret = NULL;
	while (node) {
		struct vm_area* vma = rbtree_entry(node, struct vm_area, rbtree_link);
		if (vma->end > address) {
			ret = vma;
			if (vma->start <= address)
				break;
			node = node->left;
		} else {
			node = node->right;
		}
	}

	return ret;
}

/* Similar to vma_find(), but makes sure that the VMA contains the address */
struct vm_area* vma_lookup(struct mm* mm, uintptr_t address) {
	struct vm_area* ret = vma_find(mm, address);
	return (ret && ret->start <= address) ? ret : NULL;
}

/* Finds the first VMA overlapping a range */
static inline struct vm_area* vma_find_intersection(struct mm* mm, uintptr_t start, uintptr_t end) {
	struct vm_area* vma = vma_find(mm, start);
	return (vma && vma->start < end) ? vma : NULL;
}

static int vma_link(struct mm* mm, struct vm_area* vma) {
	struct rbtree_node** link = &mm->vma_rbtree.root;
	struct rbtree_node* parent = NULL;
	while (*link) {
		struct vm_area* current = rbtree_entry(*link, struct vm_area, rbtree_link);
		parent = *link;
		if (vma->end <= current->start)
			link = &parent->left;
		else if (vma->start >= current->end)
			link = &parent->right;
		else
			return -EINVAL;
	}

	rbtree_insert(&vma->rbtree_link, parent, link);
	rbtree_insert_fixup(&mm->vma_rbtree, &vma->rbtree_link);

	struct rbtree_node* const prev = rbtree_prev_node(&vma->rbtree_link);
	if (prev)
		list_add_after(&rbtree_entry(prev, struct vm_area, rbtree_link)->list_link, &vma->list_link);
	else
		list_add(&mm->vma_list, &vma->list_link);

	return 0;
}

static inline void vma_unlink(struct mm* mm, struct vm_area* vma) {
	rbtree_remove(&mm->vma_rbtree, &vma->rbtree_link);
	list_remove(&vma->list_link);
}

/* Split a VMA at address, upper is a VMA allocated by the caller, which will be linked into the tree */
static void _vma_split(struct mm* mm, struct vm_area* vma, uintptr_t address, struct vm_area* upper) {
	bug(address <= vma->start || address >= vma->end);
	bug((address & (vma->page_size - 1)) != 0);

	upper->start = address;
	upper->end = vma->end;
	upper->page_size = vma->page_size;
	upper->prot = vma->prot;
	upper->vmm_flags = vma->vmm_flags;
	rbtree_node_init(&upper->rbtree_link);
	list_node_init(&upper->list_link);

	vma->end = address;
	bug(vma_link(mm, upper) != 0);
}

struct vm_range_info {
	struct vm_area* first, *last;
	int flags_and, flags_or;
	bool split_start, split_end, has_hole;
};

/*
 * Survey the range to give callers whatever they need before mutating the range, out is clobbered even on failure.
 * -EINVAL is returned when the VMA cannot be split because of page sizes and address alignment
 */
static int vma_check_range(struct mm* mm, uintptr_t start, uintptr_t end, struct vm_range_info* out) {
	*out = (struct vm_range_info){ .first = NULL, .last = NULL, .flags_and = ~0, .flags_or = 0, .split_start = false, .split_end = false, .has_hole = false };

	struct vm_area* vma = vma_find_intersection(mm, start, end);
	out->first = vma;

	uintptr_t covered = start;
	for (; vma && vma->start < end; vma = vma_next(mm, vma)) {
		const size_t ps_mask = vma->page_size - 1;
		out->last = vma;
		out->flags_and &= vma->vmm_flags;
		out->flags_or |= vma->vmm_flags;

		/* A gap before this VMA, or the first one */
		if (vma->start > covered)
			out->has_hole = true;

		covered = vma->end;

		/* A split can only happen on a boundary aligned to the page size */
		if (vma->start < start) {
			if (start & ps_mask)
				return -EINVAL;
			out->split_start = true;
		}
		if (vma->end > end) {
			if (end & ps_mask)
				return -EINVAL;
			out->split_end = true;
		}
	}

	/* A gap is after the last VMA, or the range is unmapped */
	if (covered < end)
		out->has_hole = true;
	/* Nothing was found, so don't report every flag */
	if (!out->first)
		out->flags_and = 0;

	return 0;
}

/* Splits a VMA at the edges. On success the info is updated to describe the range after splitting. On failure the VMA's are untouched */
static int vma_split(struct mm* mm, uintptr_t start, uintptr_t end, struct vm_range_info* info) {
	if (!info->split_start && !info->split_end)
		return 0;

	struct vm_area* start_split = NULL;
	struct vm_area* end_split = NULL;
	if (info->split_start && !(start_split = vma_alloc()))
		return -ENOMEM;
	if (info->split_end && !(end_split = vma_alloc())) {
		vma_free(start_split); /* NULL safe */
		return -ENOMEM;
	}

	if (end_split)
		_vma_split(mm, info->last, end, end_split);
	if (start_split) {
		const bool single = (info->first == info->last);
		_vma_split(mm, info->first, start, start_split);
		info->first = start_split;
		if (single)
			info->last = start_split;
	}

	info->split_start = false;
	info->split_end = false;

	return 0;
}

static inline bool vma_is_mergeable(struct vm_area* a, struct vm_area* b) {
	if (a->end != b->start)
		return false;
	if (a->page_size != b->page_size || a->prot != b->prot || a->vmm_flags != b->vmm_flags)
		return false;
	return !(a->vmm_flags & (VMM_IOMEM | VMM_STACK));
}

static struct vm_area* vma_merge(struct mm* mm, struct vm_area* vma) {
	struct vm_area* next, *prev;
	while ((next = vma_next(mm, vma)) != NULL && vma_is_mergeable(vma, next)) {
		vma->end = next->end;
		vma_unlink(mm, next);
		vma_free(next);
	}
	while ((prev = vma_prev(mm, vma)) != NULL && vma_is_mergeable(prev, vma)) {
		prev->end = vma->end;
		vma_unlink(mm, vma);
		vma_free(vma);
		vma = prev;
	}
	return vma;
}

static inline bool align_address_up(uintptr_t address, size_t align, uintptr_t* out) {
	if (address >= UINTPTR_MAX - align)
		return false;
	*out = ROUND_UP(address, align);
	return true;
}

static inline bool align_size_up(size_t size, size_t align, size_t* out) {
	if (size >= SIZE_MAX - align)
		return false;
	*out = ROUND_UP(size, align);
	return true;
}

static int vma_find_gap(struct mm* mm, size_t size, uintptr_t low, uintptr_t high, size_t align, uintptr_t* out) {
	if (size == 0 || (align & (align - 1)) || high < low)
		return -EINVAL;
	if (high - low < size)
		return -ENOMEM;

	uintptr_t address;
	if (!align_address_up(low, align, &address) || address >= high)
		return -ENOMEM;

	struct vm_area* vma;
	list_for_each_entry(vma, &mm->vma_list, list_link) {
		if (vma->end <= address)
			continue;
		if (vma->start >= high)
			break;
		if (vma->start > address && vma->start - address >= size)
			goto out;
		if (!align_address_up(vma->end, align, &address) || address >= high)
			return -ENOMEM;
	}

	if (high - address < size)
		return -ENOMEM;
out:
	*out = address;
	return 0;
}

static int _vma_unmap(struct mm* mm, uintptr_t address, uintptr_t end, struct vm_range_info* info) {
	if (!info->first)
		return 0;

	int err = vma_split(mm, address, end, info);
	if (err)
		return err;
	
	struct vm_area* vma = info->first;
	while (vma && vma->start < end) {
		struct vm_area* next = vma_next(mm, vma);
		vma_unlink(mm, vma);
		vma_free(vma);
		vma = next;
	}

	return 0;
}

int vma_map(struct mm* mm, uintptr_t hint, size_t size, pgprot_t prot, int vmm_flags, uintptr_t* out) {
	if (size == 0 || (vmm_flags & ~VMM_ALL_MASK) || ((vmm_flags & VMM_NOREPLACE) && !(vmm_flags & VMM_FIXED)))
		return -EINVAL;

	const size_t page_size = vm_get_page_size_from_flags(vmm_flags);
	if (page_size == 0)
		return -EINVAL;
	if (!arch_supports_page_size(page_size))
		return -EOPNOTSUPP;
	if (!align_size_up(size, page_size, &size))
		return -ERANGE;

	uintptr_t address, hint_end;
	struct vm_range_info info;
	if (vmm_flags & VMM_FIXED) {
		address = hint;
		if (address % page_size)
			return -EINVAL;
		if (__builtin_add_overflow(hint, size, &hint_end))
			return -ERANGE;

		int err = vma_check_range(mm, address, hint_end, &info);
		if (err)
			return err;

		if ((vmm_flags & VMM_NOREPLACE) && info.first)
			return -EEXIST;
		if (info.flags_or & VMM_SEALED)
			return -EPERM;
	} else {
		/* Check if the hint is usable */
		if (hint) {
			hint = ROUND_DOWN(hint, page_size);
			if (__builtin_add_overflow(hint, size, &hint_end))
				hint = 0;
			else if (hint < mm->mmap.start || hint_end >= mm->mmap.end)
				hint = 0;
			else if (vma_find_intersection(mm, hint, hint_end))
				hint = 0;
		}

		/* Use the hint if it's usable. If not, ignore it */
		if (hint) {
			address = hint;
		} else {
			const struct vmm_range* range = (vmm_flags & VMM_STACK) ? &mm->stack : &mm->mmap;
			int err = vma_find_gap(mm, size, range->start, range->end, page_size, &address);
			if (err)
				return err;
		}
	}

	struct vm_area* vma = vma_alloc();
	if (!vma)
		return -ENOMEM;

	if (vmm_flags & VMM_FIXED) {
		/* Try to purge everything in the range. On failure, this does NOT restore anything */
		int err = _vma_unmap(mm, address, address + size, &info);
		if (unlikely(err)) {
			vma_free(vma);
			return err;
		}
	}

	/* vma_alloc() initializes rbtree_link and list_link */
	vma->start = address;
	vma->end = address + size;
	vma->page_size = page_size;
	vma->prot = prot;
	vma->vmm_flags = vmm_flags & VM_AREA_PERSISTENT_FLAGS;

	bug(vma_link(mm, vma) != 0);
	vma_merge(mm, vma);

	if (out)
		*out = address;
	return 0;
}

int vma_update(struct mm* mm, uintptr_t address, size_t size, pgprot_t prot, int vmm_flags) {
	if (address % PAGE_SIZE || size == 0 || (vmm_flags & (VMM_PHYSICAL | VMM_FIXED | VMM_NOREPLACE | VMM_HUGETLB | VMM_HUGETLB_2MB | VMM_HUGETLB_1GB | VMM_IOMEM | VMM_STACK)))
		return -EINVAL;

	if (!align_size_up(size, PAGE_SIZE, &size))
		return -ERANGE;
	uintptr_t end;
	if (__builtin_add_overflow(address, size, &end))
		return -ERANGE;

	/* Make sure the whole range can be changed */
	struct vm_range_info info;
	int err = vma_check_range(mm, address, end, &info);
	if (err)
		return err;
	if (!info.first || info.has_hole)
		return -ENOENT;
	if (info.flags_or & VMM_SEALED)
		return -EPERM;

	err = vma_split(mm, address, end, &info);
	if (err)
		return err;

	struct vm_area* vma = info.first;
	const int immutable = VM_AREA_PERSISTENT_FLAGS & ~VMM_SEALED;
	while (vma && vma->start < end) {
		vma->prot = prot;
		vma->vmm_flags = (vma->vmm_flags & immutable) | (vmm_flags & ~immutable);
		struct vm_area* survivor = vma_merge(mm, vma);
		vma = vma_next(mm, survivor);
	}

	return 0;
}

int vma_unmap(struct mm* mm, uintptr_t address, size_t size, int vmm_flags) {
	if (vmm_flags != 0 || size == 0 || address % PAGE_SIZE)
		return -EINVAL;

	if (!align_size_up(size, PAGE_SIZE, &size))
		return -ERANGE;
	uintptr_t end;
	if (__builtin_add_overflow(address, size, &end))
		return -ERANGE;

	/* Unlike vma_update(), this function does not care about holes */
	struct vm_range_info info;
	int err = vma_check_range(mm, address, end, &info);
	if (err)
		return err;
	if (info.flags_or & VMM_SEALED)
		return -EPERM;

	return _vma_unmap(mm, address, end, &info);
}

void vma_destroy(struct list_head* vma_list) {
	struct vm_area* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, vma_list, list_link)
		vma_free(pos);
}

static void vma_init(void) {
	vma_cache = slab_cache_create(sizeof(struct vm_area), alignof(struct vm_area), MM_ZONE_NORMAL, NULL, NULL);
	if (unlikely(!vma_cache))
		out_of_memory();
}

INIT_TASK_DECLARE(zones_init_task);
INIT_TASK_DEFINE(vma_init_task, INIT_TASK_SCOPE_BSP, vma_init, &zones_init_task);
