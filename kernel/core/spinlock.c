#include <crater/spinlock.h>
#include <crater/sched.h>
#include <crater/irq.h>
#include <arch/processor.h>

void spinlock_acquire(spinlock_t* lock) {
	while (atomic_exchange_explicit(lock, true, memory_order_acquire)) {
		while (atomic_load_explicit(lock, memory_order_relaxed))
			arch_cpu_relax();
	}
}

void spinlock_release(spinlock_t* lock) {
	atomic_store_explicit(lock, false, memory_order_release);
}

bool spinlock_try_acquire(spinlock_t* lock) {
	return !atomic_exchange_explicit(lock, true, memory_order_acquire);
}

void spinlock_acquire_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	spinlock_acquire(lock);
}

void spinlock_release_irq_restore(spinlock_t* lock, unsigned long* flags) {
	spinlock_release(lock);
	local_irq_restore(*flags);
}

bool spinlock_try_acquire_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	if (spinlock_try_acquire(lock))
		return true;
	local_irq_restore(*flags);
	return false;
}

void spinlock_acquire_preempt_disable(spinlock_t* lock) {
	preempt_disable();
	spinlock_acquire(lock);
}

void spinlock_release_preempt_enable(spinlock_t* lock) {
	spinlock_release(lock);
	preempt_enable();
}

bool spinlock_try_acquire_preempt_disable(spinlock_t* lock) {
	preempt_disable();
	if (spinlock_try_acquire(lock))
		return true;
	preempt_enable();
	return false;
}

void spinlock_acquire_softirq_disable(spinlock_t* lock) {
	local_softirq_disable();
	spinlock_acquire_preempt_disable(lock);
}

void spinlock_release_softirq_enable(spinlock_t* lock) {
	spinlock_release_preempt_enable(lock);
	local_softirq_enable();
}

bool spinlock_try_acquire_softirq_disable(spinlock_t* lock) {
	local_softirq_disable();
	if (spinlock_try_acquire_preempt_disable(lock))
		return true;
	local_softirq_enable();
	return false;
}
