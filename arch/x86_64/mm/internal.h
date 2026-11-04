#pragma once

#include <crater/errno.h>
#include <arch/context.h>
#include <arch/page.h>

enum pt_flags {
	PT_NONE = ARCH_PTE_FLAG_NONE,
	PT_PRESENT = ARCH_PTE_FLAG_READ,
	PT_READ_WRITE = ARCH_PTE_FLAG_WRITE,
	PT_USER_SUPERVISOR = ARCH_PTE_FLAG_USER,
	PT_WRITETHROUGH = ARCH_PTE_FLAG_WT,
	PT_CACHE_DISABLE = ARCH_PTE_FLAG_UC,
	PT_ACCESSED = (1 << 5),
	PT_DIRTY = (1 << 6),
	PT_4K_PAT = (1 << 7),
	PT_HUGEPAGE = (1 << 7),
	PT_GLOBAL = (1 << 8),
	PT_NULL_MAPPING = (1 << 9),
	PT_HUGEPAGE_PAT = (1 << 12),
	PT_NX = (1ul << 63)
};

enum pat_type {
	PAT_TYPE_UC = 0x00,
	PAT_TYPE_WC = 0x01,
	PAT_TYPE_WT = 0x04,
	PAT_TYPE_WP = 0x05,
	PAT_TYPE_WB = 0x06,
	PAT_TYPE_UC_MINUS = 0x07,
	PAT_TYPE_UNKNOWN = 0xFFFF
};

#define PAT_TYPE_COUNT 8
#define PAT_ENTRY_COUNT 8

/**
 * @brief Get the PT flags from a PAT type
 *
 * @param[in] type The PAT type
 * @param[in] hugepage Whether or not this is for a hugepage
 * @param[out] flags Pointer to where to caching flags will be stored
 *
 * @retval -ENOSYS PAT does not exist
 * @retval -ENOTSUP Pat type not supported
 * @retval 0 Successful
 */
int pat_type_to_pt_flags(enum pat_type type, bool hugepage, enum pt_flags* flags);

/**
 * @brief Fix up a fault in a user copy context
 * @param context The context
 * @retval true The fault was handled
 * @retval false The fault was not handled
 */
bool usercopy_context_fixup_fault(struct arch_context* context);
