#pragma once

#include <crater/types.h>
#include <crater/atomic.h>
#include <arch/processor.h>

#if ARCH_SMP_MAX_CPUS > 4096
#define SMP_MAX_CPUS 4096
#else
#define SMP_MAX_CPUS ARCH_SMP_MAX_CPUS
#endif

struct smp_cpus {
	u32 count;
	struct cpu** cpus;
};

/**
 * @brief Register the current CPU
 */
void smp_register_cpu(void);

/**
 * @brief Safely read the SMP CPU's
 * @param out_cpus Where the cpu info will be stored
 */
void smp_cpus_read_acquire(struct smp_cpus* out_cpus);

/**
 * @brief Stop reading the SMP CPU's
 * @param cpus Same variable that was given to smp_read_acquire
 */
void smp_cpus_read_release(struct smp_cpus* cpus);

/**
 * @brief Wait for other CPU's to finish initialization
 */
void smp_init_bsp_wait_for_others(void);

/**
 * @brief Wait for all CPU's to finish initialization.
 *
 * Can be called on any CPU, but only after smp_init_complete() is called.
 * Otherwise this will deadlock.
 */
void smp_init_wait_for_all(void);

/**
 * @brief Signal that the current CPU has finished initialization.
 */
void smp_init_complete(void);

/**
 * @brief Send a stop IPI to all processors
 */
void smp_send_stop(void);

struct cpumask {
	u8 __atomic mask[(SMP_MAX_CPUS + 7) >> 3];
};

static inline void cpumask_memset(struct cpumask* mask, int val) {
	for (size_t i = 0; i < ARRAY_SIZE(mask->mask); i++)
		atomic_store(&mask->mask[i], val & U8_MAX);
}

static inline void cpumask_set(struct cpumask* mask, u32 cpu, bool x) {
	if (x)
		atomic_fetch_or(&mask->mask[cpu >> 3], 1 << (cpu & 7));
	else
		atomic_fetch_and(&mask->mask[cpu >> 3], ~(1 << (cpu & 7)));
}

static inline bool cpumask_test(const struct cpumask* mask, u32 cpu) {
	return (atomic_load(&mask->mask[cpu >> 3]) & (1 << (cpu & 7))) != 0;
}
