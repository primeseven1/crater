#include <crater/uio.h>
#include <crater/string.h>
#include <crater/usercopy.h>

int uio_init(struct uio* uio, const struct iovec* iov, size_t iov_count, off_t offset, enum uio_seg seg, enum uio_rw rw) {
	if (offset < 0 || iov_count > IOV_MAX || (!iov && iov_count))
		return -EINVAL;

	size_t total = 0;
	for (size_t i = 0; i < iov_count; i++) {
		size_t len = iov[i].iov_len;
		if (len > (size_t)SSIZE_MAX - total)
			return -EINVAL;
		if (len == 0)
			continue;
		total += len;

		if (seg == UIO_USERSPACE) {
			if (!usercopy_access_ok(iov[i].iov_base_user, len))
				return -EFAULT;
		} else if (IS_USER_ADDRESS(iov[i].iov_base)) {
			return -EFAULT;
		}
	}

	*uio = (struct uio){ .iov = iov, .iov_count = iov_count, .iov_offset = 0, .offset = offset, .resid = total, .seg = seg, .rw = rw };
	return 0;
}

static void uio_skip_empty(struct uio* uio) {
	while (uio->iov_offset == uio->iov->iov_len) {
		uio->iov++;
		uio->iov_count--;
		uio->iov_offset = 0;
	}
}

static void uio_advance(struct uio* uio, size_t count) {
	uio->iov_offset += count;
	uio->offset += count;
	uio->resid -= count;
}

static size_t uio_current(struct uio* uio, size_t max, uintptr_t* out_base) {
	uio_skip_empty(uio);
	size_t len = uio->iov->iov_len - uio->iov_offset;
	*out_base = (uintptr_t)uio->iov->iov_base + uio->iov_offset;
	return (len > max) ? max : len;
}

int uio_move(void* buf, size_t count, struct uio* uio) {
	if (count > uio->resid)
		count = uio->resid;

	u8* b = buf;
	while (count) {
		uintptr_t base;
		size_t len = uio_current(uio, count, &base);

		int err = 0;
		if (uio->rw == UIO_READ)
			err = (uio->seg == UIO_USERSPACE) ? usercopy_to_user((__force void __user*)base, b, len) : (memcpy((void*)base, b, len), 0);
		else
			err = (uio->seg == UIO_USERSPACE) ? usercopy_from_user(b, (__force void __user*)base, len) : (memcpy(b, (void*)base, len), 0);
		if (err)
			return err;

		uio_advance(uio, len);
		b += len;
		count -= len;
	}

	return 0;
}

int uio_zero(size_t count, struct uio* uio) {
	if (uio->rw != UIO_READ)
		return -EINVAL;
	if (count > uio->resid)
		count = uio->resid;

	while (count) {
		uintptr_t base;
		size_t size = uio_current(uio, count, &base);
		if (uio->seg == UIO_USERSPACE) {
			int err = usercopy_memset((__force void __user*)base, 0, size);
			if (err)
				return err;
		} else {
			memset((void*)base, 0, size);
		}

		uio_advance(uio, size);
		count -= size;
	}

	return 0;
}

void uio_skip(size_t count, struct uio* uio) {
	if (count > uio->resid)
		count = uio->resid;
	while (count) {
		uintptr_t tmp;
		size_t len = uio_current(uio, count, &tmp);
		uio_advance(uio, len);
		count -= len;
	}
}

int iovec_copy_from_user(struct iovec* dest, const struct iovec __user* src, size_t count) {
	if (count > IOV_MAX)
		return -EINVAL;
	return usercopy_from_user(dest, src, count * sizeof(*dest));
}
