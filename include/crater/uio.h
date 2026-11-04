#pragma once

#include <crater/types.h>
#include <crater/errno.h>
#include <arch/posix.h>

enum uio_seg {
	UIO_SYSSPACE,
	UIO_USERSPACE
};

enum uio_rw {
	UIO_READ,
	UIO_WRITE
};

struct uio {
	const struct iovec* iov;
	size_t iov_count;
	size_t iov_offset;
	off_t offset;
	size_t resid;
	enum uio_seg seg;
	enum uio_rw rw;
};

/**
 * @brief Initialize a uio
 *
 * @param[out] The uio to initialize
 * @param[in] iov Array of iovecs, can be NULL is iov_count is zero
 * @param[in] iov_count The number of iovecs in the iovec array
 * @param[in] offset Starting file offset
 * @param[in] seg Where the buffer lives (UIO_SYSSPACE/UIO_USERSPACE)
 * @param[in] rw Direction of the transfer (UIO_READ/UIO_WRITE)
 *
 * @retval -EINVAL Negative offset, iov_count is more than IOV_MAX, iov is NULL with an iov_count, or the length does not fit in a ssize_t
 * @retval -EFAULT The buffer is not in the address space given by seg
 * @retval 0 Successful
 */
int uio_init(struct uio* uio, const struct iovec* iov, size_t iov_count, off_t offset, enum uio_seg seg, enum uio_rw rw);

int uio_move(void* buf, size_t count, struct uio* uio);

void uio_skip(size_t count, struct uio* uio);

int uio_zero(size_t count, struct uio* uio);

/**
 * @brief Copy an iovec array from user space
 *
 * @param dest The kernel buffer
 * @param src The user buffer to copy from
 * @param count Number of iovec structures to copy
 *
 * @retval -EFAULT Invalid user address
 * @retval 0 Successful
 */
int iovec_copy_from_user(struct iovec* dest, const struct iovec __user* src, size_t count);
