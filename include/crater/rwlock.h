#pragma once

#include <crater/types.h>
#include <crater/atomic.h>
#include <arch/irq_flags.h>

typedef long __atomic rwlock_t;

#define RWLOCK_WRITER_HELD LONG_MIN
#define RWLOCK_WRITER_PENDING (1l << 0)
#define RWLOCK_READER_PLUS_ONE 2l /* Keep bit zero free for the pending flag */

#define RWLOCK_INITIALIZER atomic_init(0)
#define RWLOCK_DEFINE(n) rwlock_t n = RWLOCK_INITIALIZER

static inline void rwlock_init(rwlock_t* lock) {
	atomic_store_explicit(lock, 0, memory_order_relaxed);
}

void rwlock_read_acquire(rwlock_t* lock);
void rwlock_read_release(rwlock_t* lock);
bool rwlock_try_read_acquire(rwlock_t* lock);
void rwlock_write_acquire(rwlock_t* lock);
void rwlock_write_release(rwlock_t* lock);
bool rwlock_try_write_acquire(rwlock_t* lock);

void rwlock_read_acquire_irq_save(rwlock_t* lock, unsigned long* flags);
void rwlock_read_release_irq_restore(rwlock_t* lock, unsigned long* flags);
bool rwlock_try_read_acquire_irq_save(rwlock_t* lock, unsigned long* flags);
void rwlock_write_acquire_irq_save(rwlock_t* lock, unsigned long* flags);
void rwlock_write_release_irq_restore(rwlock_t* lock, unsigned long* flags);
bool rwlock_try_write_acquire_irq_save(rwlock_t* lock, unsigned long* flags);

void rwlock_read_acquire_preempt_disable(rwlock_t* lock);
void rwlock_read_release_preempt_enable(rwlock_t* lock);
bool rwlock_try_read_acquire_preempt_disable(rwlock_t* lock);
void rwlock_write_acquire_preempt_disable(rwlock_t* lock);
void rwlock_write_release_preempt_enable(rwlock_t* lock);
bool rwlock_try_write_acquire_preempt_disable(rwlock_t* lock);
