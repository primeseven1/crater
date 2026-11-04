#pragma once

#include <arch/percpu.h>
#include <arch/irq_flags.h>
#include <crater/mm.h>
#include <crater/time.h>
#include <crater/sched_policy.h>
#include <crater/interrupt.h>

struct cpu {
	struct mm* mm_struct;
	struct timekeeper_source* timekeeper;
	struct runqueue runqueue;
	struct list_head timer_event_list, softirq_timer_cb_list;
	u16 softirq_mask;
	struct semaphore softirqd_sem;
	long softirq_count;
	bool need_resched;
	struct arch_cpu arch_specific;
};
static_assert(sizeof_field(struct cpu, softirq_mask) * 8 >= SOFTIRQ_COUNT);

/**
 * @brief Get the current per-cpu structure
 *
 * This function is NOT safe to call with preemption enabled.
 * Members of the structure also should not be read/written with preempt enabled.
 *
 * Some members of the structure may require IRQ's to be disabled too.
 *
 * @return The current per-cpu structure
 */
static inline struct cpu* current_cpu(void) {
	struct arch_cpu* acpu = arch_current_cpu();
	return container_of(acpu, struct cpu, arch_specific);
}
