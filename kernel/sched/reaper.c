#include <crater/kthread.h>
#include <crater/proc.h>
#include <crater/printk.h>
#include <crater/timekeeper.h>

#define REAPER_POLL_US (100u * 1000) /* 100 milliseconds */
#define REAPER_WARN_US (20ull * 1000 * 1000) /* 20 seconds */

/* Detaches a thread from the runqueue and process. If the refcount is more than zero, it hands it off to reaper1 */
static int reaper0(void* arg) {
	struct runqueue* const rq = arg;
	while (1) {
		const int err = semaphore_wait(&rq->reaper_sem, 0);
		if (unlikely(err))
			continue;

		preempt_disable();

		struct thread* zombie = NULL;
		if (likely(!list_empty(&rq->zombie_list))) {
			zombie = list_first_entry(&rq->zombie_list, struct thread, block_link);
			list_remove(&zombie->block_link);
			sched_thread_detach(zombie);
			if (atomic_load(&zombie->refcnt) != 0) {
				zombie->detach_time = time_fromboot();
				zombie->reap_warn_deadline = REAPER_WARN_US;
				list_add_tail(&rq->detached_list, &zombie->block_link);
				semaphore_signal(&rq->__reaper1_sem);
				zombie = NULL;
			}
		}

		preempt_enable();
		if (zombie)
			free_thread(zombie);
	}

	bug("unreachable");
}

/*
 * Walks a list of detached threads (from reaper0), and frees the threads once the refcount hits zero.
 *
 * This function is also responsible for keeping track of how long each thread has been in the detached list for,
 * and warns when a thread has been in the list for more than 20 seconds or so.
 */
static int reaper1(void* arg) {
	struct runqueue* const rq = arg;
	while (1) {
		int err = semaphore_wait_timed(&rq->__reaper1_sem, REAPER_POLL_US, 0);
		if (err != 0 && err != -ETIME)
			continue;
		
		struct list_head free_list;
		list_head_init(&free_list);
		const struct timespec now = time_fromboot();

		preempt_disable();

		struct thread* pos, *tmp;
		list_for_each_entry_safe(pos, tmp, &rq->detached_list, block_link) {
			if (atomic_load(&pos->refcnt) == 0) {
				list_remove(&pos->block_link);
				list_add_tail(&free_list, &pos->block_link);
			} else if (timespec_cmp(timespec_sub(now, pos->detach_time), timespec_from_us(pos->reap_warn_deadline)) >= 0) {
				time_t diff = timespec_us(timespec_sub(now, pos->detach_time));
#ifdef CONFIG_DEBUG
				struct thread* th = pos;
#else
				struct thread* th = NULL;
#endif
				printk(PRINTK_WARN "reaper1: thread %p not reaped after %ld seconds, refcnt=%lu\n", th, diff / 1000000ul, atomic_load(&pos->refcnt));
				pos->reap_warn_deadline += REAPER_WARN_US;
			}
		}

		preempt_enable();
		while (!list_empty(&free_list)) {
			struct thread* zombie = list_first_entry(&free_list, struct thread, block_link);
			list_remove(&zombie->block_link);
			free_thread(zombie);
		}
	}

	bug("unreachable");
}

static void reaper_init(void) {
	struct runqueue* rq = &current_cpu()->runqueue;

	struct thread* reaper0_thread = kthread_create(TOPOLOGY_CURRENT_CPU | TOPOLOGY_NO_MIGRATE, reaper0, rq, "reaper0/%u", rq->sched_id);
	if (unlikely(!reaper0_thread))
		out_of_memory();
	struct thread* reaper1_thread = kthread_create(TOPOLOGY_CURRENT_CPU | TOPOLOGY_NO_MIGRATE, reaper1, rq, "reaper1/%u", rq->sched_id);
	if (!reaper1_thread)
		out_of_memory();

	int err = kthread_run(reaper0_thread, SCHED_PRIO_DEFAULT);
	if (unlikely(err))
		panic("Failed to start reaper0 thread: %d", err);
	err = kthread_run(reaper1_thread, SCHED_PRIO_DEFAULT);
	if (unlikely(err))
		panic("Failed to start reaper1 thread: %d", err);
}

INIT_TASK_DECLARE(kthread_init_task, sched_init_task, sched_ap_init_task);
INIT_TASK_DEFINE(reaper_thread_init_task, INIT_TASK_SCOPE_BSP, reaper_init, &kthread_init_task, &sched_init_task);
INIT_TASK_DEFINE(reaper_thread_ap_init_task, INIT_TASK_SCOPE_AP, reaper_init, &kthread_init_task, &sched_ap_init_task);
