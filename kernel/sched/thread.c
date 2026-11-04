#include <crater/slab.h>
#include <crater/panic.h>
#include <crater/sched_types.h>
#include <crater/percpu.h>
#include <crater/vmm.h>
#include <crater/irq.h>

void* alloc_stack(void) {
	struct page* pages = alloc_pages(MM_ZONE_NORMAL, get_order(THREAD_STACK_SIZE));
	if (!pages)
		return ERR_PTR(-ENOMEM);

	struct page* page_array[(THREAD_STACK_SIZE >> PAGE_SHIFT) + 1];
	page_array[0] = NULL; /* guard page */
	for (size_t i = 1; i < ARRAY_SIZE(page_array); i++)
		page_array[i] = pages + i - 1;

	u8* mapping = vm_map_pages(NULL, page_array, ARRAY_SIZE(page_array), PGPROT_READ | PGPROT_WRITE, VMM_STACK);
	release_page(pages); /* vm_map_pages() will take a reference on success, so this is safe */

	return IS_PTR_ERR(mapping) ? mapping : mapping + THREAD_STACK_SIZE + PAGE_SIZE;
}

void free_stack(void* top) {
	const size_t unmap_size = THREAD_STACK_SIZE + PAGE_SIZE;
	void* const bottom = (u8*)top - unmap_size;
	vm_unmap_force(bottom, unmap_size, 0);
}

int alloc_thread_stack(struct thread* thread, void** top) {
	if (thread->kernel_stack_top)
		return -EEXIST;

	void* stack = alloc_stack();
	if (IS_PTR_ERR(stack))
		return PTR_ERR(stack);
	thread->kernel_stack_top = stack;
	if (top)
		*top = stack;

	return 0;
}

struct thread* alloc_thread(void) {
	unsigned long irq_flags = local_irq_save();
	const struct sched_policy_ops* ops = current_cpu()->runqueue.policy->ops;
	local_irq_restore(irq_flags);
	if (!ops->alloc || !ops->free)
		return NULL;

	struct thread* ret = ops->alloc();
	if (unlikely(!ret))
		return NULL;
	int err = arch_context_init(&ret->context);
	if (unlikely(err)) {
		ops->free(ret);
		return NULL;
	}

	ret->kernel_stack_top = NULL;
	atomic_store(&ret->topology.cpu, NULL);
	atomic_store(&ret->topology.migratable, false);
	atomic_store(&ret->topology.on_cpu, false);
	cpumask_memset(&ret->topology.cpumask, 0);
	atomic_store(&ret->proc, NULL);
	ret->mm_struct = NULL;
	ret->in_usercopy = 0;
	ret->preempt_count = 0;
	atomic_store(&ret->prio, 0);
	atomic_store(&ret->state, THREAD_NEW);
	atomic_store(&ret->state_flags, 0);
	atomic_store(&ret->wakeup_errno, 0);
	atomic_store(&ret->should_exit, false);
	atomic_store(&ret->sleep_gen, 0);
	list_node_init(&ret->proc_link);
	list_node_init(&ret->block_link);
	ret->detach_time = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 };
	ret->reap_warn_deadline = 0;
	atomic_store(&ret->refcnt, 1);

	return ret;
}

void free_thread(struct thread* thread) {
	bug(atomic_load(&thread->refcnt) != 0);

	arch_context_destroy(&thread->context);
	if (thread->kernel_stack_top) {
		free_stack(thread->kernel_stack_top);
		thread->kernel_stack_top = NULL;
	}

	unsigned long irq_flags = local_irq_save();
	const struct sched_policy_ops* ops = current_cpu()->runqueue.policy->ops;
	local_irq_restore(irq_flags);

	bug(ops->free == NULL);
	ops->free(thread);
}
