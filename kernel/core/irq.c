#include <crater/compiler.h>
#include <crater/irq.h>
#include <crater/init.h>
#include <crater/interrupt.h>
#include <crater/slab.h>
#include <crater/printk.h>
#include <crater/trace.h>
#include <crater/panic.h>
#include <crater/hashtable.h>
#include <crater/string.h>

#include <arch/irq_flags.h>
#include <arch/processor.h>

struct irq_shared_handler {
	const char* devname;
	void* devid;
	irqhandler_t handler;
	struct list_node link;
};

#define IRQ_STATUS_FLAG_PENDING (1 << 0)

struct irq {
	unsigned int number; /* IRQ number */
	struct isr* isr; /* ISR associated with this IRQ */
	int flags; /* IRQ_FLAG_* flags */
	int status_flags;
	long disable_count; /* Acts like a preempt_count, zero means the IRQ is enabled */
	unsigned long __atomic inflight; /* How many handlers are running. Can only increase when the spinlock is acquired. */
	struct list_head handler_list; /* struct irq_shared_handler. IRQ must be disabled and synchronized before modifying */
	spinlock_t lock; /* Primarily for deciding if the IRQ should run */
	unsigned long __atomic refcnt;
	struct list_node link, pending_link;
};

static struct hashtable* irq_table = NULL;
static MUTEX_DEFINE(irq_table_lock);

static inline void irq_ref(struct irq* irq) {
	atomic_add_fetch(&irq->refcnt, 1);
}

static inline void irq_unref(struct irq* irq) {
	atomic_sub_fetch(&irq->refcnt, 1);
}

static struct irq* find_irq_from_table_locked(unsigned int irqnum) {
	struct irq* irq;
	int err = hashtable_search(irq_table, &irqnum, sizeof(irqnum), &irq);
	if (err == 0) {
		irq_ref(irq);
		return irq;
	}
	return NULL;
}

static inline struct irq* find_irq_from_table(unsigned int irqnum) {
	mutex_acquire(&irq_table_lock);
	struct irq* irq = find_irq_from_table_locked(irqnum);
	mutex_release(&irq_table_lock);
	return irq;
}

static int irq_add_to_table_locked(unsigned int irqnum, struct irq* irq) {
	irq_ref(irq);
	int err = hashtable_insert(irq_table, &irqnum, sizeof(irqnum), &irq);
	if (err)
		irq_unref(irq);
	return err;
}

static int irq_remove_from_table_locked(unsigned int irqnum) {
	struct irq* irq = find_irq_from_table_locked(irqnum);
	if (irq) {
		bug(hashtable_remove(irq_table, &irqnum, sizeof(irqnum)) != 0);
		irq_unref(irq); /* find_irq_from_table_locked() adds a ref */
		irq_unref(irq);
		return 0;
	}
	return -ENOENT;
}

static inline void __synchronize_irq(struct irq* irq) {
	while (atomic_load(&irq->inflight) != 0)
		arch_cpu_relax();
}

static LIST_HEAD_DEFINE(pending_irqs);
static SPINLOCK_DEFINE(pending_irqs_lock);
static bool __atomic running_pending = atomic_init(false);

static inline void add_pending_locked(struct irq* irq) {
	if (!list_node_linked(&irq->pending_link)) {
		irq_ref(irq);
		list_add(&pending_irqs, &irq->pending_link);
	}
}

static inline void remove_pending_locked(struct irq* irq) {
	list_remove(&irq->pending_link);
	irq_unref(irq);
}

static void add_pending(struct irq* irq) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&pending_irqs_lock, &irq_flags);
	add_pending_locked(irq);
	spinlock_release_irq_restore(&pending_irqs_lock, &irq_flags);
}

static bool enter_irq(struct irq* irq) {
	spinlock_acquire(&irq->lock);

	bool enabled = (irq->disable_count == 0);
	if (likely(enabled)) {
		irq_ref(irq);
		atomic_add_fetch(&irq->inflight, 1);
		irq->status_flags &= ~IRQ_STATUS_FLAG_PENDING;
	} else {
		irq->status_flags |= IRQ_STATUS_FLAG_PENDING;
	}

	spinlock_release(&irq->lock);
	return enabled;
}

static inline void exit_irq(struct irq* irq) {
	atomic_sub_fetch(&irq->inflight, 1);
	irq_unref(irq);
}

static irqreturn_t run_irq_handlers(struct irq* irq) {
	irqreturn_t iret = IRQ_NONE;

	struct irq_shared_handler* sh;
	list_for_each_entry(sh, &irq->handler_list, link) {
		if (sh->handler(irq->number, sh->devid) == IRQ_HANDLED)
			iret = IRQ_HANDLED;
	}

	return iret;
}

