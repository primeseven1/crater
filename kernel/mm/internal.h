#pragma once

#include <crater/vmm.h>
#include <crater/rbtree.h>

#define VM_AREA_PERSISTENT_FLAGS (VMM_SEALED | VMM_STACK | VMM_IOMEM)

struct vm_area {
	uintptr_t start, end;
	size_t page_size;
	pgprot_t prot;
	int vmm_flags;
	struct rbtree_node rbtree_link;
	struct list_node list_link;
};

size_t vm_get_page_size_from_flags(int vmm_flags);

/**
 * @brief Cover a virtual address range with a VMA
 *
 * @param[in] mm The mm struct to use
 * @param[in] hint Hint on where to place the mapping
 * @param[in] size The size of the mapping
 * @param[in] prot Page protection flags
 * @param[in] vmm_flags VMM_* flags
 * @param[out] out Return value for the address of the mapping
 *
 * @retval -EINVAL Invalid hint (when VMM_FIXED is used) or invalid flag combination
 * @retval -ERANGE hint + size overflows, or size cannot be rounded to a page size, this value should NOT be returned to userspace (return EINVAL instead)
 * @retval -ENOMEM Out of virtual memory address space, or out of physical memory
 * @retval -EEXIST VMM_FIXED and VMM_NOREPLACE was used, but a VMA already exists at the hint
 * @retval -EPERM VMM_FIXED was used, but the VMA already exists AND the VMA has VMM_SEALED applied to it
 * @retval 0 Successful
 */
int vma_map(struct mm* mm, uintptr_t hint, size_t size, pgprot_t prot, int vmm_flags, uintptr_t* out);

/**
 * @brief Update a VMA
 *
 * @param mm The mm struct to use
 * @param address The address to update
 * @param size The size to update
 * @param prot New protection flags
 * @param vmm_flags VMM_* flags
 *
 * @retval -ENOMEM Out of memory
 * @retval -EINVAL Invalid flags
 * @retval -ERANGE address + size overflows, or the size cannot be rounded to a page size, should not be returned to userspace (return EINVAL instead)
 * @retval -ENOENT Part or all of the range is unmapped, should not be returned to userspace (return ENOMEM instead)
 * @retval -EPERM The VMA as VMM_SEALED applied to it
 * @retval 0 Successful
 */
int vma_update(struct mm* mm, uintptr_t address, size_t size, pgprot_t prot, int vmm_flags);

/**
 * @brief Unmap a VMA
 *
 * @param mm The mm struct to use
 * @param address The address to update
 * @param size The size to update
 * @param vmm_flags VMM_* flags
 *
 * @retval -EINVAL Invalid flags
 * @retval -ERANGE address + size overflows, or size could not be rounded to a page size
 * @retval -ENOMEM Out of memory (can happen when splitting a VMA)
 * @retval -EPERM A VMA in the range has VMM_SEALED applied to it
 */
int vma_unmap(struct mm* mm, uintptr_t address, size_t size, int vmm_flags);

/**
 * @brief Look up a VMA by address
 *
 * @param mm The mm struct to use
 * @param address The address to look up
 *
 * @return The pointer to the vm area
 */
struct vm_area* vma_lookup(struct mm* mm, uintptr_t address);

/**
 * @brief Get the next VMA
 *
 * @param mm The mm struct the VMA is in
 * @param vma The VMA
 *
 * @return The next VMA, or NULL if the VMA is the last one
 */
struct vm_area* vma_next(struct mm* mm, struct vm_area* vma);

/**
 * @brief Get the previous VMA
 *
 * @param mm The mm struct
 * @param vma The VMA
 *
 * @return The previous VMA, or NULL is the VMA is the first one
 */
struct vm_area* vma_prev(struct mm* mm, struct vm_area* vma);

/**
 * @brief Free all VMA's in a list
 * @param vma_list The VMA list
 */
void vma_destroy(struct list_head* vma_list);
