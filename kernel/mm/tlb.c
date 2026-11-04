#include <crater/irq.h>
#include <crater/proc.h>
#include <crater/vmm.h>
#include <crater/tlb.h>

#include <arch/tlb.h>
#include <arch/processor.h>

#define TLB_FULL_INVALIDATE_THRESHOLD_PAGE_COUNT 32

static void invalidate_local(uintptr_t address, size_t page_count) {
	if (address % PAGE_SIZE) {
		address = ROUND_DOWN(address, PAGE_SIZE);
		if (page_count != SIZE_MAX) /* Let TLB_FULL_INVALIDATE_THRESHOLD_PAGE_COUNT handle it */
			page_count++;
	}
	if (page_count >= TLB_FULL_INVALIDATE_THRESHOLD_PAGE_COUNT)
		arch_tlb_flush_all();
	else
		arch_tlb_flush_count(address, page_count);
}

static struct isr* __atomic shootdown_isr = atomic_init(NULL);

static uintptr_t __atomic shootdown_address;
static size_t __atomic shootdown_page_count;
static u32 __atomic shootdown_cpus_remaining;
static MUTEX_DEFINE(shootdown_mtx);

static void shootdown_ipi(struct isr* isr) {
	(void)isr;
	invalidate_local(atomic_load(&shootdown_address), atomic_load(&shootdown_page_count));
	atomic_sub_fetch(&shootdown_cpus_remaining, 1);
}

static void invalidate_others(uintptr_t address, size_t page_count) {
	if (!atomic_load(&shootdown_isr))
		return;

	mutex_acquire(&shootdown_mtx);
	preempt_disable();

	struct smp_cpus cpus;
	smp_cpus_read_acquire(&cpus);

	if (cpus.count > 1) {
		atomic_store(&shootdown_address, address);
		atomic_store(&shootdown_page_count, page_count);
		atomic_store(&shootdown_cpus_remaining, cpus.count - 1);
		for (u32 i = 0; i < cpus.count; i++) {
			if (cpus.cpus[i] != current_cpu())
				bug(irqctl_send_ipi(cpus.cpus[i], atomic_load(&shootdown_isr), 0) != 0);
		}
		while (atomic_load(&shootdown_cpus_remaining))
			arch_cpu_relax();
	}

	smp_cpus_read_release(&cpus);

	preempt_enable();
	mutex_release(&shootdown_mtx);
}

static inline void tlb_invalidate(uintptr_t address, size_t page_count) {
	invalidate_local(address, page_count);
	invalidate_others(address, page_count);
}

static inline void __tlb_batch_init(struct tlb_batch* batch) {
	batch->first_page_virtual = UINTPTR_MAX;
	batch->last_page_virtual = 0;
	batch->page_count = 0;
	list_head_init(&batch->page_tables_list);
}

void tlb_batch_init(struct tlb_batch* batch, arch_pte_t* pagetable) {
	batch->pagetable = pagetable;
	__tlb_batch_init(batch);
}

void tlb_batch_flush(struct tlb_batch* batch) {
	if (!list_empty(&batch->page_tables_list)) {
		tlb_invalidate(0, SIZE_MAX); /* Doing a full invalidate makes sure that paging structures aren't cached */
	} else if (batch->first_page_virtual <= batch->last_page_virtual) {
		size_t page_count = ((batch->last_page_virtual - batch->first_page_virtual) >> PAGE_SHIFT) + 1;
		tlb_invalidate(batch->first_page_virtual, page_count);
	}

	/* Release leaf page table entries */
	for (size_t i = 0; i < batch->page_count; i++)
		release_page(batch->pages[i]);

	/* Now free page tables */
	struct page* pos, *n;
	list_for_each_entry_safe(pos, n, &batch->page_tables_list, pagetable_tlb_batch_link) {
		list_remove(&pos->pagetable_tlb_batch_link);
		release_page(pos);
	}

	__tlb_batch_init(batch);
}

void tlb_batch_add(struct tlb_batch* batch, uintptr_t virtual, struct page* page) {
	if (unlikely(batch->page_count == ARRAY_SIZE(batch->pages) && page))
		tlb_batch_flush(batch);

	if (virtual < batch->first_page_virtual)
		batch->first_page_virtual = virtual;
	if (virtual > batch->last_page_virtual)
		batch->last_page_virtual = virtual;

	if (page)
		batch->pages[batch->page_count++] = page;
}

void tlb_batch_add_page_table(struct tlb_batch* batch, struct page* page) {
	bug(list_node_linked(&page->pagetable_tlb_batch_link) == true);
	list_add(&batch->page_tables_list, &page->pagetable_tlb_batch_link);
}

void tlb_shootdown_init(void) {
	struct isr* isr = alloc_isr();
	if (unlikely(!isr))
		out_of_memory();

	int err = register_isr(isr, shootdown_ipi, NULL, ISR_FLAG_TYPE_SGI);
	if (unlikely(err))
		panic("%s() failed: register_isr() returned %d", __func__, err);

	atomic_store(&shootdown_isr, isr);
}
