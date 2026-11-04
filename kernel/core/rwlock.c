#include <crater/rwlock.h>
#include <crater/sched.h>
#include <crater/irq.h>

static inline bool writer_pending(long state) {
	return !!(state & RWLOCK_WRITER_PENDING);
}

static inline bool writer_held(long state) {
	return state == RWLOCK_WRITER_HELD;
}

void rwlock_read_acquire(rwlock_t* lock) {
	while (1) {
		long s = atomic_load_explicit(lock, memory_order_relaxed);
		if (writer_held(s) || writer_pending(s)) {
			arch_cpu_relax();
			continue;
		}
		if (atomic_compare_exchange_weak_explicit(lock, &s, s + RWLOCK_READER_PLUS_ONE, memory_order_acquire, memory_order_relaxed))
			return;

		arch_cpu_relax();
	}
}

void rwlock_read_release(rwlock_t* lock) {
	atomic_fetch_sub_explicit(lock, RWLOCK_READER_PLUS_ONE, memory_order_release);
}

bool rwlock_try_read_acquire(rwlock_t* lock) {
	long s = atomic_load_explicit(lock, memory_order_relaxed);
	if (writer_held(s) || writer_pending(s))
		return false;
	/* If the state changes, the CAS will fail */
	return atomic_compare_exchange_strong_explicit(lock, &s, s + RWLOCK_READER_PLUS_ONE, memory_order_acquire, memory_order_relaxed);
}

void rwlock_write_acquire(rwlock_t* lock) {
	atomic_fetch_or_explicit(lock, RWLOCK_WRITER_PENDING, memory_order_relaxed);
	while (1) {
		long expected = RWLOCK_WRITER_PENDING;
		if (atomic_compare_exchange_weak_explicit(lock, &expected, RWLOCK_WRITER_HELD, memory_order_acquire, memory_order_relaxed))
			return;
		if (expected == 0)
			atomic_fetch_or_explicit(lock, RWLOCK_WRITER_PENDING, memory_order_relaxed);
		arch_cpu_relax();
	}
}

void rwlock_write_release(rwlock_t* lock) {
	atomic_store_explicit(lock, 0, memory_order_release);
}

bool rwlock_try_write_acquire(rwlock_t* rw) {
	long expected = 0;
	return atomic_compare_exchange_strong_explicit(rw, &expected, RWLOCK_WRITER_HELD, memory_order_acquire, memory_order_relaxed);
}

void rwlock_read_acquire_irq_save(rwlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	rwlock_read_acquire(lock);
}

void rwlock_read_release_irq_restore(rwlock_t* lock, unsigned long* flags) {
	rwlock_read_release(lock);
	local_irq_restore(*flags);
}

bool rwlock_try_read_acquire_irq_save(rwlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	if (rwlock_try_read_acquire(lock))
		return true;
	local_irq_restore(*flags);
	return false;
}

void rwlock_write_acquire_irq_save(rwlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	rwlock_write_acquire(lock);
}

void rwlock_write_release_irq_restore(rwlock_t* lock, unsigned long* flags) {
	rwlock_write_release(lock);
	local_irq_restore(*flags);
}

bool rwlock_try_write_acquire_irq_save(rwlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	if (rwlock_try_write_acquire(lock))
		return true;
	local_irq_restore(*flags);
	return false;
}

void rwlock_read_acquire_preempt_disable(rwlock_t* lock) {
	preempt_disable();
	rwlock_read_acquire(lock);
}
void rwlock_read_release_preempt_enable(rwlock_t* lock) {
	rwlock_read_release(lock);
	preempt_enable();
}

bool rwlock_try_read_acquire_preempt_disable(rwlock_t* lock) {
	preempt_disable();
	if (rwlock_try_read_acquire(lock))
		return true;
	preempt_enable();
	return false;
}

void rwlock_write_acquire_preempt_disable(rwlock_t* lock) {
	preempt_disable();
	rwlock_write_acquire(lock);
}

void rwlock_write_release_preempt_enable(rwlock_t* lock) {
	rwlock_write_release(lock);
	preempt_enable();
}

bool rwlock_try_write_acquire_preempt_disable(rwlock_t* lock) {
	preempt_disable();
	if (rwlock_try_write_acquire(lock))
		return true;
	preempt_enable();
	return false;
}
