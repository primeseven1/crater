#include <crater/timer.h>
#include <crater/sched.h>
#include <crater/printk.h>

#define SCHED_TICK_TIME_US 1000

void preempt_disable(void) {
	current_thread()->preempt_count++;
	compiler_barrier();
}

void preempt_enable(void) {
	compiler_barrier();
	bug(current_thread()->preempt_count-- == 0);
}

void preempt_offset(long count) {
	struct thread* current = current_thread();
	compiler_barrier();
	current->preempt_count += count;
	bug(current->preempt_count < 0);
	compiler_barrier();
}

static void do_preempt(void* event_handle, void* arg) {
	(void)arg;

	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	struct thread* current = atomic_load(&rq->current);

	spinlock_acquire(&rq->lock);

	if (current != rq->idle) {
		bool resched = rq->policy->ops->tick(rq, current);
		if (!cpu->need_resched)
			cpu->need_resched = resched;
	} else {
		cpu->need_resched = true;
	}

	spinlock_release(&rq->lock);

	/* Now just re-arm the preempt event */
	const struct timer_event_handler preempt_handler = { .fn = do_preempt, NULL };
	bug(arm_timer_event_handle(event_handle, SCHED_TICK_TIME_US, &preempt_handler, TIMER_FLAG_PERCPU | TIMER_FLAG_HARDIRQ) != 0);
}

void preempt_init(void) {
	const struct timer_event_handler preempt_handler = { .fn = do_preempt, NULL };

	/* Use TIMER_FLAG_EVENT_ALLOC_ATOMIC here, otherwise a mutex will be acquired before the scheduler is fully initialized */
	void* handle;
	int err = arm_timer_event(SCHED_TICK_TIME_US, &preempt_handler,
			TIMER_FLAG_PERCPU | TIMER_FLAG_HARDIRQ | TIMER_FLAG_EVENT_ALLOC_ATOMIC,
			&handle);
	if (err == -ENOMEM)
		out_of_memory();
	else if (err)
		panic("Failed to arm preempt event: %i\n", err);
}
