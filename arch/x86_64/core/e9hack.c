#ifdef CONFIG_ARCH_X86_64_E9HACK

#include <crater/string.h>

#include <x86_64/e9.h>
#include <x86_64/pmio.h>
#include <x86_64/asm/cpuid.h>

static void e9_printk_hook(int level, int global_level, const char* msg) {
	(void)level;
	(void)global_level;
	while (*msg)
		arch_x86_64_outb(0xe9, *msg++);
}

void arch_x86_64_e9_init(void) {
	if (arch_x86_64_inb(0xe9) != 0xe9) {
		printk(PRINTK_WARN "e9hack: Is this real hardware?\n");
		return;
	}

	/* Don't really care if this fails or not */
	int err = printk_set_hook(e9_printk_hook);
	if (err)
		printk(PRINTK_WARN "e9hack: Failed to set hook: %i\n", err);
}

#endif /* CONFIG_ARCH_X86_64_E9HACK */
