#include <crater/init.h>
#include <crater/sched.h>
#include <crater/proc.h>
#include <crater/vmm.h>
#include <crater/timer.h>
#include <crater/irq.h>
#include <arch/processor.h>

static inline int clamp_prio(int prio) {
	if (prio < SCHED_PRIO_MIN)
		return SCHED_PRIO_MIN;
	else if (prio > SCHED_PRIO_MAX)
		return SCHED_PRIO_MAX;
	return prio;
}

int sched_thread_attach(struct thread* thread, struct proc* proc, int prio) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;
	if (!rq->policy->ops->attach)
		return -ENOSYS;

	prio = clamp_prio(prio);

	atomic_store(&thread->state, THREAD_READY);
	atomic_store(&thread->prio, prio);
	proc_thread_attach(proc, thread);

	unsigned long flags;
	spinlock_acquire_irq_save(&rq->lock, &flags);

	int err = rq->policy->ops->attach(rq, thread, prio);
	if (err == 0)
		atomic_fetch_add(&rq->thread_count, 1);
	else
		proc_thread_detach(thread);

	spinlock_release_irq_restore(&rq->lock, &flags);
	return err;
}

void sched_thread_detach(struct thread* thread) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;
	bug(rq->policy->ops->detach == NULL);

	proc_thread_detach(thread);

	unsigned long flags;
	spinlock_acquire_irq_save(&rq->lock, &flags);

	rq->policy->ops->detach(rq, thread);
	atomic_fetch_sub(&rq->thread_count, 1);

	spinlock_release_irq_restore(&rq->lock, &flags);
}

static struct isr* __atomic resched_isr = atomic_init(NULL);

static void resched_ipi(struct isr* isr) {
	(void)isr;
	current_cpu()->need_resched = true;
}

static void resched_timer_event(void* event_handle, void* arg) {
	(void)event_handle;
	(void)arg;
	current_cpu()->need_resched = true;
}

static void send_resched(struct cpu* cpu) {
	/*
	 * The ISR being NULL means that there is only one CPU, so instead set a timer event to trigger right away.
	 * If irqctl_send_ipi() or arm_timer_event() fails, then there is nothing we can do. Can't print an error because
	 * printk() may try to acquire scheduler locks.
	 */
	if (atomic_load(&resched_isr)) {
		irqctl_send_ipi(cpu, atomic_load(&resched_isr), 0);
	} else {
		const struct timer_event_handler handler = { .fn = resched_timer_event, .arg = NULL };
		arm_timer_event(0, &handler, TIMER_FLAG_EVENT_ALLOC_AUTOFREE | TIMER_FLAG_HARDIRQ, NULL);
	}
}

int sched_enqueue(struct thread* thread) {
	struct cpu* cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &cpu->runqueue;
	if (!rq->policy->ops->enqueue)
		return -ENOSYS;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	bug(atomic_load(&thread->proc) == NULL); /* Thread is detached */

	struct thread* rq_current = atomic_load(&rq->current);
	int ret = rq->policy->ops->enqueue(rq, thread);
	if (ret == 0 && atomic_load(&thread->prio) > atomic_load(&rq_current->prio))
		send_resched(cpu);

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return ret;
}

/* This function is not used anywhere in the kernel right now, but may be used when migration is implemented. */
int sched_dequeue(struct thread* thread) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;
	if (!rq->policy->ops->dequeue)
		return -ENOSYS;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	bug(atomic_load(&thread->proc) == NULL); /* Thread is detached */
	int ret = -EAGAIN;
	if (likely(atomic_load(&rq->current) != thread))
		ret = rq->policy->ops->dequeue(rq, thread);

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return ret;
}

int sched_change_prio(struct thread* thread, int prio) {
	struct cpu* cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &cpu->runqueue;
	if (!rq->policy->ops->change_prio)
		return -ENOSYS;

	prio = clamp_prio(prio);

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	int err = rq->policy->ops->change_prio(rq, thread, prio);
	if (err == 0) {
		struct thread* rq_current = atomic_load(&rq->current);
		int old_prio = atomic_exchange(&thread->prio, prio);
		bool resched = (rq_current == thread) ? (prio < old_prio) : (prio > atomic_load(&rq_current->prio));
		if (resched)
			send_resched(cpu);
	}

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return err;
}

static void __schedule(void) {
	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	struct thread* current = atomic_load(&rq->current);

	spinlock_acquire(&rq->lock);

	if (current != rq->idle)
		rq->policy->ops->yield(rq, current);
	struct thread* next = rq->policy->ops->pick_next(rq);
	if (!next)
		next = rq->idle;
	cpu->need_resched = false;
	if (next == current) {
		spinlock_release(&rq->lock);
		return;
	}

	int expected_state = THREAD_RUNNING;
	atomic_compare_exchange_strong(&current->state, &expected_state, THREAD_READY);

	/* Prepare the next thread for execution */
	expected_state = THREAD_READY;
	bug(atomic_compare_exchange_strong(&next->state, &expected_state, THREAD_RUNNING) == false);
	atomic_store(&rq->current, next);
	atomic_store(&next->topology.on_cpu, true);
	if (current->mm_struct != next->mm_struct)
		mm_switch_context(next->mm_struct);

	spinlock_release(&rq->lock);
	arch_context_switch(current, next);
}

