#include <crater/term.h>
#include <crater/usercopy.h>
#include <crater/vmm.h>

#include <arch/context.h>
#include <arch/asm/linkage.h>

__diag_ignore("-Wmissing-prototypes");

__asmlinkage long syscall_write(const struct arch_context* ctx, int fd, const char __user* buf, size_t len) {
	(void)ctx;

	if (len > SSIZE_MAX)
		return -EINVAL;
	if (fd != 1) /* stdout */
		return -EBADF;
	if (len == 0)
		return 0;

	char* kbuf = vmalloc(len);
	if (!kbuf)
		return -ENOMEM;
	int err = usercopy_from_user(kbuf, buf, len);
	if (err)
		goto out;

	term_write(kbuf, len);
out:
	vfree(kbuf);
	return err == 0 ? (long)len : err;
}
