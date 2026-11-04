#pragma once

#define arch_atomic_thread_fence(order) __atomic_thread_fence(order)
#define arch_atomic_load(obj, order) __atomic_load_n(obj, order)
#define arch_atomic_store(obj, v, order) __atomic_store_n(obj, v, order)
#define arch_atomic_exchange(obj, v, order) __atomic_exchange_n(obj, v, order)
#define arch_atomic_fetch_add(obj, n, order) __atomic_fetch_add(obj, n, order)
#define arch_atomic_add_fetch(obj, n, order) __atomic_add_fetch(obj, n, order)
#define arch_atomic_fetch_sub(obj, n, order) __atomic_fetch_sub(obj, n, order)
#define arch_atomic_sub_fetch(obj, n, order) __atomic_sub_fetch(obj, n, order)
#define arch_atomic_fetch_and(obj, n, order) __atomic_fetch_and(obj, n, order)
#define arch_atomic_and_fetch(obj, n, order) __atomic_and_fetch(obj, n, order)
#define arch_atomic_fetch_or(obj, n, order) __atomic_fetch_or(obj, n, order)
#define arch_atomic_or_fetch(obj, n, order) __atomic_or_fetch(obj, n, order)
#define arch_atomic_fetch_xor(obj, n, order) __atomic_fetch_xor(obj, n, order)
#define arch_atomic_xor_fetch(obj, n, order) __atomic_xor_fetch(obj, n, order)
#define arch_atomic_fetch_nand(obj, n, order) __atomic_fetch_nand(obj, n, order)
#define arch_atomic_nand_fetch(obj, n, order) __atomic_nand_fetch(obj, n, order)
#define arch_atomic_compare_exchange_strong(obj, expected, desired, succ, fail) \
	__atomic_compare_exchange_n(obj, expected, desired, false, succ, fail)
#define arch_atomic_compare_exchange_weak(obj, expected, desired, succ, fail) \
	__atomic_compare_exchange_n(obj, expected, desired, true, succ, fail)
