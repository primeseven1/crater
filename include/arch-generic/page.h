#pragma once

#include <arch/pte_types.h>

struct tlb_batch;

/**
 * @brief Create a new page table
 *
 * The kernel page tables must be copied into the returned page table.
 *
 * @return A pointer to the new table
 */
arch_pte_t* arch_pagetable_new(void);

/**
 * @brief Free a page table
 * @param table The table to free
 */
void arch_pagetable_free(arch_pte_t* table);

/**
 * @brief Add a PTE into a page table
 *
 * @param tlb_batch The TLB batch to use
 * @param virtual The virtual address to map
 * @param physical The physical address to map
 * @param pte_flags The PTE flags to apply to the PTE
 *
 * @retval -EINVAL The virtual or physical addresses are not aligned to the requested page size, or invalid combinations of PTE flags. May also be returned for other reasons (eg. Non-canonical addresses).
 * @retval -EEXIST Page table entry already exists
 * @retval -EOPNOTSUPP A specific operation is recognized but not supported (eg. A page size that is not supported)
 * @retval -ENOMEM Out of memory (like failing to allocate a page table)
 * @return 0 on success, -errno on failure. This function may return more errno's than the ones documented.
 */
int arch_pagetable_map(struct tlb_batch* tlb_batch, uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags);

/**
 * @brief Update a PTE
 *
 * Nearly identical to arch_pagetable_map(), but doesn't allocate new page tables
 *
 * @param tlb_batch The TLB batch to use 
 * @param virtual The virtual address to update
 * @param physical The physical address to map
 * @param pte_flags The PTE flags to apply to the PTE
 *
 * @retval -EINVAL Same as arch_pagetable_map(), invalid virutal or physical address or PTE flags.
 * @retval -ENOENT Intermediate PTE does not exist, or page is unmapped.
 * @retval -EEXIST Unlike arch_pagetable_map(), this can happen when the requested page size does not match the PTE.
 * @retval -EOPNOTSUPP Same as arch_pagetable_map(), operation is recognized but not supported (like when requesting a page size not supported by the processor)
 * @return 0 On success, -errno on failure.
 */
int arch_pagetable_update(struct tlb_batch* tlb_batch, uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags);

/**
 * @brief Remove a PTE
 *
 * When the value of *page_size is zero, it means you do not care about the page size being unmapped.
 * In this case, the size of the page being unmapped is written back to *page_size. Virtual address must
 * still be aligned by PAGE_SIZE, but does not need to be aligned by the page size of the PTE if *page_size is zero.
 *
 * @param[in] tlb_batch The TLB batch to use
 * @param[in] virtual The virtual address to unmap
 * @param[in,out] page_size Requested page size to unmap, or zero.
 *
 * @retval -EINVAL Virtual address is not aligned by *page_size
 * @retval -EOPNOTSUPP Same as arch_pagetable_map(), usually when a page size is unsupported.
 * @retval -ENOENT Page is unmapped, or intermediate PTE does not exist
 * @retval -EEXIST *page_size does not equal the page size of the PTE.
 * @return 0 on success, -errno on failure.
 */
int arch_pagetable_unmap(struct tlb_batch* tlb_batch, uintptr_t virtual, size_t* page_size);

/**
 * @brief Get the physical address of a PTE
 *
 * The virtual address can be misaligned. The offset into the page is added to the physical address.
 *
 * @param[in] pagetable The page table
 * @param[in] virtual The virtual address
 * @param[out] out Where the physical address is stored
 *
 * @retval -ENOENT PTE does not exist
 * @return 0 on success, -errno on failure
 */
int arch_pagetable_get_physical(arch_pte_t* pagetable, uintptr_t virtual, physaddr_t* out);

/**
 * @brief Get the page table the current CPU is using
 * @return The page table
 */
arch_pte_t* arch_pagetable_get_cpu_current(void);

/**
 * @brief Switch to a different page table
 * @param pagetable The page table to switch to
 */
void arch_pagetable_switch(arch_pte_t* pagetable);

/**
 * @brief Iterate through a page table hierarchy for a virtual address range
 *
 * Can be used to quickly iterate through a page table to see what virtual addresses are mapped.
 *
 * @param[in] pagetable The page table
 * @param[in] virual The virtual address to start at
 * @param[out] Pointer to where to store the next virtual address to check
 *
 * @return The page size, or zero if no page is mapped there
 */
size_t arch_pagetable_iterate_range(arch_pte_t* pagetable, uintptr_t virtual, uintptr_t* next);

/**
 * @brief Check if the CPU suports a page size
 * @param page_size The page size
 * @retval true The page size is supported
 * @retval false The page size is not supported
 */
bool arch_supports_page_size(size_t page_size);
