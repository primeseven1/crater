#include <crater/vmm.h>
#include <crater/mm.h>
#include <crater/printk.h>
#include <crater/percpu.h>
#include <crater/sched.h>
#include <crater/trace.h>
#include <crater/irq.h>
#include <crater/usercopy.h>
#include <crater/tlb.h>
#include "internal.h"

static arch_pte_flags_t get_arch_pte_flags(pgprot_t prot, int vmm_flags) {
	arch_pte_flags_t pte_flags = ARCH_PTE_FLAG_NONE;
	const pgprot_t caching_mode = prot & PGPROT_CACHE_MASK;
	if (caching_mode == PGPROT_UC)
		pte_flags |= ARCH_PTE_FLAG_UC;
	else if (caching_mode == PGPROT_WT)
		pte_flags |= ARCH_PTE_FLAG_WT;
	else if (caching_mode == PGPROT_WC)
		pte_flags |= ARCH_PTE_FLAG_WC;

	if (vmm_flags & VMM_HUGETLB) {
		int size = vmm_flags & VMM_HUGETLB_SIZE_MASK;
		switch (size) {
		case VMM_HUGETLB_2MB:
			pte_flags |= ARCH_PTE_FLAG_HUGETLB_2MB;
			break;
		case VMM_HUGETLB_1GB:
			pte_flags |= ARCH_PTE_FLAG_HUGETLB_1GB;
			break;
		default:
			bug("Invalid page size flag"); /* Already checked earlier, should not happen */
		}
	}

	if (prot & PGPROT_READ)
		pte_flags |= ARCH_PTE_FLAG_READ;
	if (prot & PGPROT_WRITE)
		pte_flags |= ARCH_PTE_FLAG_WRITE;
	if (prot & PGPROT_USER)
		pte_flags |= ARCH_PTE_FLAG_USER;
	if (prot & PGPROT_EXEC)
		pte_flags |= ARCH_PTE_FLAG_EXEC;

	return pte_flags;
}

/* Look up a page by address and add a reference to it if it exists */
static int hold_page_address(physaddr_t physical, struct page** page, int flags) {
	struct page* tmp;
	int err = get_page_from_address(physical, &tmp); /* Gives page with a ref added */
	if (err == 0) {
		if (flags & VMM_IOMEM && !(tmp->flags & PAGE_FLAG_RESERVED)) {
			release_page(tmp);
			err = -EACCES;
		} else {
			*page = tmp;
		}
	}
	return err;
}

static struct page* get_page_release_lookup_ref(physaddr_t physical) {
	struct page* page = NULL;
	int err = get_page_from_address(physical, &page);
	if (err != 0)
		bug(err == -EACCES); /* Mapped with zero refs, very bad thing!! :D */
	else
		release_page(page); /* Release lookup ref */
	return page;
}

size_t vm_get_page_size_from_flags(int vmm_flags) {
	if (!(vmm_flags & VMM_HUGETLB))
		return PAGE_SIZE;
	int size = vmm_flags & VMM_HUGETLB_SIZE_MASK;
	switch (size) {
	case VMM_HUGETLB_2MB:
		return VMM_HUGETLB_2MB_SIZE;
	case VMM_HUGETLB_1GB:
		return VMM_HUGETLB_1GB_SIZE;
	}
	return 0;
}

static int get_flags_from_page_size(size_t page_size) {
	if (page_size == PAGE_SIZE)
		return 0;

	switch (page_size) {
	case VMM_HUGETLB_2MB_SIZE:
		return VMM_HUGETLB | VMM_HUGETLB_2MB;
	case VMM_HUGETLB_1GB_SIZE:
		return VMM_HUGETLB | VMM_HUGETLB_1GB;
	}

	dump_stack();
	printk(PRINTK_CRIT "mm: Invalid page size %zu in %s()", page_size, __func__);
	return 0;
}

