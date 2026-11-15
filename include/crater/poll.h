#pragma once

#include <crater/waitq.h>
#include <crater/list.h>
#include <crater/time.h>
#include <arch/posix.h>

struct vnode;

struct poll_entry {
	struct waitq_entry wqe;
	struct waitq* wq;
	short events;
};

struct poll_table {
	struct waitq wq; /* The polling thread sleeps here */
	bool triggered, registering; /* poll_table_add() only does something when this is set */
	short events; /* The events the poll operation being called right now is asked about */
	int error; /* Set when poll_table_add() fails */
	struct list_head chunks; /* struct poll_chunk */
};

struct poll_vnode {
	struct vnode* vnode;
	short events, revents;
};

#define POLL_EVENTS_ALWAYS (POLLERR | POLLHUP)
#define POLL_EVENTS_DEFAULT (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)

void poll_table_init(struct poll_table* pt);
void poll_table_destroy(struct poll_table* pt);

/**
 * @brief Add a waitqueue to a poll table
 *
 * Meant to be called from a poll operation, before it checks what is ready.
 * When the waitqueue is woken, the thread sleeping in poll_table_sleep() wakes up.
 *
 * This function does nothing if pt is NULL, or if the poll table is no longer registering waitqueues.
 *
 * @param pt The poll table, can be NULL
 * @param wq The waitqueue that gets woken when the readiness changes
 */
void poll_table_add(struct poll_table* pt, struct waitq* wq);

/**
 * @brief Sleep until a waitqueue in the poll table is woken.
 *
 * @param pt The poll table
 * @param us The number of microseconds to wait before timing out
 * @param flags Thread sleep flags (THREAD_STATE_FLAG_*)
 *
 * @retval 0 A waitqueue was woken, the readiness needs to be checked again
 * @retval -ETIME Timed out
 * @retval -EINTR Interrupted by a signal
 */
int poll_table_sleep(struct poll_table* pt, time_t us, int flags);

/**
 * @brief Wait for events on a set of vnodes
 *
 * The caller needs to hold a reference to all vnodes until this function returns.
 * POLLERR and POLLHUP are always reported, even if they are not asked for.
 *
 * @param vnodes The vnodes to poll, can be NULL if the count is zero
 * @param count The number of vnodes, when zero this function just does a sleep
 * @param us The number of microseconds to wait. A value of zero only checks without sleeping. Negative waits forever
 * @param flags Thread sleep flags (THREAD_STATE_FLAG_*)
 *
 * @retval -EINVAL count is bigger than INT_MAX
 * @retval -ENOMEM Out of memory while adding waitqueues
 * @retval -EINTR Interrupted, only possible with THREAD_STATE_FLAG_INTERRUPTIBLE
 * @return The number of vnodes with a non-zero revents value, 0 if timed out
 */
int poll_vnodes(struct poll_vnode* vnodes, size_t count, time_t us, int flags);

/**
 * @brief Tell pollers (and sleepers) that the readiness of something changed
 *
 * Safe to call from an interrupt context.
 *
 * @param wq The waitqueue that poll operation adds to the poll table
 * @param events The events that are now ready, zero wakes every poller no matter what it asked for
 *
 * @return The number of accepted wakeups
 */
static inline unsigned int poll_notify(struct waitq* wq, short events) {
	return waitq_wake(wq, 0, (unsigned long)events);
}
