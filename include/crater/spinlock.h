#pragma once

#include <crater/types.h>
#include <crater/atomic.h>

typedef bool __atomic spinlock_t;
#define SPINLOCK_INITIALIZER atomic_init(false)
#define SPINLOCK_DEFINE(n) spinlock_t n = SPINLOCK_INITIALIZER

static inline void spinlock_init(spinlock_t* lock) {
	atomic_store_explicit(lock, false, memory_order_relaxed);
}

/**
 * @brief Acquire a spinlock
 * @param lock The lock to acqure
 */
void spinlock_acquire(spinlock_t* lock);

/**
 * @brief Release a spinlock
 * @param lock The lock to release
 */
void spinlock_release(spinlock_t* lock);

/**
 * @brief Acquire a spinlock if not already acquired
 * @param lock The lock to acquire
 * @return If the lock is acquired successfully, returns true, otherwise false
 */
bool spinlock_try_acquire(spinlock_t* lock);

/**
 * @brief Save current IRQ state, disable IRQ's, and then acquire the spinlock
 *
 * The flags must be a thread-local variable (on the stack). It does not need to be
 * initialized to anything.
 *
 * @param lock The lock to acquire
 * @param flags IRQ flags
 */
void spinlock_acquire_irq_save(spinlock_t* lock, unsigned long* flags);

/**
 * @brief Release spinlock, and restore the IRQ state
 *
 * @param lock The lock to release
 * @param flags Whatever variable was passed to spinlock_acquire_irq_save()
 */
void spinlock_release_irq_restore(spinlock_t* lock, unsigned long* flags);

/**
 * @brief Like spinlock_acquire_irq_save(), but does not spin if the lock isn't available
 *
 * On failure, the previous IRQ state is restored
 *
 * @param lock The lock to acquire
 * @param flags IRQ flags
 *
 * @return If the lock is acquired successfully, returns true, otherwise false
 */
bool spinlock_try_acquire_irq_save(spinlock_t* lock, unsigned long* flags);

/**
 * @brief Disable preempt and acquire a spinlock
 * @param lock The lock to acquire
 */
void spinlock_acquire_preempt_disable(spinlock_t* lock);

/**
 * @brief Release a spinlock and enable preempt
 * @param lock The lock to release
 */
void spinlock_release_preempt_enable(spinlock_t* lock);

/**
 * @brief Try to acquire a spinlock, but disable preempt if acquired
 * @param lock The lock to acquire
 * @return true if the lock is acquired, otherwise false
 */
bool spinlock_try_acquire_preempt_disable(spinlock_t* lock);

/**
 * @brief Acquire a spinlock and disable softirq's
 *
 * This function will also disable preemption.
 *
 * @param lock The lock to acquire
 */
void spinlock_acquire_softirq_disable(spinlock_t* lock);

/**
 * @brief Release a spinlock and re-enable softirq's
 *
 * This function re-enables preemption.
 *
 * @param lock The lock to acquire
 */
void spinlock_release_softirq_enable(spinlock_t* lock);

/**
 * @brief Try to acquire a spinlock without spinning, also disabling softirq's
 *
 * This function also disables preemption on a successful acquire.
 *
 * @param lock The lock to acquire
 * @return true if the lock is acquired, otherwise false
 */
bool spinlock_try_acquire_softirq_disable(spinlock_t* lock);