/* Unmap a page, with an optional page argument to release the page without a lookup */
static void unmap_page(struct tlb_batch* batch, struct page* page, uintptr_t virtual, size_t* page_size) {
	*page_size = 0;

	physaddr_t physical;
	const int err = arch_pagetable_get_physical(batch->pagetable, virtual, &physical);
	if (err)
		return;

	bug(arch_pagetable_unmap(batch, virtual, page_size) != 0); /* arch_pagetable_get_physical() worked, so this should not fail */

	if (!page)
		page = get_page_release_lookup_ref(physical);
	tlb_batch_add(batch, virtual, page);
}

/* Unmap a range of pages, does NOT optimize pfndb lookups */
static inline void unmap_pages(struct tlb_batch* batch, uintptr_t virtual, size_t size) {
	const uintptr_t end = virtual + size;
	while (virtual < end) {
		size_t page_size;
		unmap_page(batch, NULL, virtual, &page_size);
		if (page_size == 0)
			page_size = PAGE_SIZE;
		if (virtual % page_size)
			virtual = ROUND_DOWN(virtual, page_size);
		virtual += page_size;
	}
}

struct map_page_arg {
	bool use_page;
	union {
		struct page* page;
		physaddr_t physaddr;
	} un;
};

struct map_pages_arg {
	size_t page_count;
	bool use_pages;
	union {
		struct page** pages;
		physaddr_t physaddr;
	} un;
};

/*
 * Map a page, either a direct physical address or a struct page*, if mapping a physical address,
 * it attempts to hold the page associated with the address if it exists
 */
static int map_page(struct tlb_batch* batch, uintptr_t virtual, const struct map_page_arg* arg, arch_pte_flags_t pte_flags, int vmm_flags) {
	struct page* page;
	physaddr_t physical;

	if (arg->use_page) {
		page = arg->un.page;
		physical = page_to_physaddr(page);
		hold_page(page);
	} else {
		physical = arg->un.physaddr;
		const int err = hold_page_address(physical, &page, vmm_flags);
		if (err) {
			if (err == -EACCES)
				return err;

			/* -ENOMEM means that the physical address is not covered by pfndb, so there is nothing to reference */
			bug(err != -ENOMEM);
			page = NULL;
		}
	}

	const int err = arch_pagetable_map(batch, virtual, physical, pte_flags);
	if (err) {
		if (page)
			release_page(page);
		return err;
	}

	/* Invalidate just in case */
	tlb_batch_add(batch, virtual, NULL);
	return 0;
}

static int map_pages(struct tlb_batch* batch, uintptr_t virtual, const struct map_pages_arg* arg, pgprot_t prot, int vmm_flags) {
	const arch_pte_flags_t pte_flags = get_arch_pte_flags(prot, vmm_flags);
	const size_t page_size = vm_get_page_size_from_flags(vmm_flags);
	for (size_t mapped_pages = 0; mapped_pages < arg->page_count; mapped_pages++) {
		struct map_page_arg map_page_arg;
		map_page_arg.use_page = arg->use_pages;
		if (arg->use_pages) {
			map_page_arg.un.page = arg->un.pages[mapped_pages];
			if (!map_page_arg.un.page)
				continue; /* Guard page */
		} else {
			map_page_arg.un.physaddr = arg->un.physaddr + mapped_pages * page_size;
		}

		const int err = map_page(batch, virtual + mapped_pages * page_size, &map_page_arg, pte_flags, vmm_flags);
		if (err) {
			size_t unmapped_page_size;
			for (size_t i = 0; i < mapped_pages; i++) {
				const uintptr_t page_virtual = virtual + i * page_size;
				if (arg->use_pages)
					unmap_page(batch, arg->un.pages[i], page_virtual, &unmapped_page_size);
				else
					unmap_page(batch, NULL, page_virtual, &unmapped_page_size);
				bug(unmapped_page_size != page_size);
			}
			return err;
		}
	}

	return 0;
}

