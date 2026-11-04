#include <crater/smp.h>
#include <crater/init.h>
#include <crater/percpu.h>
#include <crater/slab.h>
#include <crater/timekeeper.h>
#include <crater/rwlock.h>
#include <crater/interrupt.h>
#include <crater/irq.h>

static struct cpu** smp_cpus;
static u32 smp_cpu_count;
static struct isr* stop_isr = NULL;
static RWLOCK_DEFINE(smp_cpus_lock);

void smp_register_cpu(void) {
	struct cpu* cpu = current_cpu();
	u32 sched_id = cpu->runqueue.sched_id;
	rwlock_write_acquire(&smp_cpus_lock);

	bug(sched_id >= smp_cpu_count);
	bug(smp_cpus[sched_id] != NULL);
	smp_cpus[sched_id] = cpu;

	rwlock_write_release(&smp_cpus_lock);
}

void smp_cpus_read_acquire(struct smp_cpus* out_cpus) {
	rwlock_read_acquire(&smp_cpus_lock);
	out_cpus->count = smp_cpu_count;
	out_cpus->cpus = smp_cpus;
}

void smp_cpus_read_release(struct smp_cpus* cpus) {
	cpus->count = 0;
	cpus->cpus = NULL;
	rwlock_read_release(&smp_cpus_lock);
}

static u32 __atomic init_cpus_left;
static u32 __atomic stop_cpus_left;

void smp_init_bsp_wait_for_others(void) {
	while (atomic_load_explicit(&init_cpus_left, memory_order_acquire) > 1)
		arch_cpu_relax();
}

void smp_init_wait_for_all(void) {
	while (atomic_load_explicit(&init_cpus_left, memory_order_acquire))
		arch_cpu_relax();
}

void smp_init_complete(void) {
	atomic_sub_fetch_explicit(&init_cpus_left, 1, memory_order_acq_rel);
}

void smp_send_stop(void) {
	struct smp_cpus cpus;

	unsigned long irq_flags = local_irq_save();
	smp_cpus_read_acquire(&cpus);

	if (stop_isr && cpus.cpus) {
		atomic_store_explicit(&stop_cpus_left, cpus.count - 1, memory_order_release);
		for (u32 i = 0; i < cpus.count; i++) {
			if (cpus.cpus[i] && cpus.cpus[i] != current_cpu())
				irqctl_send_ipi(cpus.cpus[i], stop_isr, 0);
		}

		int waited_for = 0;
		while (atomic_load_explicit(&stop_cpus_left, memory_order_acquire)) {
			mdelay(1);
			if (++waited_for > 1000)
				break;
			arch_cpu_relax();
		}
	}

	/* Here we never release the read lock, that is done on purpose */
	local_irq_restore(irq_flags);
}

static void stop_ipi(struct isr* isr) {
	(void)isr;
	atomic_fetch_sub_explicit(&stop_cpus_left, 1, memory_order_release);
	while (1)
		arch_cpu_idle();
}

static void smp_init(void) {
	u32 cpu_count = arch_get_cpu_count();
	struct cpu** cpus = kcalloc(cpu_count, sizeof(*cpus), MM_ZONE_NORMAL | MM_NOFAIL);

	struct isr* isr = NULL;
	if (cpu_count >= 1) {
		isr = alloc_isr();
		if (!isr)
			out_of_memory();
		int err = register_isr(isr, stop_ipi, NULL, ISR_FLAG_TYPE_SGI);
		if (err)
			panic("Failed to register halt ISR\n");
	}

	rwlock_write_acquire(&smp_cpus_lock);
	smp_cpu_count = cpu_count;
	smp_cpus = cpus;
	stop_isr = isr;
	rwlock_write_release(&smp_cpus_lock);
	atomic_store_explicit(&init_cpus_left, cpu_count, memory_order_release);
	smp_register_cpu(); /* Register the BSP */

	arch_start_cpus();
}

INIT_TASK_DECLARE(zones_init_task, heap_init_task, sched_init_task, printk_late_init_task);
INIT_TASK_DEFINE(smp_init_task, INIT_TASK_SCOPE_BSP, smp_init, &zones_init_task, &heap_init_task, &sched_init_task);
