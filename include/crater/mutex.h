#pragma once

#include <crater/semaphore.h>

typedef struct {
	struct thread* __atomic owner;
	struct semaphore sem;
	spinlock_t spinlock;
} mutex_t;

#define MUTEX_DEFINE(n) mutex_t n = { .sem = SEMAPHORE_INITIALIZER(n.sem, 1), .owner = atomic_init(NULL), .spinlock = SPINLOCK_INITIALIZER }
#define MUTEX_INITIALIZER(n) { .owner = atomic_init(NULL), .sem = SEMAPHORE_INITIALIZER(n.sem, 1), .spinlock = SPINLOCK_INITIALIZER }
static inline void mutex_init(mutex_t* lock) {
	semaphore_init(&lock->sem, 1);
	atomic_store_explicit(&lock->owner, NULL, memory_order_relaxed);
	spinlock_init(&lock->spinlock);
}

/**
 * @brief Acquire a mutex
 * @param lock The lock to acquire
 */
void mutex_acquire(mutex_t* lock);

/**
 * @brief Acquire a mutex
 *
 * @param lock The lock to acquire
 *
 * @retval 0 Successful
 * @retval -EDEADLK Avoided a deadlock
 */
int mutex_acquire_safe(mutex_t* lock);

/**
 * @brief Acquire a mutex, but with a timeout
 *
 * @param lock The lock to acquire
 * @param us The number of microseconds before a timeout
 *
 * @retval 0 Successful
 * @retval -ETIME Timed out
 * @retval -ENOMEM Usually because a timer event could not be allocated
 */
int mutex_acquire_timed(mutex_t* lock, time_t us);

/**
 * @brief Try to acquire a mutex
 * @param lock The lock to acquire
 * @retval true Acquired the lock
 * @retval false Failed to acquire the lock
 */
bool mutex_try_acquire(mutex_t* lock);

/**
 * @brief Release a mutex
 * @param lock The lock to release
 */
void mutex_release(mutex_t* lock);

/**
 * @brief Swap from spinlocks to actual mutexes
 *
 * Only called when no mutexes are held
 */
void mutex_disable_spinlock(void);
