#include <crater/timer.h>
#include <crater/init.h>
#include <crater/panic.h>
#include <crater/percpu.h>
#include <crater/time.h>
#include <crater/timekeeper.h>
#include <crater/printk.h>
#include <crater/irq.h>

struct timer_event {
	struct cpu* cpu;
	struct timespec expiration;
	struct timer* timer;
	struct timer_event_handler handler;
	int flags;
	struct slab_cache* cache;
	struct list_node link;
};

static LIST_HEAD_DEFINE(timer_list);
static LIST_HEAD_DEFINE(event_list);
static SPINLOCK_DEFINE(timer_lock);

static struct timer* pick_timer(int flags) {
	static const struct {
		int feat;
		int opt;
	} flag_pairs[] = {
		{ .feat = TIMER_FLAG_PERCPU, .opt = 1u << __TIMER_FLAG_PERCPU_BIT_OPTIONAL },
		{ .feat = TIMER_FLAG_HIGH_PRECISION, .opt = 1u << __TIMER_FLAG_HIGH_PRECISION_BIT_OPTIONAL }
	};

	struct timer* ret = NULL;
	int best_score = -1;

	int required = 0;
	for (size_t i = 0; i < ARRAY_SIZE(flag_pairs); i++) {
		if ((flags & flag_pairs[i].feat) && !(flags & flag_pairs[i].opt))
			required |= flag_pairs[i].feat;
	}

	struct timer* t;
	list_for_each_entry(t, &timer_list, link) {
		if ((t->flags & required) != required)
			continue;

		int score = 0;
		for (size_t i = 0; i < ARRAY_SIZE(flag_pairs); i++) {
			int feat = flag_pairs[i].feat;
			int opt = flag_pairs[i].opt;
			if ((flags & opt) && (t->flags & feat))
				score++;
		}

		if (score > best_score) {
			best_score = score;
			ret = t;
		}
	}

	return ret;
}

static struct timer_event* earliest_event(struct timer* timer) {
	struct list_head* list = (timer->flags & TIMER_FLAG_PERCPU) ? &current_cpu()->timer_event_list : &event_list;
	struct timer_event* earliest = NULL;
	struct timer_event* event;
	list_for_each_entry(event, list, link) {
		if (event->timer != timer)
			continue;
		if (!earliest || timespec_cmp(earliest->expiration, event->expiration) > 0)
			earliest = event;
	}
	return earliest;
}

static struct slab_cache* event_cache;
static struct slab_cache* atomic_event_cache;

static void timer_softirq(void) {
	struct timer_event* events[5] = { NULL, NULL, NULL, NULL, NULL };
	local_irq_disable();
	struct cpu* cpu = current_cpu();
	struct timer_event* ev, *tmp;
	size_t i = 0;
	list_for_each_entry_safe(ev, tmp, &cpu->softirq_timer_cb_list, link) {
		if (i >= ARRAY_SIZE(events))
			break;
		events[i++] = ev;
		list_remove(&ev->link);
	}
	local_irq_enable();

	for (i = 0; i < ARRAY_SIZE(events); i++) {
		ev = events[i];
		if (!ev)
			break;
		int flags = ev->flags;
		ev->handler.fn(ev, ev->handler.arg);
		if (flags & TIMER_FLAG_EVENT_ALLOC_AUTOFREE)
			free_timer_event_handle(ev);
	}

	local_irq_disable();
	if (!list_empty(&cpu->softirq_timer_cb_list))
		softirq_raise(SOFTIRQ_TIMER);
	local_irq_enable();
}

static void do_percpu(void) {
	struct cpu* cpu = current_cpu();
	struct timespec now = time_fromboot();

	struct timer_event* ev, *tmp;
	list_for_each_entry_safe(ev, tmp, &cpu->timer_event_list, link) {
		if (timespec_cmp(now, ev->expiration) >= 0) {
			list_remove(&ev->link);
			int flags = ev->flags;
			if (flags & TIMER_FLAG_HARDIRQ) {
				ev->handler.fn(ev, ev->handler.arg);
				if (flags & TIMER_FLAG_EVENT_ALLOC_AUTOFREE)
					free_timer_event_handle(ev);
			} else {
				list_add_tail(&cpu->softirq_timer_cb_list, &ev->link);
				softirq_raise(SOFTIRQ_TIMER);
			}
		}
	}
}