void schedule_preempt_irq(void) {
	bug(current_thread()->preempt_count != 0);
	__schedule();
}

int schedule(void) {
	bug(in_atomic());
	local_irq_disable();
	__schedule();
	int ret = atomic_load(&current_thread()->wakeup_errno);
	local_irq_enable();
	return ret;
}

int sched_yield(void) {
	schedule();
	return 0;
}

/* Expected to be called with IRQ's off */
__asmlinkage void sched_userspace_check(struct arch_context* ctx) {
	struct thread* thread = current_thread();
	if (atomic_load(&thread->should_exit)) {
		memset(ctx, 0, sizeof(*ctx));
		arch_context_prepare_execution(ctx, ARCH_CODE_ADDRESS(uintptr_t, sched_thread_exit), (uintptr_t)thread->kernel_stack_top);
	}
}

static void __sched_wakeup_locked(struct thread* thread, int wakeup_errno) {
	struct cpu* target_cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &target_cpu->runqueue;
	struct thread* rq_current = atomic_load(&rq->current);

	/* Not interruptible, just let the thread wake up normally */
	if (wakeup_errno == -EINTR && !(atomic_load(&thread->state_flags) & THREAD_STATE_FLAG_INTERRUPTIBLE))
		return;

	int expected = THREAD_SLEEPING;
	if (thread == rq_current) {
		/* The thread is in between sched_prepare_sleep() and the context switch */
		if (atomic_compare_exchange_strong(&thread->state, &expected, THREAD_RUNNING)) {
			atomic_store(&thread->wakeup_errno, wakeup_errno);
			atomic_store(&thread->state_flags, 0);
		}
		return;
	}

	if (!atomic_compare_exchange_strong(&thread->state, &expected, THREAD_READY))
		return;

	atomic_store(&thread->wakeup_errno, wakeup_errno);
	atomic_store(&thread->state_flags, 0);

	bug(rq->policy->ops->enqueue(rq, thread) != 0);
	if (atomic_load(&thread->prio) > atomic_load(&rq_current->prio))
		send_resched(target_cpu);
}

void sched_wakeup(struct thread* thread, int wakeup_errno) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);
	__sched_wakeup_locked(thread, wakeup_errno);
	spinlock_release_irq_restore(&rq->lock, &irq_flags);
}

struct sched_timer_arg {
	struct thread* thread;
	unsigned long long gen;
};

static struct slab_cache* atomic_sta_cache;

static void sched_timer_handler(void* event_handle, void* arg) {
	struct sched_timer_arg* targ = arg;
	struct thread* thread = targ->thread;
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	/* Only wake if this timer is for the current sleep */
	if (atomic_load(&thread->sleep_gen) == targ->gen) {
		int wakeup_errno = 0;
		if (atomic_load(&thread->state_flags) & THREAD_STATE_FLAG_TIMEOUT)
			wakeup_errno = -ETIME;
		__sched_wakeup_locked(thread, wakeup_errno);
	}

	spinlock_release_irq_restore(&rq->lock, &irq_flags);

	THREAD_RELEASE(thread);
	slab_cache_free(atomic_sta_cache, targ);
	free_timer_event_handle(event_handle);
}

int sched_prepare_sleep(time_t us, int flags) {
	bug(in_interrupt() == true);
	if ((flags & THREAD_STATE_FLAG_TIMEOUT) && us == 0)
		return -EINVAL;
	if (us < 0)
		return -EINVAL;

	struct thread* thread = current_thread();

	void* handle = NULL;
	struct sched_timer_arg* targ = NULL;
	if (us != 0) {
		/* Freed in an atomic context, and this function also may be called in an atomic context */
		handle = alloc_timer_event_handle(TIMER_FLAG_EVENT_ALLOC_ATOMIC);
		if (!handle)
			return -ENOMEM;
		targ = slab_cache_alloc(atomic_sta_cache);
		if (!targ) {
			free_timer_event_handle(handle);
			return -ENOMEM;
		}
	}

	int err = 0;
	unsigned long irq_flags = local_irq_save();

	struct runqueue* rq = &current_cpu()->runqueue;
	spinlock_acquire(&rq->lock);

	unsigned long long gen = atomic_add_fetch(&thread->sleep_gen, 1);
	atomic_store(&thread->state_flags, flags);
	atomic_store(&thread->wakeup_errno, 0);
	atomic_store(&thread->state, THREAD_SLEEPING);

	spinlock_release(&rq->lock);

	if (handle) {
		THREAD_HOLD(thread); /* Thread might be reaped before the timer event fires, so hold a reference */
		targ->thread = thread;
		targ->gen = gen;
		const struct timer_event_handler handler = { .fn = sched_timer_handler, .arg = targ };
		err = arm_timer_event_handle(handle, us, &handler, 0);
		if (err) {
			atomic_store(&thread->state, THREAD_RUNNING);
			free_timer_event_handle(handle);
			slab_cache_free(atomic_sta_cache, targ);
			THREAD_RELEASE(thread);
		}
	}

	local_irq_restore(irq_flags);
	return err;
}

