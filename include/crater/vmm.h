#pragma once

#include <crater/mm.h>
#include <crater/compiler.h>

#define VMM_PHYSICAL (1 << 0) /* Used in vm_map() to map directly to a physical address */
#define VMM_FIXED (1 << 1) /* Address hints are no longer hints */
#define VMM_NOREPLACE (1 << 2) /* Do not replace mappings when using VMM_FIXED, instead return -EEXIST when this occurs */
#define VMM_HUGETLB (1 << 3) /* Use hugepages */
#define VMM_HUGETLB_2MB (0b01 << 4)
#define VMM_HUGETLB_1GB (0b10 << 4)
#define VMM_SEALED (1 << 6) /* Do not allow changes to a mapping, throughout the life of the program */
#define VMM_STACK (1 << 7) /* The mapping is for a stack */
#define VMM_IOMEM (1 << 8) /* The mapping is for MMIO */

#define VMM_HUGETLB_SIZE_MASK (VMM_HUGETLB_2MB | VMM_HUGETLB_1GB)
#define VMM_ALL_MASK (VMM_PHYSICAL | VMM_FIXED | VMM_NOREPLACE | VMM_HUGETLB | VMM_HUGETLB_SIZE_MASK | VMM_SEALED | VMM_STACK | VMM_IOMEM)

#define VMM_HUGETLB_2MB_SIZE 0x200000
#define VMM_HUGETLB_1GB_SIZE 0x40000000

/**
 * @brief Get the CPU's MM struct
 * @return The pointer to the MM struct
 */
struct mm* current_mm(void);

/**
 * @brief Called during page table teardown when destroying a page table
 *
 * Whenever the page table walker encounters a leaf mapping, the function calls this function to
 * release the page.
 *
 * @param address The address to tear down
 */
void vm_pagetable_teardown_leaf(physaddr_t address);

/**
 * @brief Map pages into virtual memory
 *
 * @param hint Hint on where to place the mapping
 * @param size The size of the mapping, automatically rounded to the requested page size
 * @param prot Page protection flags
 * @param vmm_flags VMM_* flags
 * @param opt An optional argument based on the VMM flags. When VMM_PHYSICAL is used, this should point to a physaddr_t
 *
 * @retval -EINVAL Hint is invalid (when using VMM_FIXED), size is zero, bad pgprot, bad vmm_flags, or optional argument is not provided when needed
 * @retval -EOPNOTSUPP An operation (usually related to page sizes) is not supported
 * @retval -ENOMEM Out of memory
 * @return A pointer to the memory, or -errno that can be checked with the IS_PTR_ERR()/PTR_ERR() macro
 */
void* vm_map(void* hint, size_t size, pgprot_t prot, int vmm_flags, void* opt);

/**
 * @brief Map pages into virtual memory
 *
 * Guard pages can be made by having NULL pages in the page array.
 * When protecting or unmapping, make sure you use the size of the mapping, not the page count.
 *
 * @param hint Hint on where to place the mapping
 * @param pages The array of pages to map
 * @param page_count Number of pages in the page array
 * @param prot Page protection flags
 * @param vmm_flags VMM_* flags
 *
 * @return A pointer to the mapped memory, or -errno on failure
 */
void* vm_map_pages(void* hint, struct page** pages, size_t page_count, pgprot_t prot, int vmm_flags);

/**
 * @brief Change the page protection flags on a mapping
 *
 * The address + size must be fully covered by a VMA.
 * If a VMA cannot be split because of page sizes, -EINVAL is returned.
 *
 * @param address The address of the mapping
 * @param size The size to protect
 * @param prot The new page protection flags
 * @param vmm_flags VMM_* flags
 *
 * @retval -EINVAL Invalid flags, or misaligned address
 * @retval -ENOMEM Out of memory, usually happening when failing to split a VMA.
 * @retval -ENOENT Address + size not fully covered by a VMA, should not be returned to userspace (return ENOMEM instead)
 * @return 0 on success, -errno on failure
 */
int vm_protect(void* address, size_t size, pgprot_t prot, int vmm_flags);

/**
 * @brief Unmap a virtual address range
 *
 * Unlike vm_protect, address + size does NOT need to be fully covered by a VMA.
 * If a VMA cannot be split because of page sizes, -EINVAL is returned.
 *
 * @param address The address to unmap
 * @param size The size to unmap
 * @param vmm_flags VMM_* flags
 *
 * @retval -EINVAL Invalid flags, or misaligned address
 * @retval -ENOMEM Out of memory (if failing to split a VMA)
 * @return 0 on success, -errno on failure
 */
int vm_unmap(void* address, size_t size, int vmm_flags);

/**
 * @brief Unmap a virtual address range, but return no errors
 *
 * On failure, this causes a kernel panic. The -ENOMEM case is handled,
 * so being out of memory is not nessecarily kernel panic.
 *
 * @param virtual The virtual address to unmap
 * @param size The size to unmap
 * @param vmm_flags VMM_* flags
 */
void vm_unmap_force(void* virtual, size_t size, int vmm_flags);

/**
 * @brief Map pages into user space
 *
 * @param hint A hint on where to place the mapping
 * @param size The size to unmap
 * @param prot Page protection flags
 * @param vmm_flags VMM_* flags
 * @param opt Optional argument based on VMM flags
 *
 * @see vm_map()
 *
 * @return A pointer in userspace to the memory, or -errno on failure
 */
void __user* vm_map_user(void __user* hint, size_t size, pgprot_t prot, int vmm_flags, void* opt);

/**
 * @brief Protect pages in user space
 *
 * @param virtual The address to protect
 * @param size The number of bytes to protect
 * @param prot The new page protection flags
 * @param vmm_flags VMM_* flags
 *
 * @see vm_protect()
 *
 * @return 0 on success, -errno on failure
 */
int vm_protect_user(void __user* virtual, size_t size, pgprot_t prot, int vmm_flags);

/**
 * @brief Unmap pages in user space
 *
 * @param virtual The virtual address to unmap
 * @param size The size to unmap
 * @param vmm_flags VMM_* flags
 *
 * @see vm_unmap()
 *
 * @return 0 on success, -errno on failure
 */
int vm_unmap_user(void __user* virtual, size_t size, int vmm_flags);

/**
 * @brief Map I/O memory
 *
 * @param physical The physical address, can be misaligned
 * @param size Size of the mapping
 * @param caching_mode Caching mode for the pages
 *
 * @see vm_map()
 *
 * @return A pointer to the memory including the alignment, or -errno on failure.
 */
void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t caching_mode);

/**
 * @brief Unmap I/O memory
 *
 * @param virtual The virtual address
 * @param size The size to unmap
 *
 * @see vm_unmap()
 *
 * @return 0 on success, -errno on failure
 */
int iounmap(void __iomem* virtual, size_t size);

/**
 * @brief Allocate memory using the VMM
 *
 * This function allocates virtually contiguous memory. A guard page is placed at the end.
 *
 * @param size The size to allocate
 * @return A pointer to the memory
 */
void* vmalloc(size_t size);

/**
 * @brief Re-allocate memory allocated by vmalloc()
 *
 * @param ptr The original pointer
 * @param size The new size
 *
 * @return A pointer to the new block of memory
 */
void* vrealloc(void* ptr, size_t size);

/**
 * @brief Free memory allocated with vmalloc()
 * @param ptr The pointer to free
 */
void vfree(void* ptr);

/**
 * @brief Enable TLB shootdowns
 *
 * Called only by the BSP
 */
void tlb_shootdown_init(void);
