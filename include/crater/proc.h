#pragma once

#include <crater/cred.h>
#include <crater/mm.h>
#include <crater/vfs.h>
#include <crater/mutex.h>
#include <crater/sched.h>
#include <crater/fd.h>
#include <arch/posix.h>

struct proc {
	pid_t pid;
	struct cred cred;
	struct proc* parent, *sibling, *child;
	struct mm* mm_struct;
	struct list_head thread_list;
	unsigned int thread_count;
	bool no_more_threads;
	spinlock_t thread_list_lock;
	struct vnode* cwd, *root;
	mutex_t cwd_root_mtx;
	struct fd* fd_table;
	mutex_t fd_mtx;
	int exit_code;
	unsigned long __atomic refcnt;
};

/**
 * @brief Get the current process
 *
 * Does NOT increase the refcount
 *
 * @return A pointer to the current process
 */
static inline struct proc* current_proc(void) {
	return atomic_load(&current_thread()->proc);
}

/**
 * @brief Get a process by PID
 *
 * Process is returned with a ref, use PROC_RELEASE() to unref.
 *
 * @param[in] pid The PID of the process
 * @param[out] out Pointer to where the process will be stored
 *
 * @retval -ESRCH No process found
 * @retval 0 Successful
 */
int proc_get(pid_t pid, struct proc** out);

/**
 * @brief Create a process structure
 *
 * Pointer is returned with a ref, use PROC_RELEASE() to unref.
 *
 * @return A pointer to the new process
 */
struct proc* proc_create(void);

/**
 * @brief Called when a process refcount goes to zero
 *
 * Do not call this function directly, use PROC_RELEASE(), which will
 * call this function when the refcount hits zero.
 *
 * @param proc The process
 */
void proc_inactive(struct proc* proc);

/**
 * @brief Terminate the current process
 * @param exit_code The exit code
 */
_Noreturn void proc_terminate(int exit_code);

/**
 * @brief Attach a thread to a process
 *
 * @param proc The process to attach the thread to
 * @param thread The thread to attach
 *
 * @retval -ESRCH Process is in the middle of being torn down
 * @retval 0 Successful
 */
int proc_thread_attach(struct proc* proc, struct thread* thread);

/**
 * @brief Detach a thread from a process
 * @param thread The thread to detach
 */
void proc_thread_detach(struct thread* thread);

#define PROC_HOLD(p) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(p), struct proc*), "__builtin_types_compatible_p(typeof(p), struct proc*)"); \
		atomic_add_fetch(&(p)->refcnt, 1); \
	} while (0)
#define PROC_RELEASE(p) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(p), struct proc*), "__builtin_types_compatible_p(typeof(p), struct proc*)"); \
		if (atomic_sub_fetch(&(p)->refcnt, 1) == 0) \
			proc_inactive(p); \
	} while (0)
