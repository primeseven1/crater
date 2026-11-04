#include <crater/hashtable.h>
#include <crater/proc.h>
#include <crater/panic.h>
#include <crater/init.h>
#include <crater/slab.h>

static struct slab_cache* process_cache;
static struct hashtable* process_table;
static pid_t __atomic pid_counter = atomic_init(1);
static struct proc kernel_proc = {
	.pid = 0,
	.cred = { .uid = 0, .euid = 0, .suid = 0, .gid = 0, .egid = 0, .sgid = 0 },
	.parent = NULL, .sibling = NULL, .child = NULL,
	.mm_struct = NULL,
	.thread_list = LIST_HEAD_INITIALIZER(kernel_proc.thread_list),
	.thread_count = 0,
	.no_more_threads = false,
	.thread_list_lock = SPINLOCK_INITIALIZER,
	.cwd = NULL, .root = NULL,
	.cwd_root_mtx = MUTEX_INITIALIZER(kernel_proc.cwd_root_mtx),
	.fd_table = NULL,
	.fd_mtx = MUTEX_INITIALIZER(kernel_proc.fd_mtx),
	.exit_code = 0,
	.refcnt = atomic_init(1)
};

int proc_get(pid_t pid, struct proc** out) {
	if (pid != 0) {
		int err = hashtable_search(process_table, &pid, sizeof(pid), out);
		if (err)
			return -ESRCH;
	} else {
		*out = &kernel_proc;
	}
	PROC_HOLD(*out);
	return 0;
}

struct proc* proc_create(void) {
	struct proc* proc = slab_cache_alloc(process_cache);
	if (!proc)
		return NULL;

	proc->mm_struct = mm_create();
	if (!proc->mm_struct) {
		slab_cache_free(process_cache, proc);
		return NULL;
	}
	proc->fd_table = fd_create_table();
	if (!proc->fd_table) {
		mm_destroy(proc->mm_struct);
		slab_cache_free(process_cache, proc);
		return NULL;
	}

	proc->pid = atomic_fetch_add(&pid_counter, 1);
	proc->cred = *current_cred();
	proc->parent = NULL;
	proc->sibling = NULL;
	proc->child = NULL;
	list_head_init(&proc->thread_list);
	proc->thread_count = 0;
	proc->no_more_threads = false;
	spinlock_init(&proc->thread_list_lock);
	proc->cwd = NULL;
	proc->root = NULL;
	mutex_init(&proc->cwd_root_mtx);
	mutex_init(&proc->fd_mtx);
	proc->exit_code = 0;
	atomic_store(&proc->refcnt, 1);

	int err = hashtable_insert(process_table, &proc->pid, sizeof(proc->pid), &proc);
	if (err) {
		mm_destroy(proc->mm_struct);
		fd_destroy_table(proc->fd_table);
		slab_cache_free(process_cache, proc);
		return NULL;
	}
	return proc;
}

void proc_inactive(struct proc* proc) {
	bug(proc == &kernel_proc);
	bug(hashtable_remove(process_table, &proc->pid, sizeof(proc->pid)) != 0);
	mm_destroy(proc->mm_struct);
	fd_destroy_table(proc->fd_table);
	slab_cache_free(process_cache, proc);
}

_Noreturn void proc_terminate(int exit_code) {
	struct proc* proc = current_proc();
	if (proc == &kernel_proc)
		panic("Attempted to kill the current process in a kernel thread");
	if (proc->pid == 1)
		panic("Attempted to kill init (exit_code: %#.8x)", exit_code);

	struct thread* current = current_thread();
	bool thread_exit_now = false;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&proc->thread_list_lock, &irq_flags);

	if (!proc->no_more_threads) {
		proc->no_more_threads = true;
		struct thread* thread;
		list_for_each_entry(thread, &proc->thread_list, proc_link) {
			if (thread != current) {
				atomic_store(&thread->should_exit, true);
				sched_wakeup(thread, -EINTR);
			}
		}
	} else {
		thread_exit_now = true;
	}

	spinlock_release_irq_restore(&proc->thread_list_lock, &irq_flags);

	if (thread_exit_now)
		sched_thread_exit();

	/* TODO: This is terrible */
	while (1) {
		spinlock_acquire_irq_save(&proc->thread_list_lock, &irq_flags);
		bool one_thread = proc->thread_count == 1;
		spinlock_release_irq_restore(&proc->thread_list_lock, &irq_flags);
		if (one_thread)
			break;
		schedule();
	}

	proc->exit_code = exit_code;
	sched_thread_exit();
}

int proc_thread_attach(struct proc* proc, struct thread* thread) {
	int err = -ESRCH;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&proc->thread_list_lock, &irq_flags);

	if (likely(!proc->no_more_threads)) {
		THREAD_HOLD(thread);
		PROC_HOLD(proc);

		bug(atomic_exchange(&thread->proc, proc) != NULL);
		list_add(&proc->thread_list, &thread->proc_link);
		proc->thread_count++;
		if (!thread->mm_struct)
			thread->mm_struct = proc->mm_struct;

		err = 0;
	}

	spinlock_release_irq_restore(&proc->thread_list_lock, &irq_flags);
	return err;
}

void proc_thread_detach(struct thread* thread) {
	struct proc* proc = atomic_exchange(&thread->proc, NULL);

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&proc->thread_list_lock, &irq_flags);

	list_remove(&thread->proc_link);
	proc->thread_count--;
	PROC_RELEASE(proc);
	THREAD_RELEASE(thread);

	spinlock_release_irq_restore(&proc->thread_list_lock, &irq_flags);
}

static void proc_init(void) {
	process_cache = slab_cache_create(sizeof(struct proc), alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	if (!process_cache)
		out_of_memory();
	process_table = hashtable_create(32, sizeof(struct proc*));
	if (!process_table)
		out_of_memory();
	kernel_proc.mm_struct = current_cpu()->mm_struct;
}

INIT_TASK_DECLARE(heap_init_task, vmm_init_task);
INIT_TASK_DEFINE(proc_init_task, INIT_TASK_SCOPE_BSP, proc_init, &heap_init_task, &vmm_init_task);