static void do_global(void) {
	struct timespec now = time_fromboot();

	while (1) {
		/* TODO: Batch these */
		spinlock_acquire(&timer_lock);
		struct timer_event* ev = NULL;
		struct timer_event* it, *tmp;
		list_for_each_entry_safe(it, tmp, &event_list, link) {
			if (timespec_cmp(now, it->expiration) >= 0) {
				list_remove(&it->link);
				ev = it;
				break;
			}
		}
		spinlock_release(&timer_lock);

		if (!ev)
			break;

		int flags = ev->flags;
		if (flags & TIMER_FLAG_HARDIRQ) {
			ev->handler.fn(ev, ev->handler.arg);
			if (flags & TIMER_FLAG_EVENT_ALLOC_AUTOFREE)
				free_timer_event_handle(ev);
		} else {
			list_add_tail(&current_cpu()->softirq_timer_cb_list, &ev->link);
			softirq_raise(SOFTIRQ_TIMER);
		}
	}
}

static void arm_earliest_event_locked(struct timer* timer) {
	struct timer_event* earliest = earliest_event(timer);
	if (!earliest)
		return;

	struct timespec now = time_fromboot();
	struct timespec t = timespec_cmp(now, earliest->expiration) >= 0 ?
		(struct timespec){ .tv_sec = 0, .tv_nsec = 0 } : timespec_sub(earliest->expiration, now);

	int err = timer->ops->arm(timer, t, earliest);
	if (err == -EBUSY)
		err = timer->ops->rearm(timer, t, earliest);
	bug(err != 0 && err != -EALREADY);
}

void do_timer_events(struct timer* source) {
	bool percpu = !!(source->flags & TIMER_FLAG_PERCPU);
	if (percpu) {
		do_percpu();
	} else {
		do_global();
		spinlock_acquire(&timer_lock);
	}

	arm_earliest_event_locked(source);

	if (!percpu)
		spinlock_release(&timer_lock);
}

void* alloc_timer_event_handle(int flags) {
	if (flags & TIMER_FLAG_EVENT_ALLOC_AUTOFREE)
		flags |= TIMER_FLAG_EVENT_ALLOC_ATOMIC;

	struct slab_cache* cache = (flags & TIMER_FLAG_EVENT_ALLOC_ATOMIC) ? atomic_event_cache : event_cache;
	struct timer_event* ret = slab_cache_alloc(cache);
	if (ret) {
		ret->cpu = NULL;
		ret->expiration = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 };
		ret->timer = NULL;
		ret->handler = (struct timer_event_handler){ .fn = NULL, .arg = NULL };
		ret->flags = flags & (TIMER_FLAG_EVENT_ALLOC_ATOMIC | TIMER_FLAG_EVENT_ALLOC_AUTOFREE);
		ret->cache = cache;
		list_node_init(&ret->link);
	}
	return ret;
}

void free_timer_event_handle(void* handle) {
	slab_cache_free(((struct timer_event*)handle)->cache, handle);
}

int arm_timer_event_handle(void* handle, time_t us, const struct timer_event_handler* handler, int flags) {
	if (!handler || !handler->fn)
		return -EINVAL;
	if (us < 0)
		return -EINVAL;
	struct timer* timer = pick_timer(flags);
	if (!timer)
		return -ENODEV;
	if (timer->flags & TIMER_FLAG_PERCPU)
		flags |= TIMER_FLAG_PERCPU;

	struct timer_event* event = handle;
	event->handler = *handler;
	event->flags |= flags;
	event->timer = timer;

	struct timespec ts_us = timespec_from_us(us);
	unsigned long irq_flags = local_irq_save();
	event->expiration = timespec_add(time_fromboot(), ts_us);
	event->cpu = (flags & TIMER_FLAG_PERCPU) ? current_cpu() : NULL;

	struct list_head* list = event->cpu ? &event->cpu->timer_event_list : &event_list;
	if (!event->cpu)
		spinlock_acquire(&timer_lock);

	list_add(list, &event->link);

	int err = timer->ops->arm(timer, ts_us, event);
	if (err == -EBUSY) {
		struct timer_event* earliest = earliest_event(timer);
		if (earliest == event)
			err = timer->ops->rearm(timer, ts_us, event);
		else
			err = 0;
	}
	if (err)
		list_remove(&event->link);

	if (!event->cpu)
		spinlock_release(&timer_lock);
	local_irq_restore(irq_flags);

	return err;
}

