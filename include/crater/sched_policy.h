#pragma once

#include <crater/spinlock.h>
#include <crater/list.h>
#include <crater/sched_types.h>

struct sched_policy;

struct runqueue {
	u32 sched_id;
	const struct sched_policy* policy;
	struct thread* __atomic current; /* Only changes under the lock AND by the CPU that owns the runqueue */
	struct thread* idle; /* For when there are no other threads to run */
	unsigned long __atomic thread_count;
	struct list_head zombie_list, detached_list;
	struct semaphore reaper_sem, __reaper1_sem;
	void* policy_priv; /* For scheduling algorithm */
	spinlock_t lock; /* When modifying runqueues, or changing the current thread */
};

struct sched_policy_ops {
	int (*init)(struct runqueue*); /* Initialize the runqueue */
	struct thread* (*alloc)(void); /* Allocate a thread structure */
	void (*free)(struct thread*); /* Free a thread structure */
	int (*attach)(struct runqueue*, struct thread*, int); /* Attach a thread to a runqueue */
	void (*detach)(struct runqueue*, struct thread*); /* Detach a thread from a runqueue */
	int (*enqueue)(struct runqueue*, struct thread*); /* Add a new thread to the queue. Might be called from a hardirq context. */
	int (*dequeue)(struct runqueue*, struct thread*); /* Remove a thread from the queue. Might be called from a hardirq context. */
	struct thread* (*pick_next)(struct runqueue*); /* Pick a thread in the runqueue. Might be called from a hardirq context. */
	int (*change_prio)(struct runqueue*, struct thread*, int); /* Change the priority of a thread, returns -errno on failure. */
	bool (*tick)(struct runqueue*, struct thread*); /* Happens on a timer IRQ, returns true if should reschedule. Called in a hardirq context. */
	void (*yield)(struct runqueue*, struct thread*); /* Put the current thread back on the queue. Might be called from a hardirq context. Not called when running the idle thread. */
};

struct sched_policy {
	const char* name, *desc;
	const struct sched_policy_ops* ops;
};

#define __sched_policy __attribute__((section(".schedpolicies"), aligned(8), used))
