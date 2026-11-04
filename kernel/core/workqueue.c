#include <crater/workqueue.h>
#include <crater/kthread.h>
#include <crater/format.h>
#include <crater/string.h>
#include <crater/init.h>
#include <crater/slab.h>
#include <crater/printk.h>

struct workqueue_item {
	void (*fn)(void*);
	void* arg;
	struct list_node link;
};

static struct slab_cache* atomic_wq_item_cache;

static int worker(void* arg) {
	struct workqueue* wq = arg;
	while (1) {
		if (semaphore_wait(&wq->sem, 0) != 0)
			continue;

		struct workqueue_item* item = NULL;
		bool empty = false;
		unsigned long irq_flags;
		spinlock_acquire_irq_save(&wq->lock, &irq_flags);

		if (likely(!list_empty(&wq->queue))) {
			item = list_first_entry(&wq->queue, struct workqueue_item, link);
			list_remove(&item->link);
			empty = list_empty(&wq->queue);
		}

		spinlock_release_irq_restore(&wq->lock, &irq_flags);

		if (likely(item)) {
			item->fn(item->arg);
			slab_cache_free(atomic_wq_item_cache, item);
		} else {
			printk(PRINTK_WARN "workqueue%s: Signaled but has no work\n", wq->name);
		}

		if (empty) {
			spinlock_acquire_irq_save(&wq->lock, &irq_flags);

			if (list_empty(&wq->queue))
				completion_signal(&wq->synchronizer);

			spinlock_release_irq_restore(&wq->lock, &irq_flags);
		}
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
	completion_init(&wq->synchronizer);
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

int workqueue_schedule(struct workqueue* wq, workhandler_t handler, void* arg) {
	struct workqueue_item* item = slab_cache_alloc(atomic_wq_item_cache);
	if (!item)
		return -ENOMEM;
	item->fn = handler;
	item->arg = arg;
	list_node_init(&item->link);

	if (!wq)
		wq = system_workqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);

	if (list_empty(&wq->queue))
		completion_reset(&wq->synchronizer);

	list_add_tail(&wq->queue, &item->link);
	semaphore_signal(&wq->sem);

	spinlock_release_irq_restore(&wq->lock, &irq_flags);
	return 0;
}

void workqueue_synchronize(struct workqueue* wq) {
	if (!wq)
		wq = system_workqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);

	bool reschedule = false;
	if (!list_empty(&wq->queue)) {
		bug(completion_wait_no_resched(&wq->synchronizer, 0) != 0);
		reschedule = true;
	}

	spinlock_release_irq_restore(&wq->lock, &irq_flags);

	if (reschedule)
		bug(schedule() != 0);
}

static void workqueue_init(void) {
	atomic_wq_item_cache = slab_cache_create(sizeof(struct workqueue_item),
			alignof(struct workqueue_item),
			MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
	if (!atomic_wq_item_cache)
		out_of_memory();

	system_workqueue = workqueue_create(0, "system");
	if (!system_workqueue)
		panic("Failed to create system workqueue");
}

INIT_TASK_DECLARE(heap_init_task, kthread_init_task);
INIT_TASK_DEFINE(workqueue_init_task, INIT_TASK_SCOPE_BSP, workqueue_init, &heap_init_task, &kthread_init_task);
