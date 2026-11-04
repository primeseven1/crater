#include <crater/waitq.h>
#include <crater/timekeeper.h>

bool waitq_default_cb(struct waitq_entry* entry, unsigned long key) {
	(void)key;
	if (entry->flags & WAITQ_ENTRY_WOKEN)
		return false;

	entry->flags |= WAITQ_ENTRY_WOKEN;
	sched_wakeup(entry->thread, 0);
	return true;
}

void waitq_entry_init(struct waitq_entry* entry, struct thread* thread, bool (*cb)(struct waitq_entry*, unsigned long), void* private) {
	entry->thread = thread;
	entry->cb = cb ? cb : waitq_default_cb;
	entry->private = private;
	entry->flags = 0;
	list_node_init(&entry->link);
}

void waitq_add_locked(struct waitq* wq, struct waitq_entry* entry) {
	if (entry->flags & WAITQ_ENTRY_EXCLUSIVE)
		list_add_tail(&wq->entries, &entry->link);
	else
		list_add(&wq->entries, &entry->link);
}

void waitq_add(struct waitq* wq, struct waitq_entry* entry) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);
	waitq_add_locked(wq, entry);
	spinlock_release_irq_restore(&wq->lock, &irq_flags);
}

void waitq_remove_locked(struct waitq_entry* entry) {
	if (list_node_linked(&entry->link))
		list_remove(&entry->link);
}
 
void waitq_remove(struct waitq* wq, struct waitq_entry* entry) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);
	waitq_remove_locked(entry);
	spinlock_release_irq_restore(&wq->lock, &irq_flags);
}

unsigned int waitq_wake_locked(struct waitq* wq, unsigned int nr_exclusive, unsigned long key) {
	unsigned int nr_woken = 0;
	struct waitq_entry* entry, *tmp;
	list_for_each_entry_safe(entry, tmp, &wq->entries, link) {
		const bool exclusive = !!(entry->flags & WAITQ_ENTRY_EXCLUSIVE);
		if (!entry->cb(entry, key))
			continue;
		nr_woken++;
		if (exclusive && nr_exclusive && --nr_exclusive == 0)
			break; /* Exclusive entries are always added to the tail, so breaking is safe */
	}

	return nr_woken;
}
 
unsigned int waitq_wake(struct waitq* wq, unsigned int nr_exclusive, unsigned long key) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&wq->lock, &irq_flags);
	unsigned int nr_woken = waitq_wake_locked(wq, nr_exclusive, key);
	spinlock_release_irq_restore(&wq->lock, &irq_flags);
	return nr_woken;
}

int __waitq_wait_start(struct __waitq_wait* wait, struct waitq* wq, bool (*cb)(struct waitq_entry*, unsigned long), void* private, bool exclusive, bool timed, time_t us, int flags) {
	bug(in_interrupt());
	if (flags & THREAD_STATE_FLAG_TIMEOUT)
		return -EINVAL;
	if (timed && us < 0)
		return -EINVAL;

	wait->wq = wq;
	waitq_entry_init(&wait->entry, current_thread(), cb, private);
	if (exclusive)
		wait->entry.flags |= WAITQ_ENTRY_EXCLUSIVE;
	wait->timed = timed;
	if (timed)
		wait->deadline = timespec_add(time_fromboot(), timespec_from_us(us));
	wait->flags = flags;

	spinlock_acquire_irq_save(&wq->lock, &wait->irq_flags);
	waitq_add_locked(wq, &wait->entry);
	return 0;
}
 
int __waitq_wait_sleep(struct __waitq_wait* wait) {
	time_t us = 0;
	int flags = wait->flags;
 
	if (wait->timed) {
		const struct timespec now = time_fromboot();
		if (timespec_cmp(now, wait->deadline) >= 0)
			return -ETIME;
		us = timespec_us(timespec_sub(wait->deadline, now));
		if (us == 0)
			us = 1;
		flags |= THREAD_STATE_FLAG_TIMEOUT;
	}
 
	wait->entry.flags &= ~WAITQ_ENTRY_WOKEN;
	int err = sched_prepare_sleep(us, flags);
	if (err)
		return err;
 
	spinlock_release_irq_restore(&wait->wq->lock, &wait->irq_flags);
	err = schedule();
	spinlock_acquire_irq_save(&wait->wq->lock, &wait->irq_flags);
 
	return err;
}
 
int __waitq_wait_finish(struct __waitq_wait* wait, int err) {
	struct waitq* wq = wait->wq;
	struct waitq_entry* entry = &wait->entry;
 
	waitq_remove_locked(entry);

	/* Check if an exclusive waiter took a wakeup it isn't going to use. If so, pass it on */
	const int mask = WAITQ_ENTRY_EXCLUSIVE | WAITQ_ENTRY_WOKEN;
	if (err && (entry->flags & mask) == mask)
		waitq_wake_locked(wq, 1, 0);
 
	spinlock_release_irq_restore(&wq->lock, &wait->irq_flags);
	return err;
}
