#include <crater/smp.h>
#include <crater/sched.h>

static struct cpu* pick_cpu(struct topology* topology) {
	unsigned long irq_flags = local_irq_save();
	struct cpu* ret = NULL;

	struct smp_cpus smp_cpus;
	smp_cpus_read_acquire(&smp_cpus);

	for (u32 i = 0; i < smp_cpus.count; i++ ) {
		if (!cpumask_test(&topology->cpumask, i))
			continue;
		struct cpu* cpu = smp_cpus.cpus[i];
		if (unlikely(!cpu))
			continue;
		if (!ret || atomic_load(&cpu->runqueue.thread_count) < atomic_load(&ret->runqueue.thread_count))
			ret = cpu;
	}

	if (unlikely(!ret)) {
		struct cpu* current = current_cpu();
		/* This can happen in early init when creating bootstrap threads, softirq threads, etc. */
		if (cpumask_test(&topology->cpumask, current->runqueue.sched_id))
			ret = current;
	}

	smp_cpus_read_release(&smp_cpus);

	local_irq_restore(irq_flags);
	return ret;
}

static inline int set_cpu(struct topology* topology, struct cpu* cpu) {
	if (cpumask_test(&topology->cpumask, cpu->runqueue.sched_id) == false)
		return -EINVAL;
	atomic_store(&topology->cpu, cpu);
	return 0;
}

void thread_topology_init(struct thread* thread, int topology_flags) {
	struct topology* const topology = &thread->topology;

	atomic_store(&topology->cpu, NULL);
	atomic_store(&topology->migratable, !(topology_flags & TOPOLOGY_NO_MIGRATE));
	atomic_store(&topology->on_cpu, false);

	topology_flags &= (TOPOLOGY_BSP | TOPOLOGY_CURRENT_CPU);
	if (topology_flags) {
		cpumask_memset(&topology->cpumask, 0);
		if (topology_flags & TOPOLOGY_BSP)
			cpumask_set(&topology->cpumask, 0, true);
		if (topology_flags & TOPOLOGY_CURRENT_CPU)
			cpumask_set(&topology->cpumask, current_cpu()->runqueue.sched_id, true);
	} else {
		cpumask_memset(&topology->cpumask, UINT_MAX);
	}

	struct cpu* const cpu = pick_cpu(topology);
	bug(cpu == NULL);
	bug(set_cpu(topology, cpu) != 0);
}
