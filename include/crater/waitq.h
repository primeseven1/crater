#pragma once

#include <crater/sched.h>

#define WAITQ_ENTRY_EXCLUSIVE (1 << 0)
#define WAITQ_ENTRY_WOKEN (1 << 1)

struct waitq_entry {
	struct thread* thread;
	bool (*cb)(struct waitq_entry*, unsigned long);
	void* private;
	int flags;
	struct list_node link;
};

struct waitq {
	struct list_head entries;
	spinlock_t lock;
};

#define WAITQ_INITIALIZER(n) { .entries = LIST_HEAD_INITIALIZER(n.entries), .lock = SPINLOCK_INITIALIZER }
#define WAITQ_DEFINE(n) struct waitq n = WAITQ_INITIALIZER(n)

/**
 * @brief Initialize a waitqueue
 * @param waitq The waitqueue
 */
static inline void waitq_init(struct waitq* waitq) {
	list_head_init(&waitq->entries);
	spinlock_init(&waitq->lock);
}

/**
 * @brief The default wakeup callback, wakes entry->thread
 *
 * @param entry The entry
 * @param key Unused
 *
 * @return true If the wakeup was accepted, and false if the entry was already marked as woken
 */
bool waitq_default_cb(struct waitq_entry* entry, unsigned long key);

/**
 * @brief Initialize a waitqueue entry
 *
 * @param entry The entry
 * @param thread The thread associated with the entry (usually the current thread)
 * @param cb The callback function for waking
 * @param private Whatever data
 */
void waitq_entry_init(struct waitq_entry* entry, struct thread* thread, bool (*cb)(struct waitq_entry*, unsigned long), void* private);

/**
 * @brief Add an entry to a waitqueue
 *
 * If the entry is an exclusive one, make sure to set the flag in entry->flags before calling this function.
 *
 * @param wq The waitqueue
 * @param entry The entry to add
 */
void waitq_add(struct waitq* wq, struct waitq_entry* entry);

/**
 * @brief Add an entry to a locked waitqueue
 * @see waitq_add()
 */
void waitq_add_locked(struct waitq* wq, struct waitq_entry* entry);

/**
 * @brief Remove an entry from a waitqueue
 *
 * @param wq The waitqueue
 * @param entry The entry to remove
 */
void waitq_remove(struct waitq* wq, struct waitq_entry* entry);

/**
 * @brief Remove an entry from a locked waitqueue
 * @see waitq_remove()
 */
void waitq_remove_locked(struct waitq_entry* entry);

/**
 * @brief Wake entries in a waitqueue
 *
 * @param wq The waitqueue
 * @param nr_exclusive The number of exclusive entries to wake. 0 wakes everything
 * @param key Passed to the callback function
 *
 * @return The number of accepted wakeups
 */
unsigned int waitq_wake(struct waitq* wq, unsigned int nr_exclusive, unsigned long key);

/**
 * @brief Wake entries in a locked waitqueue
 * @see waitq_wake()
 */
unsigned int waitq_wake_locked(struct waitq* wq, unsigned int nr_exclusive, unsigned long key);

struct __waitq_wait {
	struct waitq* wq;
	struct waitq_entry entry;
	struct timespec deadline;
	bool timed;
	int flags;
	unsigned long irq_flags;
};

int __waitq_wait_start(struct __waitq_wait* wait, struct waitq* wq, bool (*cb)(struct waitq_entry*, unsigned long), void* private, bool exclusive, bool timed, time_t us, int flags);
int __waitq_wait_sleep(struct __waitq_wait* wait);
int __waitq_wait_finish(struct __waitq_wait* wait, int err);

#define __waitq_wait_event(wq, cond, cb, private, exclusive, timed, us, flags) \
	({ \
		struct __waitq_wait ____wqw; \
		int ____wqw_err = __waitq_wait_start(&____wqw, (wq), (cb), (private), (exclusive), (timed), (us), (flags)); \
		if (____wqw_err == 0) { \
			while (1) { \
				if (cond) { \
					____wqw_err = 0; \
					break; \
				} \
				if (____wqw_err) \
					break; \
				____wqw_err = __waitq_wait_sleep(&____wqw); \
			} \
			____wqw_err = __waitq_wait_finish(&____wqw, ____wqw_err); \
		} \
		____wqw_err; \
	})

/**
 * @brief Wait for an event
 *
 * The condition is evaluated in an atomic context with the waitqueue locked.
 * A NULL callback will just use waitq_default_cb
 *
 * @param wq The waitqueue
 * @param cond The condition to wait for
 * @param cb The function to call on a wake attempt, called with wq->lock held
 * @param private Whatever data you want
 * @param flags Sleep flags
 *
 * @retval -EINTR Interrupted by a signal
 * @retval 0 Success
 */
#define waitq_wait_event(wq, cond, cb, private, flags) __waitq_wait_event(wq, cond, cb, private, false, false, 0, flags)

/**
 * @brief Wait for an event with a timeout
 * @see waitq_wait_event()
 * @param us The number of microseconds before timing out
 * @retval 0 Succesful
 * @retval -ETIME Timed out
 */
#define waitq_wait_event_timed(wq, cond, cb, private, us, flags) __waitq_wait_event(wq, cond, cb, private, false, true, us, flags)

/**
 * @brief The exclusive variant of waitq_wait_event()
 * @see waitq_wait_event()
 */
#define waitq_wait_event_exclusive(wq, cond, cb, private, flags) __waitq_wait_event(wq, cond, cb, private, true, false, 0, flags)

/**
 * @brief The exclusive variant of waitq_wait_event_timed()
 * @see waitq_wait_event_timed()
 */
#define waitq_wait_event_exclusive_timed(wq, cond, cb, private, us, flags) __waitq_wait_event(wq, cond, cb, private, true, true, us, flags)