static void irq_isr(struct isr* isr) {
	struct irq* irq = isr->private;
	bool enter = enter_irq(irq);
	if (!enter)
		return;
	if (run_irq_handlers(irq) != IRQ_HANDLED)
		printk(PRINTK_WARN "irq%u: spurious interrupt\n", irq->number);

	exit_irq(irq);
}

void synchronize_irq(unsigned int irqnum) {
	struct irq* irq = find_irq_from_table(irqnum);
	if (irq) {
		__synchronize_irq(irq);
		irq_unref(irq);
	} else {
		dump_stack();
		printk(PRINTK_CRIT "irq%u: %s(): No IRQ", irqnum, __func__);
	}	
}

static struct irq* create_irq(unsigned int irqnum, int flags) {
	struct irq* irq = kmalloc(sizeof(*irq), MM_ZONE_NORMAL);
	if (!irq)
		return NULL;
	irq->isr = alloc_isr();
	if (!irq->isr) {
		kfree(irq);
		return NULL;
	}

	irq->number = irqnum;
	irq->flags = flags;
	irq->status_flags = 0;
	irq->disable_count = 0;
	atomic_store(&irq->inflight, 0);
	list_head_init(&irq->handler_list);
	spinlock_init(&irq->lock);
	atomic_store(&irq->refcnt, 1);
	list_node_init(&irq->link);
	list_node_init(&irq->pending_link);

	return irq;
}

static inline void destroy_irq(struct irq* irq) {
	if (irq) {
		free_isr(irq->isr);
		kfree(irq);
	}
}

static struct irq_shared_handler* create_shared_handler(irqhandler_t handler, const char* devname, void* devid) {
	struct irq_shared_handler* sh = kmalloc(sizeof(*sh), MM_ZONE_NORMAL);
	if (!sh)
		return NULL;

	sh->devname = devname;
	sh->devid = devid;
	sh->handler = handler;
	list_node_init(&sh->link);

	return sh;
}

static inline void destroy_shared_handler(struct irq_shared_handler* sh) {
	kfree(sh);
}

static struct irqctl* irqctl = NULL;

void irqctl_eoi(const struct isr* isr) {
	irqctl->ops->eoi(isr);
}

int irqctl_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags) {
	unsigned long irq_flags = local_irq_save();
	int ret = irqctl->ops->send_ipi(cpu, isr, flags);
	local_irq_restore(irq_flags);
	return ret;
}

static void __disable_irq(struct irq* irq) {
	unsigned long flags;
	spinlock_acquire_irq_save(&irq->lock, &flags);

	bug(irq->disable_count == LONG_MAX);
	if (++irq->disable_count == 1)
		bug(irqctl->ops->disable(irq->number) != 0);

	spinlock_release_irq_restore(&irq->lock, &flags);
}

static void __enable_irq(struct irq* irq) {
	unsigned long flags;
	spinlock_acquire_irq_save(&irq->lock, &flags);

	bug(--irq->disable_count < 0);
	if (irq->disable_count == 0) {
		if (irq->status_flags & IRQ_STATUS_FLAG_PENDING)
			add_pending(irq);
		else
			bug(irqctl->ops->enable(irq->number) != 0);
	}

	spinlock_release_irq_restore(&irq->lock, &flags);
}

void disable_irq(unsigned int irqnum) {
	struct irq* irq = find_irq_from_table(irqnum);
	if (irq) {
		__disable_irq(irq);
		__synchronize_irq(irq);
		irq_unref(irq);
	}
}

void enable_irq(unsigned int irqnum) {
	struct irq* irq = find_irq_from_table(irqnum);
	if (irq) {
		__enable_irq(irq);
		irq_unref(irq);
	}
}

