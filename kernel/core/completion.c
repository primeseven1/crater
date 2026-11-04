#include <crater/completion.h>

void completion_signal(struct completion* completion) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&completion->lock, &irq_flags);

	completion->done = true;

	struct thread* it, *tmp;
	list_for_each_entry_safe(it, tmp, &completion->queue, block_link) {
		list_remove(&it->block_link);
		sched_wakeup(it, 0);
	}

	spinlock_release_irq_restore(&completion->lock, &irq_flags);
}

int completion_wait_no_resched(struct completion* completion, int flags) {
	if (flags & THREAD_STATE_FLAG_TIMEOUT)
		return -EINVAL;

	int ret = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&completion->lock, &irq_flags);

	if (!completion->done) {
		ret = sched_prepare_sleep(0, flags);
		list_add(&completion->queue, &current_thread()->block_link);
	} else {
		ret = -EALREADY;
	}

	spinlock_release_irq_restore(&completion->lock, &irq_flags);
	return ret;
}

int completion_wait(struct completion* completion, int flags) {
	int ret = completion_wait_no_resched(completion, flags);
	if (ret == 0)
		return schedule();
	if (ret == -EALREADY)
		return 0;
	return ret;
}

int completion_reset(struct completion* completion) {
	int err = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&completion->lock, &irq_flags);

	if (list_empty(&completion->queue))
		completion->done = false;
	else
		err = -EBUSY;

	spinlock_release_irq_restore(&completion->lock, &irq_flags);
	return err;
}
