#pragma once

#include <arch/atomic.h>

enum memory_order {
	memory_order_relaxed = __ATOMIC_RELAXED,
	memory_order_consume = __ATOMIC_CONSUME,
	memory_order_acquire = __ATOMIC_ACQUIRE,
	memory_order_release = __ATOMIC_RELEASE,
	memory_order_acq_rel = __ATOMIC_ACQ_REL,
	memory_order_seq_cst = __ATOMIC_SEQ_CST
};

#ifdef __CHECKER__
#define __atomic __attribute__((noderef))
#else /* __CHECKER__ */
#define __atomic
#endif /* __CHECKER__ */

#define atomic_init(v) (v)
#define atomic_thread_fence(order) arch_atomic_thread_fence(order)

#define atomic_load_explicit(obj, order) arch_atomic_load(obj, order)
#define atomic_store_explicit(obj, v, order) arch_atomic_store(obj, v, order)
#define atomic_exchange_explicit(obj, v, order) arch_atomic_exchange(obj, v, order)
#define atomic_fetch_add_explicit(obj, n, order) arch_atomic_fetch_add(obj, n, order)
#define atomic_add_fetch_explicit(obj, n, order) arch_atomic_add_fetch(obj, n, order)
#define atomic_fetch_sub_explicit(obj, n, order) arch_atomic_fetch_sub(obj, n, order)
#define atomic_sub_fetch_explicit(obj, n, order) arch_atomic_sub_fetch(obj, n, order)
#define atomic_fetch_and_explicit(obj, n, order) arch_atomic_fetch_and(obj, n, order)
#define atomic_and_fetch_explicit(obj, n, order) arch_atomic_and_fetch(obj, n, order)
#define atomic_fetch_or_explicit(obj, n, order) arch_atomic_fetch_or(obj, n, order)
#define atomic_or_fetch_explicit(obj, n, order) arch_atomic_or_fetch(obj, n, order)
#define atomic_fetch_xor_explicit(obj, n, order) arch_atomic_fetch_xor(obj, n, order)
#define atomic_xor_fetch_explicit(obj, n, order) arch_atomic_xor_fetch(obj, n, order)
#define atomic_fetch_nand_explicit(obj, n, order) arch_atomic_fetch_nand(obj, n, order)
#define atomic_nand_fetch_explicit(obj, n, order) arch_atomic_nand_fetch(obj, n, order)
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
	arch_atomic_compare_exchange_strong(obj, expected, desired, succ, fail)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
	arch_atomic_compare_exchange_weak(obj, expected, desired, succ, fail)

#define atomic_load(obj) atomic_load_explicit(obj, memory_order_seq_cst)
#define atomic_store(obj, v) atomic_store_explicit(obj, v, memory_order_seq_cst)
#define atomic_exchange(obj, v) atomic_exchange_explicit(obj, v, memory_order_seq_cst)
#define atomic_fetch_add(obj, n) atomic_fetch_add_explicit(obj, n, memory_order_seq_cst)
#define atomic_add_fetch(obj, n) atomic_add_fetch_explicit(obj, n, memory_order_seq_cst)
#define atomic_fetch_sub(obj, n) atomic_fetch_sub_explicit(obj, n, memory_order_seq_cst)
#define atomic_sub_fetch(obj, n) atomic_sub_fetch_explicit(obj, n, memory_order_seq_cst)
#define atomic_fetch_and(obj, n) atomic_fetch_and_explicit(obj, n, memory_order_seq_cst)
#define atomic_and_fetch(obj, n) atomic_and_fetch_explicit(obj, n, memory_order_seq_cst)
#define atomic_fetch_or(obj, n) atomic_fetch_or_explicit(obj, n, memory_order_seq_cst)
#define atomic_or_fetch(obj, n) atomic_or_fetch_explicit(obj, n, memory_order_seq_cst)
#define atomic_fetch_xor(obj, n) atomic_fetch_xor_explicit(obj, n, memory_order_seq_cst)
#define atomic_xor_fetch(obj, n) atomic_xor_fetch_explicit(obj, n, memory_order_seq_cst)
#define atomic_fetch_nand(obj, n) atomic_fetch_nand_explicit(obj, n, memory_order_seq_cst)
#define atomic_nand_fetch(obj, n) atomic_nand_fetch_explicit(obj, n, memory_order_seq_cst)
#define atomic_compare_exchange_strong(obj, expected, desired) \
	atomic_compare_exchange_strong_explicit(obj, expected, desired, memory_order_seq_cst, memory_order_seq_cst)
#define atomic_compare_exchange_weak(obj, expected, desired) \
	atomic_compare_exchange_weak_explicit(obj, expected, desired, memory_order_seq_cst, memory_order_seq_cst)
