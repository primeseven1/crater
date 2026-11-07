#include <crater/usercopy.h>
#include <crater/slab.h>
#include <crater/uio.h>
#include <crater/usercopy.h>
#include <crater/fd.h>

#include <arch/context.h>
#include <arch/asm/linkage.h>

static int writev(int fd, struct iovec* iov, int iov_count, size_t* wcount) {
	struct file* file = fd_get(fd);
	if (!file)
		return -EBADF;

	int err = 0;
	mutex_acquire(&file->offset_flags_mtx);

	if (!(file->flags & FILE_WRITE)) {
		err = -EBADF;
		goto out;
	}

	off_t offset = file->offset;
	if (unlikely(offset < 0)) {
		err = -EBADFD;
		goto out;
	}

	/*
	 * TODO: The size can change in between this and the vfs_writev(), so two opens with append mode can compute the same offset and overwrite each other,
	 * this will be fixed later
	 */
	if (file->flags & FILE_APPEND) {
		struct vattr attrs;
		VOP_LOCK(file->vnode);
		err = VOP_GETATTR(file->vnode, &attrs, current_cred());
		VOP_UNLOCK(file->vnode);
		if (err)
			goto out;
		offset = attrs.size;
	}

	struct uio uio;
	err = uio_init(&uio, iov, iov_count, offset, UIO_USERSPACE, UIO_WRITE);
	if (err)
		goto out;
	err = vfs_writev(file->vnode, &uio, file_flags_to_vfs_flags(file->flags), wcount);
	if (err)
		goto out;

	file->offset = offset + *wcount;
out:
	mutex_release(&file->offset_flags_mtx);
	fd_release(file);
	return err;
}

__diag_ignore("-Wmissing-prototypes");

__asmlinkage long syscall_writev(const struct arch_context* ctx, int fd, struct iovec __user* iov, int iov_count) {
	(void)ctx;
	if (iov_count < 0 || iov_count > IOV_MAX)
		return -EINVAL;

	struct iovec* iov_kernel = kmalloc(sizeof(*iov_kernel) * iov_count, MM_ZONE_NORMAL);
	if (!iov_kernel)
		return -ENOMEM;

	size_t write_count;
	int err = usercopy_from_user(iov_kernel, iov, sizeof(*iov_kernel) * iov_count);
	if (err == 0)
		err = writev(fd, iov_kernel, iov_count, &write_count);

	kfree(iov_kernel);
	return (err == 0) ? (long)write_count : err;
}

__asmlinkage long syscall_write(const struct arch_context* ctx, int fd, const char __user* buf, size_t len) {
	(void)ctx;

	struct iovec iov = { .iov_base_user = (char __user*)buf, .iov_len = len };
	size_t write_count;
	int err = writev(fd, &iov, 1, &write_count);

	return (err == 0) ? (long)write_count : err;
}
