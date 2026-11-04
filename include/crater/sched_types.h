#pragma once

#include <crater/atomic.h>
#include <crater/list.h>
#include <crater/mm.h>
#include <crater/panic.h>
#include <crater/smp.h>
#include <crater/cred.h>

#include <arch/posix.h>
#include <arch/context.h>

#define THREAD_STACK_SIZE 0x4000

#define THREAD_NEW 0
#define THREAD_RUNNING 1
#define THREAD_READY 2
#define THREAD_SLEEPING 3
#define THREAD_ZOMBIE 4
#define THREAD_DEAD 5

#define THREAD_STATE_FLAG_INTERRUPTIBLE (1 << 0)
#define THREAD_STATE_FLAG_TIMEOUT (1 << 1)

#define SCHED_PRIO_MIN 1
#define SCHED_PRIO_DEFAULT 45
#define SCHED_PRIO_MAX 99

/* Avoid recursive includes */
struct cpu;
struct sched_policy;
struct proc;

struct context {
	struct arch_context arch_context;
	struct arch_context_extended arch_extended_context;
};

struct topology {
	struct cpu* __atomic cpu;
	bool __atomic migratable, on_cpu;
	struct cpumask cpumask;
};

struct thread {
	void* kernel_stack_top; /* The stack to switch to on userspace -> kernel, and for kthreads */
	struct topology topology; /* What CPU's this thread can run on */
	struct proc* __atomic proc; /* Process this thread is associated with */
	struct mm* mm_struct; /* Current memory context. May be different from proc->mm_struct */
	struct context context; /* CPU registers */
	long in_usercopy; /* Kernel space accessing user space */
	long preempt_count; /* If zero, the thread can be preempted */
	int __atomic prio, state, state_flags, wakeup_errno; /* Thread priority and state */
	bool __atomic should_exit;
	unsigned long long __atomic sleep_gen; /* Prevents stale wakeups */
	struct list_node proc_link, block_link;
	struct timespec detach_time; /* The time the reaper detached the this thread */
	time_t reap_warn_deadline; /* For when the thread takes too long to get freed */
	unsigned long __atomic refcnt;
};
static_assert(offsetof(struct thread, kernel_stack_top) == 0);

#define THREAD_HOLD(t) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(t), struct thread*), "__builtin_types_compatible_p(typeof(p), struct thread*)"); \
		atomic_fetch_add(&(t)->refcnt, 1); \
	} while (0)
#define THREAD_RELEASE(t) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(t), struct thread*), "__builtin_types_compatible_p(typeof(p), struct thread*)"); \
		atomic_fetch_sub(&(t)->refcnt, 1); \
	} while (0)

/**
 * @brief Allocate a thread structure
 *
 * Thread is returned with a ref
 *
 * @return A pointer to the thread
 */
struct thread* alloc_thread(void);

/**
 * @brief free a thread
 *
 * Refcount must be zero
 *
 * @param thread The thread to free
 */
void free_thread(struct thread* thread);

/**
 * @brief Allocate a kernel stack
 * @return A pointer to the top of the stack, or -errno on failure
 */
void* alloc_stack(void);

/**
 * @brief Free a kernel stack
 * @param top The top of the stack
 */
void free_stack(void* top);

/**
 * @brief Allocate a kernel stack for a thread
 *
 * There is no free_thread_stack() function. The stack is freed when
 * free_thread() is called.
 *
 * @param[in] thread The thread the stack is for
 * @param[out] top An optional argument to get the pointer to the stack
 * @return 0 on success -errno on failure
 */
int alloc_thread_stack(struct thread* thread, void** top);

/**
 * @brief Initialize the topology for a thread
 *
 * @param thread The thread
 * @param topology_flags Flags for how to select the CPU
 */
void thread_topology_init(struct thread* thread, int topology_flags);
