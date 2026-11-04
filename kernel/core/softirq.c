#include <crater/interrupt.h>
#include <crater/sched.h>
#include <crater/irq.h>
#include <crater/timekeeper.h>
#include <crater/kthread.h>

static softirqhandler_t __atomic softirq_arr[SOFTIRQ_COUNT] = { 0 };

int softirq_register(enum softirq softirq, softirqhandler_t action) {
	if (softirq < 0 || softirq >= SOFTIRQ_COUNT)
		return -EINVAL;
	softirqhandler_t expected = NULL;
	if (!atomic_compare_exchange_strong(&softirq_arr[softirq], &expected, action))
		return -EBUSY;
	return 0;
}

void softirq_raise(enum softirq softirq) {
	if (softirq < 0 || softirq >= SOFTIRQ_COUNT)
		return;

	unsigned long irq_flags = local_irq_save();
	current_cpu()->softirq_mask |= (1u << softirq);
	local_irq_restore(irq_flags);
}

static int softirqd(void* arg) {
	struct cpu* cpu = arg;
	bool reschedule = false;
	while (1) {
		if (reschedule)
			schedule();

		int err = semaphore_wait(&cpu->softirqd_sem, 0);
		if (unlikely(err))
			continue;

		preempt_offset(PREEMPT_SOFTIRQ_OFFSET);

		if (cpu->softirq_mask && cpu->softirq_count == 0) {
			unsigned long pending = cpu->softirq_mask;
			cpu->softirq_mask = 0;
			for (enum softirq i = 0; i < SOFTIRQ_COUNT; i++) {
				if (pending & (1 << i)) {
					softirqhandler_t action = atomic_load(&softirq_arr[i]);
					if (likely(action))
						action();
				}
			}
			if (cpu->softirq_mask) {
				semaphore_signal(&cpu->softirqd_sem);
				reschedule = false;
			}
		} else {
			reschedule = true;
		}

		preempt_offset(-PREEMPT_SOFTIRQ_OFFSET);
	}

	return 0;
}

void softirq_execute(void) {
	if (in_softirq() || current_cpu()->softirq_count)
		return;

	int reent_count = 10;
	struct cpu* cpu = current_cpu();
	const struct timespec end_time = timespec_add(time_fromboot(), timespec_from_us(2000));

	preempt_offset(PREEMPT_SOFTIRQ_OFFSET);
	local_irq_enable();

	while (cpu->softirq_mask) {
		unsigned long pending = cpu->softirq_mask;
		cpu->softirq_mask = 0;
		for (enum softirq i = 0; i < SOFTIRQ_COUNT; i++) {
			if (pending & (1 << i)) {
				softirqhandler_t action = atomic_load(&softirq_arr[i]);
				if (likely(action))
					action();
			}
		}
		if (timespec_cmp(time_fromboot(), end_time) >= 0)
			break;
		if (reent_count-- == 0)
			break;
	}

	local_irq_disable();
	preempt_offset(-PREEMPT_SOFTIRQ_OFFSET);
	if (cpu->softirq_mask)
		semaphore_signal(&cpu->softirqd_sem);
}

void local_softirq_enable(void) {
	compiler_barrier();
	unsigned long flags = local_irq_save();
	bug(current_cpu()->softirq_count-- == 0);
	local_irq_restore(flags);
}

void local_softirq_disable(void) {
	unsigned long flags = local_irq_save();
	current_cpu()->softirq_count++;
	local_irq_restore(flags);
	compiler_barrier();
}

static void softirq_init(void) {
	struct cpu* cpu = current_cpu();
	cpu->softirq_mask = 0;
	cpu->softirq_count = 0;

	semaphore_init(&cpu->softirqd_sem, 0);
	struct thread* d = kthread_create(TOPOLOGY_CURRENT_CPU | TOPOLOGY_NO_MIGRATE, softirqd, cpu, "softirqd/%u", cpu->runqueue.sched_id);
	if (!d)
		out_of_memory();

	int err = kthread_run(d, SCHED_PRIO_DEFAULT);
	if (err)
		panic("Failed to run softirqd thread: %d\n", err);
}

INIT_TASK_DECLARE(kthread_init_task, sched_init_task, sched_ap_init_task);
INIT_TASK_DEFINE(softirq_init_task, INIT_TASK_SCOPE_BSP, softirq_init, &kthread_init_task, &sched_init_task);
INIT_TASK_DEFINE(softirq_ap_init_task, INIT_TASK_SCOPE_AP, softirq_init, &softirq_init_task, &sched_ap_init_task);
