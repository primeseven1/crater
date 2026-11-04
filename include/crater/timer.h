#pragma once

#include <crater/init.h>
#include <crater/interrupt.h>
#include <crater/time.h>

#define __TIMER_FLAG_PERCPU_BIT 0u
#define __TIMER_FLAG_PERCPU_BIT_OPTIONAL 1u
#define __TIMER_FLAG_HIGH_PRECISION_BIT 2u
#define __TIMER_FLAG_HIGH_PRECISION_BIT_OPTIONAL 3u
#define __TIMER_FLAG_HARDIRQ 4u
#define __TIMER_FLAG_EVENT_ALLOC_ATOMIC_BIT 5u
#define __TIMER_FLAG_EVENT_ALLOC_AUTOFREE_BIT 6u

#define TIMER_FLAG_PERCPU (1u << __TIMER_FLAG_PERCPU_BIT)
#define TIMER_FLAG_HIGH_PRECISION (1u << __TIMER_FLAG_HIGH_PRECISION_BIT)
#define TIMER_FLAG_HARDIRQ (1u << __TIMER_FLAG_HARDIRQ)
#define TIMER_FLAG_EVENT_ALLOC_ATOMIC (1u << __TIMER_FLAG_EVENT_ALLOC_ATOMIC_BIT)
#define TIMER_FLAG_EVENT_ALLOC_AUTOFREE (1u << __TIMER_FLAG_EVENT_ALLOC_AUTOFREE_BIT)
#define TIMER_FLAG_MASK (TIMER_FLAG_PERCPU | TIMER_FLAG_HIGH_PRECISION | TIMER_FLAG_HARDIRQ | TIMER_FLAG_EVENT_ALLOC_ATOMIC | TIMER_FLAG_EVENT_ALLOC_AUTOFREE)

#define TIMER_FLAG_OPTIONAL(f) (f | (1u << (__##f##_BIT_OPTIONAL))) /* usage: TIMER_FLAG_OPTIONAL(TIMER_FLAG_*) */
#define TIMER_FLAG_IS_OPTIONAL(x, f) (!!(x & (1u << (__##f##_BIT_OPTIONAL)))) /* usage: TIMER_FLAG_IS_OPTIONAL(flags, TIMER_FLAG_*) */
#define TIMER_FLAG_OPTIONAL_MASK ((1u << __TIMER_FLAG_PERCPU_BIT_OPTIONAL) | (1u << __TIMER_FLAG_HIGH_PRECISION_BIT_OPTIONAL))

struct timer;

struct timer_ops {
	bool (*probe)(void); /* Check if the timer exists */
	int (*init)(struct timer*); /* Initialize the timer */
	int (*arm)(struct timer*, struct timespec fromnow, void* handle); /* Arm a timer, should fail if a timer is already armed */
	int (*rearm)(struct timer*, struct timespec fromnow, void* handle); /* Arm a timer, but should overwrite an existing timer if there is one */
	void (*cancel)(struct timer*, void* handle); /* Cancel a timer, should fail if handle != current_timer_handle */
};

struct timer {
	const char* name;
	int flags;
	const struct timer_ops* ops;
	struct init_task** probe_dependencies, **init_dependencies;
	unsigned long __atomic cpus_initialized; /* Matters for percpu timers */
	struct list_node link;
};

struct timer_event_handler {
	void (*fn)(void* handle, void* arg);
	void* arg;
};

#define __timer __attribute__((section(".timers"), aligned(8), used))

void do_timer_events(struct timer* source);

/**
 * @brief Arm a timer event with an already existing handle
 *
 * @param handle The handle
 * @param us Number of microseconds before triggering
 * @param handler The function to call when triggering
 * @param flags Timer flags
 *
 * @retval -ENODEV No timer device available
 * @retval -EINVAL Invalid handler
 * @retval 0 Successful
 */
int arm_timer_event_handle(void* handle, time_t us, const struct timer_event_handler* handler, int flags);

/**
 * @brief Arm a timer event
 *
 * @param[in] us The number of microseconds from now before the event triggers
 * @param[in] handler A pointer to the handler structure
 * @param[in] flags Flags for how to pick the timer
 * @param[out] out_handle Where the handle for the timer event should be stored
 *
 * @retval -EINVAL handler is NULL, or handler->fn is NULL
 * @retval -ENOMEM Out of memory
 * @retval -ENODEV No device available with the required flags
 * @retval 0 Successful
 */
int arm_timer_event(time_t us, const struct timer_event_handler* handler, int flags, void** out_handle);

/**
 * @brief Cancel a timer event
 *
 * Safe to call from an atomic context
 *
 * @param handle The pointer to the event handle
 *
 * @retval -EINVAL handle is NULL
 * @retval 0 Successful
 */
int cancel_timer_event(void* handle);

/**
 * @brief Allocate a timer event
 *
 * This is useful for when you want to arm a timer event, but need to arm it in
 * an atomic context. Call rearm_timer_event on the handle to arm.
 *
 * @param flags TIMER_FLAG_EVENT_ALLOC_* flags, other flags are ignored
 * @return A pointer to the handle
 */
void* alloc_timer_event_handle(int flags);

/**
 * @brief Free a timer event handle
 * @param handle The pointer to the handle
 */
void free_timer_event_handle(void* handle);
