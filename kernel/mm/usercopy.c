#include <crater/usercopy.h>
#include <crater/sched.h>
#include <arch/usercopy.h>

void usercopy_enable(void) {
	if (++current_thread()->in_usercopy == 1)
		arch_usercopy_enter();
	compiler_barrier();
}

void usercopy_disable(void) {
	compiler_barrier();
	long count = --current_thread()->in_usercopy;
	if (count == 0)
		arch_usercopy_exit();
	else if (count < 0)
		bug("usercopy_disable() not properly paired with usercopy_enable()");
}

int usercopy_memset(void __user* dest, int val, size_t count) {
	if (!usercopy_access_ok(dest, count))
		return -EFAULT;

	int err = 0;
	usercopy_enable();

	u8 __user* d = dest;
	while (count--) {
		err = user_write_byte(d++, val);
		if (err)
			break;
	}

	usercopy_disable();
	return err;
}

int usercopy_from_user(void* dest, const void __user* src, size_t count) {
	if (IS_USER_ADDRESS(dest) || !usercopy_access_ok(src, count))
		return -EFAULT;

	int err = 0;
	usercopy_enable();

	u8* d = dest;
	const u8 __user* s = src;
	while (count--) {
		u8 x;
		err = user_read_byte(s++, &x);
		if (err)
			break;
		*d++ = x;
	}

	usercopy_disable();
	return err;
}

int usercopy_to_user(void __user* dest, const void* src, size_t count) {
	if (!usercopy_access_ok(dest, count) || IS_USER_ADDRESS(src))
		return -EFAULT;

	int err = 0;
	usercopy_enable();

	u8 __user* d = dest;
	const u8* s = src;
	while (count--) {
		err = user_write_byte(d++, *s++);
		if (err)
			break;
	}

	usercopy_disable();
	return err;
}

ssize_t usercopy_strlen(const char __user* str) {
	int err = 0;
	size_t len = 0;

	usercopy_enable();

	while (1) {
		const char __user* ptr = &str[len];
		if (!IS_USER_ADDRESS(ptr)) {
			err = -EFAULT;
			break;
		}

		char ch;
		err = user_read_byte(ptr, &ch);
		if (err || ch == '\0')
			break;

		len++;
	}

	usercopy_disable();
	return err ? err : (ssize_t)len;
}
