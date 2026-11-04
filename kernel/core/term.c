#include <crater/term.h>
#include <crater/list.h>
#include <crater/mutex.h>
#include <crater/module.h>
#include <crater/cmdline.h>
#include <crater/printk.h>
#include <crater/slab.h>

struct term_hook {
	void (*write)(const char*, size_t);
	struct list_node link;
};

static LIST_HEAD_DEFINE(term_hook_head);
static SPINLOCK_DEFINE(hooks_lock);

int term_driver_register(void (*write)(const char*, size_t)) {
	struct term_hook* hook = kmalloc(sizeof(*hook), MM_ZONE_NORMAL);
	if (!hook)
		return -ENOMEM;
	list_node_init(&hook->link);
	hook->write = write;

	int ret = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&hooks_lock, &irq_flags);

	struct term_hook* pos;
	list_for_each_entry(pos, &term_hook_head, link) {
		if (pos->write == write) {
			ret = -EALREADY;
			goto out;
		}
	}

	list_add(&term_hook_head, &hook->link);
out:
	spinlock_release_irq_restore(&hooks_lock, &irq_flags);
	if (ret)
		kfree(hook);
	return ret;
}

void term_write(const char* str, size_t count) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&hooks_lock, &irq_flags);

	struct list_node* pos;
	list_for_each(pos, &term_hook_head) {
		struct term_hook* hook = list_entry(pos, struct term_hook, link);
		hook->write(str, count);
	}

	spinlock_release_irq_restore(&hooks_lock, &irq_flags);
}

static void term_init(void) {
	const char* driver = cmdline_get("term_driver");
	if (driver) {
		int err = module_load(driver);
		if (err)
			printk(PRINTK_ERR "core: Failed to load terminal driver %s, err: %i\n", driver, err);
	} else {
		printk(PRINTK_ERR "core: No terminal driver specified, try setting the term_driver command line option\n");
	}
}

INIT_TASK_DECLARE(heap_init_task, cmdline_init_task);
INIT_TASK_DEFINE(term_init_task, INIT_TASK_SCOPE_BSP, term_init, &heap_init_task, &cmdline_init_task);
