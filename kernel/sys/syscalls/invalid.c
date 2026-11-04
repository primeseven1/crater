#include <crater/sched.h>
#include <crater/proc.h>
#include <crater/printk.h>

__diag_ignore("-Wmissing-prototypes");

__asmlinkage long syscall_invalid(const struct arch_context* ctx) {
	struct proc* proc = current_proc();
	printk(PRINTK_ERR "syscall: PID %u made an invalid system call (IP: %#lx)\n", proc->pid, ARCH_CONTEXT_IP(ctx));
	return -ENOSYS;
}
