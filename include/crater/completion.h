#pragma once

#include <crater/spinlock.h>
#include <crater/sched.h>

struct completion {
	bool done;
	struct list_head queue;
	spinlock_t lock;
};

static inline void completion_init(struct completion* completion) {
	completion->done = false;
	list_head_init(&completion->queue);
	spinlock_init(&completion->lock);
}

/**
 * @brief Mark a completion event as done
 *
 * Wakes up all threads waiting.
 *
 * @param completion The completion struct
 */
void completion_signal(struct completion* completion);

/**
 * @brief Wait for a completion event, but do not reschedule
 *
 * The schedule() function must be called manually after this function.
 *
 * @param completion The completion struct
 * @param flags Sleep flags
 *
 * @retval -EINVAL Invalid flags
 * @retval -EALREADY Completion already signaled (avoid an extra schedule())
 * @retval 0 Successful (Use schedule())
 */
int completion_wait_no_resched(struct completion* completion, int flags);

/**
 * @brief Wait on a completion event
 *
 * @param completion The completion struct
 * @param flags Sleep flags
 *
 * @retval -EINVAL Invalid flags
 * @retval 0 Successful
 */
int completion_wait(struct completion* completion, int flags);

/**
 * @brief Reset a completion event
 * @param completion The completion struct
 * @retval -EBUSY Threads still waiting
 * @retval 0 Successful
 */
int completion_reset(struct completion* completion);
