#include <crater/cmdline.h>
#include <crater/timekeeper.h>
#include <crater/percpu.h>
#include <crater/slab.h>
#include <x86_64/asm/cpuid.h>

static bool tsc_usable(void) {
	/* On CPU's with multiple cores, the TSC's may have issues with drift, so this timekeeper can be disabled */
	const char* cmdline_tsc = cmdline_get("timekeeper.tsc_enable");
	if (cmdline_tsc && *cmdline_tsc == '0')
		return false;

	u32 eax, edx, _unused;
	arch_x86_64_cpuid(CPUID_EXT_LEAF_HIGHEST_FUNCTION, 0, &eax, &_unused, &_unused, &_unused);
	if (eax < CPUID_EXT_LEAF_PM_FEATURES)
		return false;

	arch_x86_64_cpuid(CPUID_EXT_LEAF_PM_FEATURES, 0, &_unused, &_unused, &_unused, &edx);
	return !!(edx & (1 << 8)); /* Check for invariant TSC */
}

static u64 rdtsc_serialized(void) {
	u32 low, high;
	__asm__("cpuid\n\t" /* cpuid is a serializing instruction */
			"rdtsc\n\t"
			: "=a"(low), "=d"(high)
			:
			: "memory", "ebx", "ecx");
	return ((u64)high << 32) | low;
}

struct tsc_priv {
	u64 offset;
};

static time_t fb_ticks(void) {
	struct timekeeper_source* source = current_cpu()->timekeeper;
	u64 offset = ((struct tsc_priv*)source->private)->offset;
	return rdtsc_serialized() - offset;
}

static inline unsigned long long get_freq_from_cpuid(u32 eax, u32 ebx, u32 ecx) {
	if (eax == 0 || ebx == 0 || ecx == 0)
		return 0;

	return (unsigned long long)ecx * ebx / eax;
}

static inline unsigned long long get_freq_from_calibration(void) {
	time_t usec = 50000;
	u64 sum = 0;
	const int sample_count = 5;
	for (int i = 0; i < sample_count; i++) {
		u64 start = rdtsc_serialized();
		udelay(usec); /* Safe since this isn't suitable as an early timekeeper */
		u64 end = rdtsc_serialized();
		sum += end - start;
	}
	return (unsigned long long)(sum * 1000000ull / ((u64)usec * sample_count));
}

static u64 get_bsp_offset(void) {
	if (current_cpu()->runqueue.sched_id == 0)
		return rdtsc_serialized();

	struct smp_cpus cpus;
	smp_cpus_read_acquire(&cpus);
	struct cpu* bsp = cpus.cpus[0];
	smp_cpus_read_release(&cpus);

	return ((struct tsc_priv*)bsp->timekeeper->private)->offset;
}

static int init(struct timekeeper* self, struct timekeeper_source** out) {
	(void)self;
	if (!tsc_usable())
		return -ENODEV;

	struct timekeeper_source* source = kmalloc(sizeof(*source), MM_ZONE_NORMAL);
	if (!source)
		return -ENOMEM;

	unsigned long long freq;

	u32 eax, ebx, ecx, _unused;
	arch_x86_64_cpuid(CPUID_LEAF_HIGHEST_FUNCTION, 0, &eax, &_unused, &_unused, &_unused);
	if (eax >= CPUID_LEAF_TSC_FREQ) {
		arch_x86_64_cpuid(0x15, 0, &eax, &ebx, &ecx, &_unused);
		freq = get_freq_from_cpuid(eax, ebx, ecx);
		if (freq == 0)
			freq = get_freq_from_calibration();
	} else {
		freq = get_freq_from_calibration();
	}

	source->private = kmalloc(sizeof(struct tsc_priv), MM_ZONE_NORMAL);
	if (!source->private) {
		kfree(source);
		return -ENOMEM;
	}
	((struct tsc_priv*)source->private)->offset = get_bsp_offset();

	source->ticks = fb_ticks;
	source->fb_freq = freq;
	*out = source;
	return 0;
}

INIT_TASK_DECLARE(heap_init_task, cmdline_init_task);
static INIT_TASK_ARRAY_DEFINE(tsc_depends, &heap_init_task, &cmdline_init_task);

static struct timekeeper __timekeeper tsc_timekeeper = {
	.name = "tsc",
	.init = init,
	.dependencies = tsc_depends,
	.flags = 0,
	.rating = 100,
};
