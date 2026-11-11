#pragma once

#include <crater/types.h>
#include <crater/list.h>
#include <crater/rbtree.h>
#include <crater/mutex.h>
#include <crater/page.h>

struct vma;

#define PAGE_FLAG_FREE (1 << 0) /* For the buddy allocator to determine if a page is free or not. */
#define PAGE_FLAG_RESERVED (1 << 1) /* Reserved by firmware or kernel. This flag does not apply for bootloader reclaimable pages. This flag is never changed. */
#define PAGE_FLAG_NEED_RECLAIM (1 << 2) /* This makes sure pages aren't accidently reclaimed twice */

struct page {
	int __atomic flags;
	struct {
		struct page* __atomic head;
		unsigned int __atomic order;
	} buddy;
	union {
		struct list_node free_link;
		struct list_node pagetable_tlb_batch_link;
	};
	long __atomic refcnt;
};

struct vmm_range {
	uintptr_t start, end;
};

struct mm {
	arch_pte_t* pagetable;
	struct list_head vma_list; /* struct vma */
	struct rbtree vma_rbtree;
	struct vmm_range mmap, stack;
	mutex_t mutex;
};

/**
 * @brief Get a physical address using HHDM
 * @param virtual The virtual address
 * @return The physical address
 */
physaddr_t hhdm_physical(const void* virtual);

/**
 * @brief Get a virtual address using HHDM
 * @param physical The physical address
 * @return The pointer to the memory
 */
void* hhdm_virtual(physaddr_t physical);

/**
 * @brief Get the base of HHDM
 * @return A pointer to the beginning of HHDM
 */
void* hhdm_base(void);

/**
 * @brief Create a new mm context
 * @return A pointer to the new context
 */
struct mm* mm_create(void);

/**
 * @brief Destroy a mm context
 * @param mm The context to destroy
 */
void mm_destroy(struct mm* mm);

/**
 * @brief Switch to a new mm context
 * @param mm The mm context to switch to
 */
void mm_switch_context(struct mm* mm);

typedef enum {
	MM_ZONE_DMA = (1 << 0), /* Memory less than 16MiB */
	MM_ZONE_DMA32 = (1 << 1), /* Memory less than 4GiB */
	MM_ZONE_NORMAL = (1 << 2), /* Memory above 4GiB */
	MM_NOFAIL = (1 << 3), /* Allocation will never fail */
	MM_ATOMIC = (1 << 4) /* Allocation cannot sleep */
} mm_t;

#define MAX_ORDER 11

static inline unsigned int get_order(size_t size) {
	if (size <= PAGE_SIZE)
		return 0;
	unsigned long pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	return (sizeof(unsigned long) * 8 - __builtin_clzl(pages - 1));
}

/**
 * @brief Get a page struct from an address
 *
 * This is primarily used to get the page structure for a reserved region
 * (eg. ACPI Tables)
 *
 * @param[in] address The address to look up
 * @param[out] page Where the resulting page will be stored
 *
 * @retval 0 Successful
 * @retval -EACCES Page is owned by the allocator
 * @retval -ENOMEM Page is not backed by physical RAM
 */
int get_page_from_address(physaddr_t address, struct page** page);

/**
 * @brief Get memory usage statistics
 * @param[out] total_page_count Number of total pages on the system
 * @param[out] free_page_count Number of free pages
 */
void mm_get_free_pages(size_t* total_page_count, size_t* free_page_count);

/**
 * @brief Allocate physical pages
 *
 * @param mm_flags The flags for how the allocation should be done
 * @param order 2 ^ order pages to allocate (0 for one page, 1 for 2 pages, 2 for 4 pages)
 *
 * @return A pointer to the page struct
 */
struct page* alloc_pages(mm_t mm_flags, unsigned int order);

/**
 * @brief Allocate a physical page
 * @param mm_flags The flags for how the allocation should be done
 * @return The pointer to the page struct
 */
static inline struct page* alloc_page(mm_t mm_flags) {
	return alloc_pages(mm_flags, 0);
}

/**
 * @brief Get the head page of the block containing the page
 * @param page The page to get the head of
 * @return The head of the page
 */
static inline struct page* page_head(const struct page* page) {
	return atomic_load(&page->buddy.head);
}

/**
 * @brief Get the virtual address from a page struct
 * @param page The page
 * @return A HHDM virtual address, returns NULL if page is NULL
 */
void* page_hhdm_virtual(const struct page* page);

/**
 * @brief Increment the refcount on a page
 * @param page The page to hold
 */
void hold_page(struct page* page);

/**
 * @brief Increment the refcount on a page if refcount > 0
 * @param page The page to hold
 * @retval false Failed to hold the page
 * @retval true Successfully held the page
 */
bool try_hold_page(struct page* page);

/**
 * @brief Decrement the refcount on a page
 *
 * May free the page(s) when refcount hits zero.
 *
 * @param page The page to release
 */
void release_page(struct page* page);

/**
 * @brief Reclaim a reclaimable page
 *
 * Adds one reclaimable page into the free list.
 *
 * @param page The page to reclaim
 */
void reclaim_page(struct page* page);

/**
 * @brief Get the physical address of a page
 * @param page The page to get the address of
 * @return The physical address of the page
 */
static inline physaddr_t page_to_physaddr(const struct page* page) {
	return hhdm_physical(page_hhdm_virtual(page));
}

void out_of_memory(void);