int request_irq(unsigned int irqnum, irqhandler_t handler, int flags, const char* devname, void* devid) {
	if (!handler || !devid)
		return -EINVAL;

	int err = 0;
	mutex_acquire(&irq_table_lock);

	struct irq* irq = find_irq_from_table_locked(irqnum);
	bool irq_exists = !!irq;
	if (!irq_exists) {
		irq = create_irq(irqnum, flags);
		if (!irq) {
			err = -ENOMEM;
			goto out;
		}
		err = irq_add_to_table_locked(irqnum, irq);
		if (err) {
			destroy_irq(irq);
			goto out;
		}
	}

	struct irq_shared_handler* sh = create_shared_handler(handler, devname, devid);
	if (!sh) {
		err = -ENOMEM;
		if (!irq_exists) {
			irq_remove_from_table_locked(irqnum);
			destroy_irq(irq);
		}
		goto out;
	}

	if (!irq_exists) {
		err = register_isr(irq->isr, irq_isr, irq, ISR_FLAG_TYPE_IRQ);
		if (err) {
			destroy_shared_handler(sh);
			irq_remove_from_table_locked(irqnum);
			destroy_irq(irq);
			goto out;
		}
		unsigned long irq_flags = local_irq_save();
		bug(irqctl->ops->install(irq->number, current_cpu(), irq->isr, flags) != 0);
		local_irq_restore(irq_flags);
	}

	__disable_irq(irq);
	__synchronize_irq(irq);

	list_add(&irq->handler_list, &sh->link);

	__enable_irq(irq);
out:
	mutex_release(&irq_table_lock);
	return err;
}

void free_irq(unsigned int irqnum, void* devid) {
	mutex_acquire(&irq_table_lock);

	struct irq* irq = find_irq_from_table_locked(irqnum);
	if (!irq)
		goto out;

	struct irq_shared_handler* sh;
	list_for_each_entry(sh, &irq->handler_list, link) {
		if (sh->devid == devid)
			break;
	}
	if (sh->devid != devid)
		goto out;

	__disable_irq(irq);
	__synchronize_irq(irq);

	list_remove(&sh->link);

	if (list_empty(&irq->handler_list) && unregister_isr(irq->isr) == 0) {
		free_isr(irq->isr);
		bug(irq_remove_from_table_locked(irqnum) != 0);

		/*
		 * May or may not deadlock depending on if i did refcounts correctly, will fix later if this happens,
		 * also need to use schedule() instead when that's implemented.
		 */
		while (atomic_load(&irq->refcnt))
			arch_cpu_relax();
		destroy_irq(irq);

		unsigned long flags = local_irq_save();
		bug(irqctl->ops->uninstall(irqnum) != 0);
		local_irq_restore(flags);
	} else {
		__enable_irq(irq);
	}
	destroy_shared_handler(sh);
out:
	mutex_release(&irq_table_lock);
}

extern struct irqctl _ld_kernel_irqctl_start[];
extern struct irqctl _ld_kernel_irqctl_end[];

static inline struct irqctl* get_ctl(void) {
	struct irqctl* ret = NULL;
	for (struct irqctl* ctl = _ld_kernel_irqctl_start; ctl < _ld_kernel_irqctl_end; ctl++) {
		if (ctl->rating == 0)
			continue;
		if (!ret || ctl->rating > ret->rating)
			ret = ctl;
	}
	return ret;
}

static void irq_init(void) {
	irq_table = hashtable_create(32, sizeof(struct irq*));
	if (unlikely(!irq_table))
		out_of_memory();

	struct irqctl* ctl;
	do {
		ctl = get_ctl();
		if (ctl) {
			if (likely(ctl->dependencies))
				init_task_run_array(ctl->dependencies);
			else
				printk(PRINTK_WARN "irqctl: %s has no dependencies\n", ctl->name);
			if (ctl->ops->init_bsp() == 0)
				break;
			ctl->rating = 0;
		}
	} while (ctl);
	if (!ctl)
		panic("No interrupt controller");

	irqctl = ctl;
	printk("irq: Using interrupt controller %s\n", ctl->name);
}

void do_pending_irqs(void) {
	if (atomic_exchange(&running_pending, true) == true)
		return;

	spinlock_acquire(&pending_irqs_lock);

	struct irq* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, &pending_irqs, pending_link) {
		bool enter = enter_irq(pos); /* enter_irq sets/clears the pending bit automatically */
		if (enter) {
			run_irq_handlers(pos);
			remove_pending_locked(pos);
			exit_irq(pos);
			bug(irqctl->ops->enable(pos->number) != 0);
		}
	}

	atomic_store(&running_pending, false);
	spinlock_release(&pending_irqs_lock);
}

INIT_TASK_DECLARE(heap_init_task);
INIT_TASK_DEFINE(irq_init_task, INIT_TASK_SCOPE_BSP, irq_init, &heap_init_task);

static void irq_ap_init(void) {
	int err = irqctl->ops->init_ap();
	if (err)
		panic("irqctl->ops->init_ap() failed! %i", err);
}

INIT_TASK_DEFINE(irq_ap_init_task, INIT_TASK_SCOPE_AP, irq_ap_init, &irq_init_task);
