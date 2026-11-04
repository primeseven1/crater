#pragma once

#include <crater/types.h>

typedef unsigned long arch_pte_t;
typedef enum {
	ARCH_PTE_FLAG_NONE = 0,
	ARCH_PTE_FLAG_READ = (1 << 0),
	ARCH_PTE_FLAG_WRITE = (ARCH_PTE_FLAG_READ | (1 << 1)),
	ARCH_PTE_FLAG_USER = (1 << 2),
	ARCH_PTE_FLAG_WT = (1 << 3),
	ARCH_PTE_FLAG_UC = (1 << 4),
	ARCH_PTE_FLAG_WC = (1 << 5), /* Must be handled using the PAT */
	ARCH_PTE_FLAG_EXEC = (1 << 6), /* Must also be handled differently, since NX is used */
	ARCH_PTE_FLAG_HUGETLB_2MB = (1 << 7), /* Also must be handled differently because of 1GB pages, but happens to be the same as the hugepage bit in the PTE entry */
	ARCH_PTE_FLAG_HUGETLB_1GB = (1 << 8)
} arch_pte_flags_t;
