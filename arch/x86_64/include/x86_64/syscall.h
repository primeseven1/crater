#pragma once

#include <arch/asm/linkage.h>

__asmlinkage void arch_x86_64_syscall_entry(void);
__asmlinkage void arch_x86_64_compat_syscall_entry(void);;