int arm_timer_event(time_t us, const struct timer_event_handler* handler, int flags, void** out_handle) {
	struct timer_event* event = alloc_timer_event_handle(flags);
	if (!event)
		return -ENOMEM;
	int err = arm_timer_event_handle(event, us, handler, flags);
	if (err) {
		free_timer_event_handle(event);
		return err;
	}
	if (out_handle)
		*out_handle = event;
	return 0;
}

int cancel_timer_event(void* handle) {
	if (!handle)
		return -EINVAL;

	struct timer_event* event = handle;
	bug(event->flags & TIMER_FLAG_EVENT_ALLOC_AUTOFREE);

	unsigned long irq_flags = local_irq_save();
	if (event->cpu != NULL && event->cpu != current_cpu()) {
		local_irq_restore(irq_flags);
		return -EBUSY;
	}

	if (!event->cpu)
		spinlock_acquire(&timer_lock);

	event->timer->ops->cancel(event->timer, event);
	if (list_node_linked(&event->link)) {
		list_remove(&event->link);
		arm_earliest_event_locked(event->timer);
	}

	if (!event->cpu)
		spinlock_release(&timer_lock);
	local_irq_restore(irq_flags);
	return 0;
}

extern struct timer _ld_kernel_timers_start[];
extern struct timer _ld_kernel_timers_end[];

static u32 __atomic cpus_done = atomic_init(0);

static void timers_ap_init(void) {
	struct cpu* cpu = current_cpu();
	list_head_init(&cpu->timer_event_list);
	list_head_init(&cpu->softirq_timer_cb_list);

	for (struct timer* t = _ld_kernel_timers_start; t < _ld_kernel_timers_end; t++) {
		if (!(t->flags & TIMER_FLAG_PERCPU))
			continue;

		list_node_init(&t->link);
		if (t->probe_dependencies)
			init_task_run_array(t->probe_dependencies);
		if (t->ops->probe()) {
			if (likely(t->init_dependencies))
				init_task_run_array(t->init_dependencies);
			else
				printk(PRINTK_WARN "timer: %s has no init dependencies\n", t->name);

			/* Only add the timer if the last CPU is initializing it */
			if (t->ops->init(t) == 0) {
				if (atomic_add_fetch(&t->cpus_initialized, 1) == arch_get_cpu_count()) {
					spinlock_acquire(&timer_lock);
					list_add(&timer_list, &t->link);
					spinlock_release(&timer_lock);
				}
			}
		}
	}

	if (unlikely(atomic_add_fetch(&cpus_done, 1) == arch_get_cpu_count() && list_empty(&timer_list)))
		panic("No timers\n");
}

static void timers_init(void) {
	softirq_register(SOFTIRQ_TIMER, timer_softirq);

	const size_t te_sz = sizeof(struct timer_event);
	const size_t te_align = alignof(struct timer_event);
	event_cache = slab_cache_create(te_sz, te_align, MM_ZONE_NORMAL, NULL, NULL);
	atomic_event_cache = slab_cache_create(te_sz, te_align, MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
	if (!event_cache || !atomic_event_cache)
		out_of_memory();

	for (struct timer* t = _ld_kernel_timers_start; t < _ld_kernel_timers_end; t++) {
		if (t->flags & TIMER_FLAG_PERCPU)
			continue;
		list_node_init(&t->link);
		if (t->probe_dependencies)
			init_task_run_array(t->probe_dependencies);
		if (t->ops->probe()) {
			if (likely(t->init_dependencies))
				init_task_run_array(t->init_dependencies);
			else
				printk(PRINTK_WARN "timer: %s has no init dependencies\n", t->name);

			if (t->ops->init(t) == 0) {
				spinlock_acquire(&timer_lock); /* Acquire the lock for the fence */
				list_add(&timer_list, &t->link);
				spinlock_release(&timer_lock);
			}
		}
	}
	timers_ap_init();
}

INIT_TASK_DECLARE(heap_init_task);
INIT_TASK_DEFINE(timers_init_task, INIT_TASK_SCOPE_BSP, timers_init, &heap_init_task);
INIT_TASK_DEFINE(timers_ap_init_task, INIT_TASK_SCOPE_AP, timers_ap_init, &timers_init_task);
