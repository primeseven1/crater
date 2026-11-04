#pragma once

#include <arch/posix.h>

static inline time_t timespec_ns(struct timespec ts) {
	return (ts.tv_sec * 1000000000ll) + ts.tv_nsec;
}

static inline time_t timespec_us(struct timespec ts) {
	return (ts.tv_sec * 1000000ll) + (ts.tv_nsec / 1000l);
}

static inline time_t timespec_ms(struct timespec ts) {
	return (ts.tv_sec * 1000ll) + (ts.tv_nsec / 1000000l);
}

static inline struct timespec timespec_from_ns(time_t ns) {
	return (struct timespec){ .tv_sec = ns / 1000000000ll, .tv_nsec = ns % 1000000000l };
}

static inline struct timespec timespec_from_us(time_t us) {
	return (struct timespec){ .tv_sec = us / 1000000ll, .tv_nsec = (us % 1000000ll) * 1000l };
}

static inline struct timespec timespec_from_ms(time_t ms) {
	return (struct timespec){ .tv_sec = ms / 1000ll, .tv_nsec = (ms % 1000ll) * 1000000l };
}

static inline struct timespec timespec_add(struct timespec a, struct timespec b) {
	struct timespec ts = { .tv_sec = a.tv_sec + b.tv_sec, .tv_nsec = a.tv_nsec + b.tv_nsec };
	if (ts.tv_nsec >= 1000000000ll) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000ll;
	}
	return ts;
}

static inline struct timespec timespec_sub(struct timespec a, struct timespec b) {
	struct timespec ts = { .tv_sec = a.tv_sec - b.tv_sec, .tv_nsec = a.tv_nsec - b.tv_nsec };
	if (ts.tv_nsec < 0) {
		ts.tv_sec--;
		ts.tv_nsec += 1000000000ll;
	}
	return ts;
}

static inline int timespec_cmp(struct timespec a, struct timespec b) {
	if (a.tv_sec != b.tv_sec)
		return (a.tv_sec > b.tv_sec) ? 1 : -1;
	if (a.tv_nsec != b.tv_nsec)
		return (a.tv_nsec > b.tv_nsec) ? 1 : -1;
	return 0;
}
