#pragma once

#include <crater/types.h>

#ifdef CONFIG_SMP
u32 arch_get_cpu_count(void);
#else
static inline u32 arch_get_cpu_count(void) {
	return 1;
}
#endif /* CONFIG_SMP */

/**
 * @brief Start application processors (AP's)
 *
 * Must be defined regardless of if CONFIG_SMP is defined or not.
 * If not defined, AP's should be put into an idle state with IRQ's off.
 */
void arch_start_cpus(void);
