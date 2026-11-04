#include <crater/sched.h>

__diag_ignore("-Wmissing-prototypes");

__asmlinkage long syscall_yield(void) {
	return sched_yield();
}