void vm_pagetable_teardown_leaf(physaddr_t address) {
	struct page* page = get_page_release_lookup_ref(address);
	if (page)
		release_page(page);
}

static void protect_pages(struct tlb_batch* batch, uintptr_t virtual, size_t count, pgprot_t prot, int vmm_flags) {
	const arch_pte_flags_t pte_flags = get_arch_pte_flags(prot, vmm_flags);
	const size_t page_size = vm_get_page_size_from_flags(vmm_flags);
	for (size_t i = 0; i < count; i++) {
		const uintptr_t page_virtual = virtual + i * page_size;
		physaddr_t physical;
		const int err = arch_pagetable_get_physical(batch->pagetable, page_virtual, &physical);
		if (err)
			continue;

		bug(arch_pagetable_update(batch, page_virtual, physical, pte_flags) != 0);
		tlb_batch_add(batch, page_virtual, NULL);
	}
}

static void vma_unmap_force(struct mm* mm, uintptr_t virtual, size_t size) {
	int err;
	do {
		err = vma_unmap(mm, virtual, size, 0);
		if (err == -ENOMEM)
			out_of_memory();
	} while (err == -ENOMEM);

	if (err)
		panic("%s() failed: %d", __func__, err);
}

static struct mm kernel_mm_struct = {
	.pagetable = NULL,
	.vma_list = LIST_HEAD_INITIALIZER(kernel_mm_struct.vma_list),
	.vma_rbtree = RBTREE_INITIALIZER,
	.mmap = { .start = KERNEL_SPACE_START, .end = KERNEL_SPACE_END },
	.stack = { .start = KERNEL_SPACE_START, .end = KERNEL_SPACE_END },
	.mutex = MUTEX_INITIALIZER(kernel_mm_struct.mutex)
};

struct mm* current_mm(void) {
	unsigned long flags = local_irq_save();
	struct mm* ret = current_cpu()->mm_struct;
	local_irq_restore(flags);
	return ret;
}

struct mm* mm_create(void) {
	struct mm* mm = kmalloc(sizeof(*mm), MM_ZONE_NORMAL);
	if (!mm)
		return NULL;

	mm->pagetable = arch_pagetable_new();
	if (!mm->pagetable)
		return NULL;

	list_head_init(&mm->vma_list);
	rbtree_init(&mm->vma_rbtree);

	size_t mmap_start = ARCH_USER_SPACE_START + 0x400000;
	size_t mmap_end = ARCH_USER_SPACE_END - 0x800000;
	mm->mmap = (struct vmm_range){ .start = mmap_start, .end = mmap_end };
	mm->stack = (struct vmm_range){ .start = mmap_end, .end = ARCH_USER_SPACE_END };
	mutex_init(&mm->mutex);

	return mm;
}

void mm_destroy(struct mm* mm) {
	arch_pagetable_free(mm->pagetable);
	vma_destroy(&mm->vma_list);
	kfree(mm);
}

void mm_switch_context(struct mm* mm) {
	unsigned long irq_flags = local_irq_save();
	current_cpu()->mm_struct = mm;
	current_thread()->mm_struct = mm;
	arch_pagetable_switch(mm->pagetable);
	local_irq_restore(irq_flags);
}

