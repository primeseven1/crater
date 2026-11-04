#include <crater/mutex.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/sched.h>

static bool __atomic use_spinlock = atomic_init(true);

void mutex_acquire(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, memory_order_relaxed))) {
		spinlock_acquire(&lock->spinlock);
		return;
	}

	struct thread* thread = current_thread();
	bug(atomic_load_explicit(&lock->owner, memory_order_relaxed) == thread);

	bug(semaphore_wait(&lock->sem, 0) != 0);
	atomic_store_explicit(&lock->owner, thread, memory_order_relaxed);
}

int mutex_acquire_safe(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, memory_order_relaxed))) {
		spinlock_acquire(&lock->spinlock);
		return 0;
	}

	struct thread* thread = current_thread();
	if (atomic_load_explicit(&lock->owner, memory_order_relaxed) == thread)
		return -EDEADLK;

	bug(semaphore_wait(&lock->sem, 0) != 0);
	atomic_store_explicit(&lock->owner, thread, memory_order_relaxed);
	return 0;
}

int mutex_acquire_timed(mutex_t* lock, time_t us) {
	if (unlikely(atomic_load_explicit(&use_spinlock, memory_order_relaxed))) {
		spinlock_acquire(&lock->spinlock);
		return 0;
	}

	struct thread* thread = current_thread();
	bug(atomic_load_explicit(&lock->owner, memory_order_relaxed) == thread);

	int res = semaphore_wait_timed(&lock->sem, us, 0);
	if (res == 0)
		atomic_store_explicit(&lock->owner, thread, memory_order_relaxed);
	return res;
}

bool mutex_try_acquire(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, memory_order_relaxed)))
		return spinlock_try_acquire(&lock->spinlock);

	struct thread* thread = current_thread();
	bug(atomic_load_explicit(&lock->owner, memory_order_relaxed) == thread);

	bool success = semaphore_try(&lock->sem);
	if (success)
		atomic_store_explicit(&lock->owner, thread, memory_order_relaxed);
	return success;
}

void mutex_release(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, memory_order_relaxed))) {
		spinlock_release(&lock->spinlock);
		return;
	}

	bug(atomic_exchange_explicit(&lock->owner, NULL, memory_order_relaxed) != current_thread());
	semaphore_signal(&lock->sem);
}

void mutex_disable_spinlock(void) {
	atomic_store_explicit(&use_spinlock, false, memory_order_relaxed);
}
