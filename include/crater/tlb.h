#pragma once

#include <crater/page.h>
#include <crater/list.h>

#define TLB_BATCH_PAGE_COUNT 32

struct tlb_batch {
	arch_pte_t* pagetable;
	uintptr_t first_page_virtual, last_page_virtual;
	size_t page_count; /* Number of pages in the pages array */
	struct page* pages[TLB_BATCH_PAGE_COUNT]; /* Since multiple addresses may map to the same page, we cannot use a list here */
	struct list_head page_tables_list;
};

/**
 * @brief Initialize a TLB batch structure
 *
 * @param batch The batch to initialize
 * @param pagetable The page table
 */
void tlb_batch_init(struct tlb_batch* batch, arch_pte_t* pagetable);

/**
 * @brief Flush TLB entries for a TLB batch structure
 *
 * After this function, any page structures associated with this batch will be released.
 * Not safe to call from an atomic context, as this may acquire mutexes.
 *
 * @param batch The batch to flush
 */
void tlb_batch_flush(struct tlb_batch* batch);

/**
 * @brief Free page tables in a TLB batch
 *
 * tlb_batch_flush() must be called before this function.
 *
 * @param batch The batch to use
 */
void tlb_batch_free_tables(struct tlb_batch* batch);

/**
 * @brief Add a page to a TLB batch
 *
 * If the number of pages exceeds TLB_BATCH_PAGE_COUNT, this function will call
 * tlb_batch_flush() to allow more pages to be added.
 *
 * @param batch The batch to add to
 * @param virtual The virtual address of the page
 * @param page The page to release after flushing (optional)
 */
void tlb_batch_add(struct tlb_batch* batch, uintptr_t virtual, struct page* page);

/**
 * @brief Add a page table into a TLB batch
 *
 * CPU's may cache paging structures, 
 */
void tlb_batch_add_page_table(struct tlb_batch* batch, struct page* page);