static int __vm_map(struct mm* mm, uintptr_t hint, const struct map_pages_arg* arg, pgprot_t prot, int vmm_flags, uintptr_t* out) {
	const size_t page_size = vm_get_page_size_from_flags(vmm_flags);
	if (page_size == 0)
		return -EINVAL;
	size_t vma_size;
	if (__builtin_mul_overflow(page_size, arg->page_count, &vma_size))
		return -ERANGE;

	struct tlb_batch tlb_batch;
	tlb_batch_init(&tlb_batch, mm->pagetable);

	mutex_acquire(&mm->mutex);

	uintptr_t virtual;
	int err = vma_map(mm, hint, vma_size, prot, vmm_flags, &virtual);
	if (err)
		goto out;
	if (arg->use_pages) {
		for (size_t i = 0; i < arg->page_count; i++) {
			if (arg->un.pages[i])
				continue;
			const uintptr_t page_virtual = virtual + i * page_size;
			err = vma_update(mm, page_virtual, page_size, PGPROT_NONE, 0);
			if (unlikely(err)) {
				vma_unmap_force(mm, virtual, vma_size);
				goto out;
			}
		}
	}

	if ((vmm_flags & (VMM_FIXED | VMM_NOREPLACE)) == VMM_FIXED) {
		unmap_pages(&tlb_batch, virtual, vma_size);
		tlb_batch_flush(&tlb_batch);
	}

	err = map_pages(&tlb_batch, virtual, arg, prot, vmm_flags);
	if (unlikely(err))
		vma_unmap_force(mm, virtual, vma_size);

	tlb_batch_flush(&tlb_batch);
out:
	mutex_release(&mm->mutex);
	if (err == 0)
		*out = virtual;
	return err;
}

static int __vm_protect(struct mm* mm, uintptr_t address, size_t size, pgprot_t prot, int vmm_flags) {
	uintptr_t end;
	if (__builtin_add_overflow(address, size, &end))
		return -ERANGE;

	mutex_acquire(&mm->mutex);

	int err = vma_update(mm, address, size, prot, vmm_flags);
	if (err == 0) {
		struct tlb_batch tlb_batch;
		tlb_batch_init(&tlb_batch, mm->pagetable);

		/* Protect() is a litte bit special: Basically if there is any page size mismatch, protect_pages() needs to know, otherwise -EEXIST may be returned by the arch layer */
		struct vm_area* vma = vma_lookup(mm, address);
		while (vma && vma->start < end) {
			const uintptr_t protect_start = (vma->start > address) ? vma->start : address;
			const uintptr_t protect_end = (vma->end < end) ? vma->end : end;
			protect_pages(&tlb_batch, protect_start, (protect_end - protect_start) / vma->page_size , prot, vmm_flags | get_flags_from_page_size(vma->page_size));
			vma = vma_next(mm, vma);
		}

		tlb_batch_flush(&tlb_batch);
	}

	mutex_release(&mm->mutex);
	return err;
}

static int __vm_unmap(struct mm* mm, uintptr_t address, size_t size, int vmm_flags) {
	mutex_acquire(&mm->mutex);

	int err = vma_unmap(mm, address, size, vmm_flags);
	if (err == 0) {
		struct tlb_batch tlb_batch;
		tlb_batch_init(&tlb_batch, mm->pagetable);
		unmap_pages(&tlb_batch, address, size);
		tlb_batch_flush(&tlb_batch);
	}

	mutex_release(&mm->mutex);
	return err;
}

static struct page** vm_alloc_pages(size_t page_count, unsigned int order) {
	struct page** pages = kcalloc(page_count, sizeof(*pages), MM_ZONE_NORMAL);
	if (!pages)
		return NULL;
	for (size_t i = 0; i < page_count; i++) {
		pages[i] = alloc_pages(MM_ZONE_NORMAL, order);
		if (!pages[i]) {
			for (size_t j = 0; j < i; j++)
				release_page(pages[j]);
			kfree(pages);
			return NULL;
		}
		memset(page_hhdm_virtual(pages[i]), 0, PAGE_SIZE);
	}

	return pages;
}

static inline void vm_release_phys_pages(struct page** pages, size_t page_count) {
	if (pages) {
		for (size_t i = 0; i < page_count; i++)
			release_page(pages[i]);
		kfree(pages);
	}
}

