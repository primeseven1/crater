#include <crater/types.h>
#include <crater/timekeeper.h>
#include <crater/init.h>
#include <crater/percpu.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/irq.h>

#include <arch/processor.h>

static inline time_t scale_ticks_to_ns(time_t ticks, unsigned long long freq) {
	return (ticks / freq) * 1000000000ull + (ticks % freq) * 1000000000ull / freq;
}

struct timespec time_fromboot(void) {
	unsigned long irq_flags = local_irq_save();
	struct timekeeper_source* source = current_cpu()->timekeeper;
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };
	time_t ticks = source ? source->ticks() : 0;
	local_irq_restore(irq_flags);

	time_t nsec = ticks ? scale_ticks_to_ns(ticks, source->fb_freq) : 0;
	ts.tv_sec = nsec / 1000000000ull;
	ts.tv_nsec = nsec % 1000000000ull;
	return ts;
}

void udelay(time_t usec) {
	if (usec <= 0)
		return;

	unsigned long irq_flags = local_irq_save();

	struct timekeeper_source* tk_src = current_cpu()->timekeeper;
	bug(tk_src == NULL);

	unsigned long long ticks_per_us = tk_src->fb_freq / 1000000;
	time_t start = tk_src->ticks();
	time_t end = start + (usec * ticks_per_us);

	while (tk_src->ticks() < end)
		arch_cpu_relax();

	local_irq_restore(irq_flags);
}

extern struct timekeeper _ld_kernel_timekeepers_start[];
extern struct timekeeper _ld_kernel_timekeepers_end[];

static struct timekeeper* __get_timekeeper(bool early) {
	struct timekeeper* best = NULL;

	size_t count = _ld_kernel_timekeepers_end - _ld_kernel_timekeepers_start;
	for (size_t i = 0; i < count; i++) {
		struct timekeeper* tk = &_ld_kernel_timekeepers_start[i];
		bool tk_early = !!(tk->flags & TIMEKEEPER_FLAG_EARLY);
		if (tk->rating == 0 || early != tk_early)
			continue;

		if (!best)
			best = tk;
		else if (tk->rating > best->rating)
			best = tk;
	}

	return best;
}

static struct timekeeper* get_timekeeper(bool early, struct timekeeper_source** out_src) {
	struct timekeeper* keeper = __get_timekeeper(early);
	while (keeper) {
		if (likely(keeper->dependencies))
			init_task_run_array(keeper->dependencies);
		else
			printk(PRINTK_WARN "timekeeper: timekeeper %s has no dependencies\n", keeper->name);
		if (keeper->init(keeper, out_src) == 0)
			break;
		keeper->rating = 0;
		keeper = __get_timekeeper(early);
	}
	return keeper;
}

static struct timekeeper* early_keeper = NULL;
static struct timekeeper* keeper = NULL;

static void timekeeper_init(void) {
	const char* name = NULL;

	/* The real timekeeper may need another working timekeeper */
	struct timekeeper_source* source;
	early_keeper = get_timekeeper(true, &source);
	if (unlikely(!early_keeper))
		panic("No early timekeeper");
	current_cpu()->timekeeper = source;
	name = early_keeper->name;

	struct timekeeper* real = get_timekeeper(false, &source);
	if (real) {
		name = real->name;
		keeper = real;
		current_cpu()->timekeeper = source;
	} else {
		keeper = early_keeper;
		if (unlikely(keeper->flags & TIMEKEEPER_FLAG_EARLY_ONLY))
			panic("No late timekeeper");
	}

	printk(PRINTK_INFO "timekeeper: Source %s chosen (frequency %llu mhz)\n", name, source->fb_freq / 1000000);
}

static void timekeeper_ap_init(void) {
	struct cpu* cpu = current_cpu();
	int err = early_keeper->init(early_keeper, &cpu->timekeeper);
	if (err)
		panic("Failed to initialize early timekeeper for AP\n");
	if (early_keeper != keeper) {
		err = keeper->init(keeper, &cpu->timekeeper);
		if (err)
			panic("Failed to initialize late timekeeper for AP\n");
	}
	printk(PRINTK_INFO "timekeeper: %s frequency %llu mhz on CPU %u\n",
			keeper->name, cpu->timekeeper->fb_freq / 1000000, cpu->runqueue.sched_id);
}

INIT_TASK_DEFINE(timekeeper_init_task, INIT_TASK_SCOPE_BSP, timekeeper_init);
INIT_TASK_DEFINE(timekeeper_ap_init_task, INIT_TASK_SCOPE_AP, timekeeper_ap_init, &timekeeper_init_task);
