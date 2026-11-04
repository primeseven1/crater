#include <crater/proc.h>

__diag_ignore("-Wmissing-prototypes");

_Noreturn __asmlinkage void syscall_exit(const struct arch_context* ctx, int exit_code) {
	(void)ctx;
	proc_terminate((exit_code & 0xff) << 8);
}