void* vm_map(void* hint, size_t size, pgprot_t prot, int vmm_flags, void* opt) {
	const size_t page_size = vm_get_page_size_from_flags(vmm_flags);
	if (page_size == 0 || (prot & PGPROT_USER) || (vmm_flags & VMM_IOMEM))
		return ERR_PTR(-EINVAL);

	if (size >= SIZE_MAX - page_size)
		return ERR_PTR(-ERANGE);
	size = ROUND_UP(size, page_size);
	const size_t page_count = size / page_size;

	struct page** pages = NULL;
	if (!(vmm_flags & VMM_PHYSICAL)) {
		pages = vm_alloc_pages(page_count, get_order(page_size));
		if (!pages)
			return ERR_PTR(-ENOMEM);
	} else if (!opt) {
		return ERR_PTR(-EINVAL);
	}

	struct map_pages_arg arg;
	arg.page_count = size / page_size;
	arg.use_pages = !!pages;
	if (pages)
		arg.un.pages = pages;
	else
		arg.un.physaddr = *(physaddr_t*)opt;

	uintptr_t ret;
	int err = __vm_map(&kernel_mm_struct, (uintptr_t)hint, &arg, prot, vmm_flags, &ret);
	if (pages)
		vm_release_phys_pages(pages, page_count);

	return err == 0 ? (void*)ret : ERR_PTR(err);
}

void* vm_map_pages(void* hint, struct page** pages, size_t page_count, pgprot_t prot, int vmm_flags) {
	if ((prot & PGPROT_USER) || (vmm_flags & VMM_IOMEM))
		return ERR_PTR(-EINVAL);
	const struct map_pages_arg arg = { .page_count = page_count, .use_pages = true, .un.pages = pages };
	uintptr_t ret;
	int err = __vm_map(&kernel_mm_struct, (uintptr_t)hint, &arg, prot, vmm_flags, &ret);
	return (err == 0) ? (void*)ret : ERR_PTR(err);
}

int vm_protect(void* address, size_t size, pgprot_t prot, int vmm_flags) {
	return __vm_protect(&kernel_mm_struct, (uintptr_t)address, size, prot, vmm_flags);
}

int vm_unmap(void* address, size_t size, int vmm_flags) {
	return __vm_unmap(&kernel_mm_struct, (uintptr_t)address, size, vmm_flags);
}

void __user* vm_map_user(void __user* hint, size_t size, pgprot_t prot, int vmm_flags, void* opt) {
	if (vmm_flags & (VMM_IOMEM | VMM_PHYSICAL) || opt != NULL) /* opt doesn't take anything right now */
		return ERR_PTR_AS(void __user*, -EINVAL);

	const size_t page_size = vm_get_page_size_from_flags(vmm_flags);
	if (size >= SIZE_MAX - page_size)
		return ERR_PTR_AS(void __user*, -ERANGE);
	size = ROUND_UP(size, page_size);
	const size_t page_count = size / page_size;

	struct mm* mm = current_mm();
	if (mm == &kernel_mm_struct)
		return ERR_PTR_AS(void __user*, -ESRCH);

	struct page** pages = vm_alloc_pages(page_count, get_order(page_size));
	if (!pages)
		return ERR_PTR_AS(void __user*, -ENOMEM);

	const struct map_pages_arg arg = { .page_count = page_count, .use_pages = true, .un.pages = pages };
	uintptr_t ret;
	int err = __vm_map(mm, (uintptr_t)hint, &arg, prot, vmm_flags, &ret);

	vm_release_phys_pages(pages, page_count);
	return (err == 0) ? (void __user*)ret : ERR_PTR_AS(void __user*, err);
}

int vm_protect_user(void __user* virtual, size_t size, pgprot_t prot, int vmm_flags) {
	struct mm* mm = current_mm();
	if (mm == &kernel_mm_struct)
		return -ESRCH;
	return __vm_protect(mm, (uintptr_t)virtual, size, prot, vmm_flags);
}

int vm_unmap_user(void __user* virtual, size_t size, int vmm_flags) {
	struct mm* mm = current_mm();
	if (mm == &kernel_mm_struct)
		return -ESRCH;
	return __vm_unmap(mm, (uintptr_t)virtual, size, vmm_flags);
}

