#pragma once

#define NULL ((void*)0)
#define BIGGEST_ALIGNMENT __BIGGEST_ALIGNMENT__

#define static_assert(e, ...) _Static_assert(e, #__VA_ARGS__)
#define offsetof(t, m) __builtin_offsetof(t, m)
#define typeof(e) __typeof__(e)
#define alignof(t) _Alignof(t)
#define container_of(ptr, type, member) ((type*)((char*)(ptr) - offsetof(type, member)))
#define sizeof_field(type, member) sizeof(((type*)0)->member)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ROUND_DOWN(v, n) ((v) - ((v) % (n)))
#define ROUND_UP(v, n) ROUND_DOWN((v) + (n) - 1, n)
#define MIN(a, b) \
	({ \
		typeof(a) ____a = (a); \
		typeof(b) ____b = (b); \
		(____a < ____b) ? ____a : ____b; \
	})
#define MAX(a, b) \
	({ \
		typeof(a) ____a = (a); \
		typeof(b) ____b = (b); \
		(____a > ____b) ? ____a : ____b; \
	})

/**
 * @brief Round a value up to a power of two
 * @param x The value to round
 * @return The next power of two, zero is returned if overflow will occur
 */
static inline unsigned long long roundup_pow2(unsigned long long x) {
	if (x <= 1)
		return 1;
	const unsigned long long ullong_bits = sizeof(unsigned long long) * 8;
	if (x > (1ull << (ullong_bits - 1)))
		return 0;
	return 1ull << (ullong_bits - __builtin_clzll(x - 1));
}

/**
 * @brief Round a number up to a power of two, but return a minimum value
 *
 * @param min The minimum value to return, must be a power of two
 * @param x The value to round
 *
 * @return The next power of two, based on the minimum. 0 is returned if min is not a power of two, or overflow occurs
 */
static inline unsigned long long roundup_pow2_minimum(unsigned long long min, unsigned long long x) {
	if (min == 0 || (min & (min - 1)) != 0)
		return 0;
	if (x < min)
		return min;
	return roundup_pow2(x);
}
