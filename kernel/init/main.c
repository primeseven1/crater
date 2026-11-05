#include <crater/init.h>
#include <crater/printk.h>
#include <crater/panic.h>
#include <crater/sched.h>
#include <crater/vmm.h>
#include <crater/module.h>
#include <crater/vfs.h>
#include <crater/input.h>
#include <crater/cmdline.h>
#include <crater/proc.h>
#include <crater/elf.h>

#include <arch/tlb.h>
#include <arch/irq_flags.h>

#include <acpi/driver.h>

#include "internal.h"

static void wait_on_bsp_dependency(const char* curr_name, struct init_task* dependency) {
	bool logged = false;
	while (!cpumask_test(&dependency->done, 0)) {
		if (!logged) {
			printk(PRINTK_INFO "init: %s waiting on %s\n",
					curr_name, dependency->name);
			logged = true;
		}
		arch_cpu_relax();
	}
}

void init_task_run(struct init_task* task) {
	u32 sched_id = current_cpu()->runqueue.sched_id;
	if ((sched_id != 0 && task->scope == INIT_TASK_SCOPE_BSP) || (sched_id == 0 && task->scope == INIT_TASK_SCOPE_AP))
		return;

	if (cpumask_test(&task->done, sched_id))
		return;
	if (cpumask_test(&task->running, sched_id))
		panic("recursive init dependency: %s", task->name);

	cpumask_set(&task->running, sched_id, true);
	for (struct init_task** deps = task->dependencies; *deps; deps++) {
		if ((*deps)->scope == INIT_TASK_SCOPE_BSP && sched_id != 0) {
			wait_on_bsp_dependency(task->name, *deps);
		} else {
			if ((task->scope == INIT_TASK_SCOPE_BSP || task->scope == INIT_TASK_SCOPE_BSP_AP) && (*deps)->scope == INIT_TASK_SCOPE_AP) {
				panic("init task %s (scope %d) relies on init task %s (scope %d)",
						task->name, task->scope, (*deps)->name, (*deps)->scope);
			}
			init_task_run(*deps);
		}
	}

	printk(PRINTK_DBG "init: running task %s\n", task->name);
	task->func();

	cpumask_set(&task->done, sched_id, true);
	cpumask_set(&task->running, sched_id, false);
}

extern struct init_task _ld_kernel_initt_start[];
extern struct init_task _ld_kernel_initt_end[];

static inline void init_run_all_tasks(void) {
	for (struct init_task* t = _ld_kernel_initt_start; t < _ld_kernel_initt_end; t++)
		init_task_run(t);
}

_Noreturn void kernel_ap_main(void) {
	init_run_all_tasks();
	arch_tlb_flush_all();
	smp_register_cpu();
	smp_init_complete();
	smp_init_wait_for_all();
	preempt_init();
	local_irq_enable();
	printk(PRINTK_DBG "smp: CPU %u up\n", current_cpu()->runqueue.sched_id);
	sched_thread_exit();
}

INIT_TASK_DECLARE(printk_init_task, term_init_task, limine_base_revision_init_task);

/* This function does zero cleanup on failure, since this function failing means a kernel panic */
static int start_init(struct vnode* vnode) {
	struct proc* proc = proc_create();
	if (!proc)
		return -ENOMEM;

	/* No tty, so use /dev/null for now */
	struct vnode* console;
	int err = vfs_open(NULL, "/dev/null", 0, &console);
	if (err)
		return err;
	err = fd_init_for_init(proc, console);
	if (err)
		return err;

	struct thread* thread = alloc_thread();
	if (!thread)
		return -ENOMEM;
	err = alloc_thread_stack(thread, NULL);
	if (err)
		return err;
	thread_topology_init(thread, 0);

	struct mm* kernel_mm = current_mm();
	mm_switch_context(proc->mm_struct);

	u8 __user* user_stack;
	struct elf64_auxv_list auxv_list;
	err = elf_load(vnode, &auxv_list);
	if (likely(err == 0)) {
		const size_t user_stack_size = 0x200000;
		user_stack = vm_map_user(NULL, user_stack_size, PGPROT_READ | PGPROT_WRITE, VMM_STACK, NULL);
		if (IS_PTR_ERR(user_stack))
			err = PTR_ERR(user_stack);
		else
			user_stack += user_stack_size;
	}

	mm_switch_context(kernel_mm);
	if (unlikely(err))
		return err;

	err = sched_thread_attach(thread, proc, SCHED_PRIO_DEFAULT);
	if (err == 0) {
		arch_context_prepare_execution(&thread->context.arch_context, auxv_list.entry.value, (uintptr_t)user_stack);
		err = sched_enqueue(thread);
	}

	return err;
}

static void print_version(void) {
#ifdef CONFIG_LLVM
	const char* compiler = "clang";
	const int cc_major = __clang_major__;
	const int cc_minor = __clang_minor__;
	const int cc_patchlevel = __clang_patchlevel__;
#else
	const char* compiler = "gcc";
	const int cc_major = __GNUC__;
	const int cc_minor = __GNUC_MINOR__;
	const int cc_patchlevel = __GNUC_PATCHLEVEL__;
#endif /* CONFIG_LLVM */
	printk("Crater version %d.%02d (%s %d.%d.%d)\n", CRATER_MAJOR, CRATER_MINOR, compiler, cc_major, cc_minor, cc_patchlevel);
}

_Noreturn void kernel_main(void) {
	print_version();

	init_task_run(&limine_base_revision_init_task); /* Sanity check the bootloader */
	init_task_run(&printk_init_task); /* Set loglevel */
	init_task_run(&term_init_task); /* See printk messages on the screen */
	init_run_all_tasks();
	smp_init_bsp_wait_for_others();

	/*
	 * smp_init_bsp_wait_for_others() will ensure no mutexes are held the time of transition,
	 * and if mutexes are held, somebody (probably me) really fucked up.
	 */
	mutex_disable_spinlock();
	arch_tlb_flush_all();
	tlb_shootdown_init();
	smp_init_complete();

	/* All CPU's are running at this point */
	preempt_init();
	local_irq_enable();
	module_load_builtins();
	acpi_drivers_load();

	mount_root_initrd();

	/* Will get removed eventually */
	keyboard_reader_thread_init();

	const char* init_cmdline = cmdline_get("init");
	struct vnode* vnode;
	int err = vfs_open(NULL, init_cmdline ? init_cmdline : "/sbin/init", 0, &vnode);
	if (err)
		panic("No init found, try setting the init command line option");
	err = start_init(vnode);
	if (err) {
		if (err == -ENOMEM)
			out_of_memory();
		panic("Failed to start init: %d", err);
	}
	bug(vfs_close(vnode, 0) != 0);
	VOP_RELEASE(vnode);

	sched_thread_exit();
}