void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t caching_mode) {
	if ((caching_mode & ~PGPROT_CACHE_MASK) || caching_mode == PGPROT_WB)
		return ERR_PTR_AS(void __iomem*, -EINVAL);

	const size_t page_offset = physical % PAGE_SIZE;
	size = ROUND_UP(size + page_offset, PAGE_SIZE);

	const struct map_pages_arg arg = { .page_count = size >> PAGE_SHIFT, .use_pages = false, .un.physaddr = physical };
	uintptr_t ret;
	int err = __vm_map(&kernel_mm_struct, 0, &arg, PGPROT_READ | PGPROT_WRITE | caching_mode, VMM_IOMEM, &ret);
	if (err)
		return ERR_PTR_AS(void __iomem*, err);
	return (u8 __iomem*)ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	const size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	size = ROUND_UP(size + page_offset, PAGE_SIZE);
	return __vm_unmap(&kernel_mm_struct, (uintptr_t)virtual - page_offset, size, 0);
}

void vm_unmap_force(void* virtual, size_t size, int vmm_flags) {
	int err;
	do {
		err = vm_unmap(virtual, size, vmm_flags);
		if (err == -ENOMEM)
			out_of_memory();
	} while (err == -ENOMEM);

	if (err != 0)
		panic("%s() failed: %d\n", __func__, err);
}

struct vmalloc_node {
	void* address;
	size_t page_count, guard_page_count;
	struct rbtree_node rbtree_link;
};

/* TODO: Change this to not use a single mutex */
static RBTREE_DEFINE(vmalloc_tree);
static MUTEX_DEFINE(vmalloc_tree_mtx);

static int __insert_node(struct vmalloc_node* node) {
	struct rbtree_node** link = &vmalloc_tree.root;
	struct rbtree_node* parent = NULL;
	while (*link) {
		struct vmalloc_node* other = container_of(*link, struct vmalloc_node, rbtree_link);
		parent = *link;
		if (node->address < other->address)
			link = &parent->left;
		else if (node->address > other->address)
			link = &parent->right;
		else
			return -EEXIST;
	}

	rbtree_insert(&node->rbtree_link, parent, link);
	rbtree_insert_fixup(&vmalloc_tree, &node->rbtree_link);

	return 0;
}

static struct vmalloc_node* __get_node_and_unlink(void* address) {
	struct rbtree_node* link = vmalloc_tree.root;
	while (link) {
		struct vmalloc_node* node = container_of(link, struct vmalloc_node, rbtree_link);
		if (address == node->address) {
			rbtree_remove(&vmalloc_tree, link);
			return node;
		}
		link = address < node->address ? link->left : link->right;
	}
	return NULL;
}

static inline int insert_node(struct vmalloc_node* node) {
	mutex_acquire(&vmalloc_tree_mtx);
	int ret = __insert_node(node);
	mutex_release(&vmalloc_tree_mtx);
	return ret;
}

static inline struct vmalloc_node* get_node_and_unlink(void* address) {
	mutex_acquire(&vmalloc_tree_mtx);
	struct vmalloc_node* ret = __get_node_and_unlink(address);
	mutex_release(&vmalloc_tree_mtx);
	return ret;
}

