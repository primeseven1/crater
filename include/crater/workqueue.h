#pragma once

#include <crater/list.h>
#include <crater/spinlock.h>
#include <crater/sched.h>
#include <crater/semaphore.h>
#include <crater/waitq.h>

typedef void (*workhandler_t)(void*);

struct workqueue {
	char name[32];
	struct thread* thread;
	struct list_head queue;
	struct semaphore sem;
	struct waitq synchronizer;
	unsigned int nr_running;
	spinlock_t lock;
};

struct workqueue_item {
	void (*fn)(void*);
	void* arg;
	struct list_node link;
};

/**
 * @brief Create a workqueue
 *
 * @param flags Topology flags
 * @param fmt Format string for the name of the workqueue
 * @param ... Variable arguments for the format string
 *
 * @return A pointer to the workqueue
 */
__attribute__((format(printf, 2, 3)))
struct workqueue* workqueue_create(int flags, const char* fmt, ...);

/**
 * @brief Add work to a workqueue
 *
 * If `wq` is NULL, this function will add to the
 * system workqueue.
 *
 * @param wq The workqueue to add to
 * @param item The workqueue item to add
 */
void workqueue_schedule(struct workqueue* wq, struct workqueue_item* item);

/**
 * @brief Wait for all work in a workqueue to finish
 *
 * Not safe to call from an atomic context.
 * If `wq` is NULL, this function synchronizes the system workqueue.
 *
 * @param wq The workqueue to synchronize
 */
void workqueue_synchronize(struct workqueue* wq);
