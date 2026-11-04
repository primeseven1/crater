#pragma once

#include <crater/compiler.h>
#include <crater/percpu.h>
#include <crater/panic.h>
#include <crater/sched_types.h>
#include <crater/irq.h>

#define TOPOLOGY_BSP (1 << 0)
#define TOPOLOGY_CURRENT_CPU (1 << 1)
#define TOPOLOGY_NO_MIGRATE (1 << 2)

static inline struct thread* current_thread(void) {
	unsigned long flags = local_irq_save();
	struct thread* ret = atomic_load(&current_cpu()->runqueue.current);
	local_irq_restore(flags);
	return ret;
}

#define PREEMPT_HARDIRQ_SHIFT 8ul
#define PREEMPT_SOFTIRQ_SHIFT 16ul
#define PREEMPT_HARDIRQ_MASK (0xFFul << PREEMPT_HARDIRQ_SHIFT)
#define PREEMPT_SOFTIRQ_MASK (0xFFul << PREEMPT_SOFTIRQ_SHIFT)
#define PREEMPT_HARDIRQ_OFFSET (1ul << PREEMPT_HARDIRQ_SHIFT)
#define PREEMPT_SOFTIRQ_OFFSET (1ul << PREEMPT_SOFTIRQ_SHIFT)

static inline bool in_softirq(void) {
	return (current_thread()->preempt_count & PREEMPT_SOFTIRQ_MASK) != 0;
}

static inline bool in_hardirq(void) {
	return (current_thread()->preempt_count & PREEMPT_HARDIRQ_MASK) != 0;
}

static inline bool in_interrupt(void) {
	return in_hardirq() || in_softirq();
}

static inline bool in_atomic(void) {
	return (current_thread()->preempt_count != 0 || local_irq_disabled(local_irq_read()));
}

/**
 * @brief Enable task preemption on the current CPU
 */
void preempt_enable(void);

/**
 * @brief Disable task preemption on the current CPU
 */
void preempt_disable(void);

/**
 * @brief Offset the preempt counter
 * @param count The number to offset by
 */
void preempt_offset(long count);

/**
 * @brief Enable task preemption on the current CPU
 */
void preempt_init(void);

/**
 * @brief Assign a scheduler ID to the current processor
 *
 * This function should be called as soon as per-cpu data is usable.
 */
void sched_assign_id(void);

/**
 * @brief Attach a thread to a runqueue and process
 *
 * @param thread The thread to attach
 * @param proc The process to attach the thread to
 * @param prio The priority of the thread
 *
 * @return -errno on failure
 */
int sched_thread_attach(struct thread* thread, struct proc* proc, int prio);

/**
 * @brief Detach a thread from a runqueue and process
 * @param thread The thread to detach
 */
void sched_thread_detach(struct thread* thread);

/**
 * @brief Enqueue a thread
 * @param thread The thread to enqueue
 * @return -errno on failure
 */
int sched_enqueue(struct thread* thread);

/**
 * @brief Dequeue a thread
 * @param thread The thread to dequeue
 * @return -errno on failure
 */
int sched_dequeue(struct thread* thread);

/**
 * @brief Change the priority of a thread
 * @param thread The thread to change the priority of
 * @param prio The new priority
 * @return -errno on failure
 */
int sched_change_prio(struct thread* thread, int prio);

/**
 * @brief Handle things like signals before returning to userspace
 * @param ctx Context being returned to
 */
__asmlinkage void sched_userspace_check(struct arch_context* ctx);

/**
 * @brief Wake a thread from sleep
 *
 * If the thread is already awake, wakeup_errno isn't set.
 *
 * @param thread The thread to wake
 * @param wakeup_errno The reason for the thread waking up
 */
void sched_wakeup(struct thread* thread, int wakeup_errno);

/**
 * @brief Prepare for a thread sleep
 *
 * @param us The number of microseconds to sleep for
 * @param flags The sleep state flags
 *
 * @retval -EINVAL us is zero, but the state is THREAD_SLEEPING
 * @retval -ENOMEM Failed to allocate a timer event for the sleep
 * @retval 0 Successful
 */
int sched_prepare_sleep(time_t us, int flags);

/**
 * @brief Exit the current thread
 */
_Noreturn void sched_thread_exit(void);

/**
 * @brief Switch to another thread
 *
 * Should only ever be called at the end of an IRQ handler when preemption is
 * enabled.
 */
void schedule_preempt_irq(void);

/**
 * @brief Switch to another thread, if available
 * @return Returns -errno or 0 if waking from a sleep, otherwise the value is meaningless
 */
int schedule(void);

/**
 * @brief Yeild the CPU
 *
 * Do NOT use this for sleeps, use schedule() instead.
 *
 * @return Always returns zero.
 */
int sched_yield(void);

/**
 * @brief Sleep for n microseconds
 * @param us Microseconds to sleep for
 */
static inline int usleep(time_t us) {
	if (us == 0)
		return 0;
	int err = sched_prepare_sleep(us, 0);
	return (err == 0) ? schedule() : err;
}

/**
 * @brief Sleep for n milliseconds
 * @param ms Milliseconds to sleep for
 */
static inline int msleep(time_t ms) {
	return usleep(ms * 1000);
}

/**
 * @brief Sleep for n nanoseconds
 * @param ns Nanoseconds to sleep for
 */
static inline int nsleep(time_t ns) {
	return usleep((ns + 999) / 1000);
}