void* vmalloc(size_t size) {
	if (size >= SIZE_MAX - PAGE_SIZE)
		return NULL;
	size = ROUND_UP(size, PAGE_SIZE);

	const size_t guard_page_count = 1;
	const size_t page_count = size >> PAGE_SHIFT;
	if (page_count == 0)
		return NULL;

	struct page** const pages = kcalloc(page_count + guard_page_count, sizeof(*pages), MM_ZONE_NORMAL);
	if (!pages)
		return NULL;
	void* ret = NULL;
	struct vmalloc_node* node = kmalloc(sizeof(*node), MM_ZONE_NORMAL);
	if (!node)
		goto out;
	for (size_t i = 0; i < page_count; i++) {
		pages[i] = alloc_page(MM_ZONE_NORMAL);
		if (!pages[i])
			goto out;
	}

	/* When vm_map_pages() encounters null on the last page, it will just reserve the VA with no permissions */
	ret = vm_map_pages(NULL, pages, page_count + guard_page_count, PGPROT_READ | PGPROT_WRITE, 0);
	if (IS_PTR_ERR(ret)) {
		ret = NULL;
		goto out;
	}

	node->address = ret;
	node->page_count = page_count;
	node->guard_page_count = guard_page_count;
	rbtree_node_init(&node->rbtree_link);

	bug(insert_node(node) != 0);
	node = NULL; /* Prevent kfree() from freeing the node on success */
out:
	/* Now drop this function's ref to the pages, on failure these pages will be released back to the allocator */
	for (size_t i = 0; i < page_count && pages[i] != NULL; i++)
		release_page(pages[i]);

	kfree(node);
	kfree(pages);

	return ret;
}

void* vrealloc(void* ptr, size_t size) {
	if (!ptr)
		return vmalloc(size);
	if (size >= SIZE_MAX - PAGE_SIZE || size == 0)
		return NULL;

	struct vmalloc_node* const node = get_node_and_unlink(ptr);
	if (!node) {
		dump_stack();
		printk(PRINTK_ERR "mm: %s() invalid address\n", __func__);
		return NULL;
	}

	size = ROUND_UP(size, PAGE_SIZE);
	void* const ret = vmalloc(size);
	if (!ret) {
		bug(insert_node(node) != 0);
		return NULL;
	}

	const size_t page_count = size >> PAGE_SHIFT;
	const size_t copy_count = (node->page_count > page_count) ? page_count << PAGE_SHIFT : node->page_count << PAGE_SHIFT;
	memcpy(ret, ptr, copy_count);

	vm_unmap_force(node->address, (node->page_count + node->guard_page_count) << PAGE_SHIFT, 0);
	kfree(node);
	return ret;
}

void vfree(void* ptr) {
	if (!ptr)
		return;

	struct vmalloc_node* const node = get_node_and_unlink(ptr);
	if (!node) {
		dump_stack();
		printk(PRINTK_ERR "mm: %s() invalid address\n", __func__);
		return;
	}

	vm_unmap_force(node->address, (node->page_count + node->guard_page_count) << PAGE_SHIFT, 0);
	kfree(node);
}

static void vmm_init(void) {
	struct mm* mm = &kernel_mm_struct;
	mm->pagetable = arch_pagetable_get_cpu_current();
	current_cpu()->mm_struct = mm;

	/* Give HHDM VMA's that can't be changed */
	uintptr_t _unused;
	uintptr_t next;
	for (uintptr_t addr = KERNEL_SPACE_START; addr < KERNEL_SPACE_END; addr = next) {
		size_t page_size = arch_pagetable_iterate_range(mm->pagetable, addr, &next);
		if (page_size != 0) {
			int err = vma_map(mm, addr, page_size, PGPROT_READ | PGPROT_WRITE, VMM_FIXED | VMM_NOREPLACE | VMM_SEALED | get_flags_from_page_size(page_size), &_unused);
			if (err == -ENOMEM)
				out_of_memory();
			else
				bug(err != 0);
		}
	}
}

static void vmm_ap_init(void) {
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	arch_pagetable_switch(cpu->mm_struct->pagetable);
}

INIT_TASK_DECLARE(vma_init_task, hhdm_init_task, zones_init_task, arch_pagetable_init_task, arch_pagetable_ap_init_task);
INIT_TASK_DEFINE(vmm_init_task, INIT_TASK_SCOPE_BSP, vmm_init, &vma_init_task, &hhdm_init_task, &zones_init_task, &arch_pagetable_init_task);
INIT_TASK_DEFINE(vmm_ap_init_task, INIT_TASK_SCOPE_AP, vmm_ap_init, &vmm_init_task, &arch_pagetable_ap_init_task);
