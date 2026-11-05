#include <crater/workqueue.h>
#include <crater/kthread.h>
#include <crater/format.h>
#include <crater/string.h>
#include <crater/init.h>
#include <crater/slab.h>
#include <crater/printk.h>

static int worker(void* arg) {
	struct workqueue* wq = arg;
	while (1) {
		if (semaphore_wait(&wq->sem, 0) != 0)
			continue;

		struct workqueue_item* item = NULL;

		unsigned long irq_flags;
		spinlock_acquire_irq_save(&wq->lock, &irq_flags);

		if (likely(!list_empty(&wq->queue))) {
			item = list_first_entry(&wq->queue, struct workqueue_item, link);
			list_remove(&item->link);
		}
		wq->nr_running++; /* Even if there is no work, increment the counter */

		spinlock_release_irq_restore(&wq->lock, &irq_flags);

		if (likely(item))
			item->fn(item->arg);
		else
			printk(PRINTK_WARN "workqueue%s: Signaled but has no work\n", wq->name);

		spinlock_acquire_irq_save(&wq->lock, &irq_flags);

		wq->nr_running--;
		if (list_empty(&wq->queue) && wq->nr_running == 0)
			waitq_wake(&wq->synchronizer, 0, 0);

		spinlock_release_irq_restore(&wq->lock, &irq_flags);
	}
	return 0;
}

struct workqueue* workqueue_create(int flags, const char* fmt, ...) {
	struct workqueue* wq = kmalloc(sizeof(*wq), MM_ZONE_NORMAL);
	if (!wq)
		return NULL;

	va_list va;
	va_start(va, fmt);
	if (vsnprintf(wq->name, sizeof(wq->name), fmt, va) < 0)
		strlcpy(wq->name, "workqueue", sizeof(wq->name));
	va_end(va);

	wq->thread = kthread_create(flags, worker, wq, "workqueue/%s", wq->name);
	if (!wq->thread) {
		kfree(wq);
		return NULL;
	}

	THREAD_HOLD(wq->thread);
	list_head_init(&wq->queue);
	semaphore_init(&wq->sem, 0);
	waitq_init(&wq->synchronizer);
	wq->nr_running = 0;
	spinlock_init(&wq->lock);

	int err = kthread_run(wq->thread, SCHED_PRIO_DEFAULT);
	if (err) {
		THREAD_RELEASE(wq->thread);
		kthread_destroy(wq->thread);
		kfree(wq);
		return NULL;
	}

	return wq;
}

static struct workqueue* system_workqueue;

void workqueue_schedule(struct workqueue* wq, struct workqueue_item* item) {
	if (!wq)
		wq = system_workqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);

	list_add_tail(&wq->queue, &item->link);
	semaphore_signal(&wq->sem);

	spinlock_release_irq_restore(&wq->lock, &irq_flags);
}

static inline bool workqueue_empty(struct workqueue* wq) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);
	const bool ret = (list_empty(&wq->queue) && wq->nr_running == 0);
	spinlock_release_irq_restore(&wq->lock, &irq_flags);
	return ret;
}

void workqueue_synchronize(struct workqueue* wq) {
	if (!wq)
		wq = system_workqueue;
	bug(waitq_wait_event(&wq->synchronizer, workqueue_empty(wq), waitq_default_cb, NULL, 0) != 0);
}

static void workqueue_init(void) {
	system_workqueue = workqueue_create(0, "system");
	if (!system_workqueue)
		panic("Failed to create system workqueue");
}

INIT_TASK_DECLARE(heap_init_task, kthread_init_task);
INIT_TASK_DEFINE(workqueue_init_task, INIT_TASK_SCOPE_BSP, workqueue_init, &heap_init_task, &kthread_init_task);
