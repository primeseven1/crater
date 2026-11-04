#include <crater/kthread.h>
#include <crater/proc.h>
#include <crater/format.h>
#include <crater/mutex.h>
#include <crater/init.h>
#include <crater/hashtable.h>
#include <crater/trace.h>
#include <crater/vmm.h>
#include <crater/string.h>
#include <crater/printk.h>

#include <arch/kthread.h>

struct kthread_argument {
	int (* __atomic threadfn)(void*);
	void* __atomic arg;
};
static_assert(sizeof(struct kthread_argument) == 16);

struct kthread {
	char name[32];
	int (*threadfn)(void*);
	void* arg, *stack_top;
	bool scheduled;
};

static struct proc* kernel_proc;

static struct hashtable* kthread_table;
static MUTEX_DEFINE(kthread_table_lock);

struct thread* kthread_create(int flags, int (*threadfn)(void*), void* arg, const char* fmt, ...) {
	struct thread* thread = alloc_thread();
	if (!thread)
		return NULL;

	struct kthread kt;
	va_list va;
	va_start(va, fmt);
	if (unlikely(vsnprintf(kt.name, sizeof(kt.name), fmt, va) < 0))
		strlcpy(kt.name, "kthread", sizeof(kt.name));
	va_end(va);

	kt.threadfn = threadfn;
	kt.arg = arg;

	int err = alloc_thread_stack(thread, &kt.stack_top);
	if (err) {
		THREAD_RELEASE(thread);
		free_thread(thread);
		return NULL;
	}

	kt.scheduled = false;

	mutex_acquire(&kthread_table_lock);
	err = hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt);
	mutex_release(&kthread_table_lock);
	if (err) {
		THREAD_RELEASE(thread); /* Remove the ref that alloc_thread() gives */
		free_thread(thread);
		return NULL;
	}

	thread_topology_init(thread, flags);
	return thread;
}

int kthread_run(struct thread* thread, int prio) {
	mutex_acquire(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		if (likely(err == -ENOENT))
			err = -EINVAL;
		goto out_unlock;
	}
	if (kt.scheduled)
		goto out_unlock;

	err = sched_thread_attach(thread, kernel_proc, prio);
	if (err)
		goto out_unlock;

	/* Prepare the stack */
	struct kthread_argument* thread_args = (struct kthread_argument*)kt.stack_top - 1;
	atomic_store(&thread_args->threadfn, kt.threadfn);
	atomic_store(&thread_args->arg, kt.arg);

	arch_context_prepare_execution(&thread->context.arch_context, ARCH_CODE_ADDRESS(uintptr_t, arch_kthread_start), (uintptr_t)kt.stack_top - sizeof(*thread_args));
	err = sched_enqueue(thread);
	if (err) {
		sched_thread_detach(thread);
		goto out_unlock;
	}

	kt.scheduled = true;
	bug(hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt) != 0);
out_unlock:
	mutex_release(&kthread_table_lock);
	return err;
}

void kthread_destroy(struct thread* thread) {
	mutex_acquire(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		dump_stack();
		printk(PRINTK_ERR "kthread: %s() invalid pointer (err %d)\n", __func__, err);
	} else {
		bug(kt.scheduled == true);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		THREAD_RELEASE(thread);
		free_thread(thread);
	}

	mutex_release(&kthread_table_lock);
}

void kthread_detach(struct thread* thread) {
	mutex_acquire(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		dump_stack();
		printk(PRINTK_ERR "kthread: %s() invalid pointer (err %d)\n", __func__, err);
	} else {
		bug(kt.scheduled == false);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		THREAD_RELEASE(thread);
	}

	mutex_release(&kthread_table_lock);
}

_Noreturn void kthread_exit(int exit) {
	(void)exit;
	sched_thread_exit();
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

_Noreturn __asmlinkage void kthread_start(int (*func)(void*), void* arg) {
	int ret = func(arg);
	kthread_exit(ret);
}

__diag_pop();

static void kthread_init(void) {
	bug(proc_get(0, &kernel_proc) != 0);
	kthread_table = hashtable_create(64, sizeof(struct kthread));
	if (unlikely(!kthread_table))
		panic("Failed to create kthread hashtable");
}

INIT_TASK_DECLARE(sched_init_task, heap_init_task);
INIT_TASK_DEFINE(kthread_init_task, INIT_TASK_SCOPE_BSP, kthread_init, &sched_init_task, &heap_init_task);
