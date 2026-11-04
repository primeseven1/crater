#pragma once

#include <crater/init.h>
#include <crater/time.h>

#define __timekeeper __attribute__((section(".timekeepers"), aligned(8), used))

#define TIMEKEEPER_FLAG_EARLY (1 << 0)
#define TIMEKEEPER_FLAG_EARLY_ONLY (1 << 1)

struct timekeeper;

struct timekeeper_source {
	time_t (*ticks)(void);
	unsigned long long fb_freq;
	void* private;
};

struct timekeeper {
	const char* name;
	int (*init)(struct timekeeper*, struct timekeeper_source**);
	unsigned int rating; /* Changes to zero if init fails */
	int flags; /* These flags may change during initialization */
	struct init_task** dependencies;
};

struct timespec time_fromboot(void);

void udelay(time_t us);

static inline void ndelay(time_t ns) {
	udelay((ns + 999) / 1000);
}

static inline void mdelay(time_t ms) {
	udelay(ms * 1000);
}
