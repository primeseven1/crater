#pragma once

#include <crater/spinlock.h>
#include <crater/list.h>
#include <crater/time.h>

struct semaphore {
	long count;
	struct list_head wait_queue;
	spinlock_t lock;
};

#define SEMAPHORE_DEFINE(n, c) struct semaphore n = { .count = c, \
	.wait_queue = LIST_HEAD_INITIALIZER(n.wait_queue), \
	.lock = SPINLOCK_INITIALIZER \
}
#define SEMAPHORE_INITIALIZER(n, c) \
	{ .count = c, .wait_queue = LIST_HEAD_INITIALIZER(n.wait_queue), .lock = SPINLOCK_INITIALIZER }
static inline void semaphore_init(struct semaphore* sem, long count) {
	sem->count = count;
	list_head_init(&sem->wait_queue);
	spinlock_init(&sem->lock);
}

/**
 * @brief Wait for an event
 *
 * @param sem The event to wait for
 * @param flags Sleep flags for the thread (SCHED_SLEEP_*)
 *
 * @retval -EINTR Interrupted by a signal
 * @retval 0 Successful
 */
int semaphore_wait(struct semaphore* sem, int flags);

/**
 * @brief Wait for an event with a timout
 *
 * @param sem The event to wait for
 * @param us The number of microseconds to wait for before timing out
 * @param flags Sleep flags for the thread
 *
 * @retval -EINTR Interrupted by a signal
 * @retval -ETIME Timed out
 * @retval 0 Successful
 */
int semaphore_wait_timed(struct semaphore* sem, time_t us, int flags);

/**
 * @brief Signal an event
 * @param sem The event to signal
 */
void semaphore_signal(struct semaphore* sem);

/**
 * @brief Reset a semaphore back to zero
 * @param sem The semaphore to reset
 * @retval -EBUSY Semaphore has waiters
 * @reval 0 Successful
 */
int semaphore_reset(struct semaphore* sem);

/**
 * @brief Check for an event without blocking
 * @param sem The event to check
 * @return true if successful
 */
bool semaphore_try(struct semaphore* sem);
