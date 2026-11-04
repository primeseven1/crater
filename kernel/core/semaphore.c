#include <crater/semaphore.h>
#include <crater/sched.h>

static int handle_wakeup_race(struct semaphore* sem, struct thread* current, int wakeup_errno) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&sem->lock, &irq_flags);

	if (wakeup_errno == -EINTR || wakeup_errno == -ETIME) {
		if (list_node_linked(&current->block_link)) {
			list_remove(&current->block_link);
			sem->count++;
		} else {
			wakeup_errno = 0;
		}

	}

	spinlock_release_irq_restore(&sem->lock, &irq_flags);
	return wakeup_errno;
}

int semaphore_wait(struct semaphore* sem, int flags) {
	if (flags & THREAD_STATE_FLAG_TIMEOUT)
		return -EINVAL;

	int errno = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&sem->lock, &irq_flags);

	if (--sem->count < 0) {
		struct thread* thread = current_thread();

		list_add_tail(&sem->wait_queue, &thread->block_link);
		errno = sched_prepare_sleep(0, flags);
		if (likely(errno == 0)) {
			spinlock_release_irq_restore(&sem->lock, &irq_flags);
			errno = schedule();
			bug(errno == -ETIME);
			if (errno == -EINTR)
				bug(!(flags & THREAD_STATE_FLAG_INTERRUPTIBLE));
			return handle_wakeup_race(sem, thread, errno);
		} else {
			list_remove(&thread->block_link);
			sem->count++;
		}
	}

	spinlock_release_irq_restore(&sem->lock, &irq_flags);
	return errno;
}

int semaphore_wait_timed(struct semaphore* sem, time_t us, int flags) {
	int errno = 0;
	struct thread* thread = current_thread();

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&sem->lock, &irq_flags);

	if (--sem->count >= 0) {
		spinlock_release_irq_restore(&sem->lock, &irq_flags);
		return 0;
	} else if (us == 0) {
		sem->count++;
		spinlock_release_irq_restore(&sem->lock, &irq_flags);
		return -ETIME;
	}

	flags |= THREAD_STATE_FLAG_TIMEOUT;
	list_add_tail(&sem->wait_queue, &thread->block_link);
	errno = sched_prepare_sleep(us, flags);
	if (errno) {
		list_remove(&thread->block_link);
		spinlock_release_irq_restore(&sem->lock, &irq_flags);
		return errno;
	}

	spinlock_release_irq_restore(&sem->lock, &irq_flags);

	errno = schedule();
	if (errno == -EINTR)
		bug(!(flags & THREAD_STATE_FLAG_INTERRUPTIBLE));
	return handle_wakeup_race(sem, thread, errno);
}

void semaphore_signal(struct semaphore* sem) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&sem->lock, &irq_flags);

	if (++sem->count <= 0 && !list_empty(&sem->wait_queue)) {
		struct thread* thread = list_first_entry(&sem->wait_queue, struct thread, block_link);
		list_remove(&thread->block_link);
		sched_wakeup(thread, 0);
	}

	spinlock_release_irq_restore(&sem->lock, &irq_flags);
}

int semaphore_reset(struct semaphore* sem) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&sem->lock, &irq_flags);

	int ret = -EBUSY;
	if (!list_empty(&sem->wait_queue))
		goto out;

	ret = 0;
	sem->count = 0;
out:
	spinlock_release_irq_restore(&sem->lock, &irq_flags);
	return ret;
}

bool semaphore_try(struct semaphore* sem) {
	bool ret = false;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&sem->lock, &irq_flags);

	if (sem->count > 0) {
		sem->count--;
		ret = true;
	}

	spinlock_release_irq_restore(&sem->lock, &irq_flags);
	return ret;
}
