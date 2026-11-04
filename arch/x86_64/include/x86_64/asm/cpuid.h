#pragma once

/* 
 * There are a lot more CPUID leafs than this, so feel free add more if needed.
 *
 * Since a CPUID leaf can have multiple functions, they are defined separately.
 * Use the macro that best defines what you are using it for.
 */

#define CPUID_LEAF_VENDOR_ID 0x00
#define CPUID_LEAF_HIGHEST_FUNCTION 0x00
#define CPUID_LEAF_PROC_INFO 0x01
#define CPUID_LEAF_FEATURE_BITS 0x01
#define CPUID_LEAF_TSC_FREQ 0x15

#define CPUID_EXT_LEAF_HIGHEST_FUNCTION 0x80000000
#define CPUID_EXT_LEAF_PROC_INFO 0x80000001
#define CPUID_EXT_LEAF_FEATURE_BITS 0x80000001
#define CPUID_EXT_LEAF_PM_FEATURES 0x80000007
#define CPUID_EXT_LEAF_ADDRESS_SIZES 0x80000008

#ifndef __ASSEMBLER__

#include <crater/types.h>

/**
 * @brief Retrieve CPU information by executing the CPUID instruction
 *
 * If the CPUID leaf/subleaf is not supported by the processor, the behavior is undefined.
 *
 * @param[in] leaf The main CPUID function
 * @param[in] subleaf The subfunction of the main CPUID function
 *
 * @param[out] eax,ebx,ecx,edx Where to put the values of the registers after CPUID is executed
 */
static inline void arch_x86_64_cpuid(u32 leaf, u32 subleaf,
		u32* eax, u32* ebx, u32* ecx, u32* edx) {
	__asm__("cpuid"
			: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
			: "a"(leaf), "c"(subleaf));
}

#endif /* __ASSEMBLER__ */