_Noreturn void sched_thread_exit(void) {
	local_irq_disable();

	/*
	 * If a thread isn't running at the time this is called (eg. With a sched_prepare_sleep()),
	 * it's possible that the thread can continue executing in an unsafe place when woken up.
	 */
	struct thread* thread = current_thread();
	bug(atomic_exchange(&thread->state, THREAD_ZOMBIE) != THREAD_RUNNING);

	struct runqueue* rq = &current_cpu()->runqueue;
	list_add_tail(&rq->zombie_list, &thread->block_link);
	semaphore_signal(&rq->reaper_sem);

	local_irq_enable();
	schedule();
	bug("unreachable");
}

static struct proc* kernel_proc;

static struct thread* create_bootstrap_thread(void (*exec)(void), int state, int prio) {
	struct thread* thread = alloc_thread();
	if (unlikely(!thread))
		out_of_memory();

	/* Even when bootstrapping the current thread, allocate a stack. This makes it slightly easier for the reaper to handle */
	int err = alloc_thread_stack(thread, NULL);
	if (unlikely(err)) {
		if (err == -ENOMEM)
			out_of_memory();
		else
			panic("Failed to allocate bootstrap thread stack: %d\n", err);
	}

	thread_topology_init(thread, TOPOLOGY_CURRENT_CPU | TOPOLOGY_NO_MIGRATE);

	/*
	 * When priority is zero, it means that it's the idle thread.
	 * The idle thread does not get attached to a runqueue, so only attach it to the process.
	 * Not doing so will cause a null dereference when switching to the idle thread.
	 */
	if (prio) {
		err = sched_thread_attach(thread, kernel_proc, prio);
		if (err == -ENOMEM)
			out_of_memory();
		bug(err != 0);
	} else {
		proc_thread_attach(kernel_proc, thread);
	}
	atomic_store(&thread->state, state);

	/*
	 * If the state is running, then the current thread is being bootstrapped,
	 * so it doesn't make sense to prepare execution on it.
	 */
	if (state != THREAD_RUNNING)
		arch_context_prepare_execution(&thread->context.arch_context, ARCH_CODE_ADDRESS(uintptr_t, exec), (uintptr_t)thread->kernel_stack_top);

	return thread;
}

static void idle(void) {
	while (1)
		arch_cpu_idle();
}

static u32 __atomic sched_id_counter = atomic_init(0);

void sched_assign_id(void) {
	u32 id = atomic_fetch_add_explicit(&sched_id_counter, 1, memory_order_relaxed);
	bug(id >= arch_get_cpu_count());
	current_cpu()->runqueue.sched_id = id;
}

static void sched_bootstrap_processor(void) {
	struct runqueue* rq = &current_cpu()->runqueue;

	spinlock_init(&rq->lock);
	list_head_init(&rq->zombie_list);
	list_head_init(&rq->detached_list);
	semaphore_init(&rq->reaper_sem, 0);
	semaphore_init(&rq->__reaper1_sem, 0);

	/* First create the current thread, and then release the thread so the reaper can free it */
	struct thread* thread = create_bootstrap_thread(NULL, THREAD_RUNNING, SCHED_PRIO_DEFAULT);
	atomic_store(&rq->current, thread);
	atomic_store(&thread->topology.on_cpu, true);
	THREAD_RELEASE(thread);

	/* Now create the idle thread, this thread should never be destroyed, so keep the ref */
	thread = create_bootstrap_thread(idle, THREAD_READY, 0);
	rq->idle = thread;
}

static void sched_init(void) {
	atomic_sta_cache = slab_cache_create(sizeof(struct sched_timer_arg), alignof(struct sched_timer_arg),
			MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
	if (!atomic_sta_cache)
		out_of_memory();

	bug(proc_get(0, &kernel_proc) != 0);
	if (arch_get_cpu_count() > 1) {
		struct isr* isr = alloc_isr();
		if (!isr)
			out_of_memory();
		int err = register_isr(isr, resched_ipi, NULL, ISR_FLAG_TYPE_SGI);
		if (err)
			panic("Failed to create reschedule IPI");
		atomic_store(&resched_isr, isr);
	}

	sched_bootstrap_processor();
}

INIT_TASK_DECLARE(timers_init_task, timekeeper_init_task, timers_ap_init_task, timekeeper_ap_init_task,
		proc_init_task, sched_policy_init_task, arch_context_init_task, arch_context_ap_init_task);
INIT_TASK_DEFINE(sched_init_task, INIT_TASK_SCOPE_BSP, sched_init,
		&timers_init_task, &timekeeper_init_task, &proc_init_task, &sched_policy_init_task, &arch_context_init_task);
INIT_TASK_DEFINE(sched_ap_init_task, INIT_TASK_SCOPE_AP, sched_bootstrap_processor,
		&timers_ap_init_task, &timekeeper_ap_init_task, &sched_policy_init_task, &arch_context_ap_init_task);
