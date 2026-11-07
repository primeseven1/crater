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

/**
 * @brief Move data from a uio into a kernel buffer
 *
 * If this function returns an error, you should also check to see if
 * anything was written successfully.
 *
 * @param buf The buffer
 * @param count The number of bytes to move
 * @param uio The uio
 *
 * @retval -EFAULT A usercopy fault happened
 * @retval 0 Successful
 */
int uio_move(void* buf, size_t count, struct uio* uio);

/**
 * @brief Discard data in a uio
 *
 * @param count The number of bytes to discard
 * @param uio The uio
 */
void uio_skip(size_t count, struct uio* uio);

/**
 * @brief Write zero's into a uio buffer
 *
 * @param count The number of bytes
 * @param uio The uio
 *
 * @retval -EINVAL The UIO is not in UIO_READ mode
 * @retval -EFAULT A usercopy fault happened
 * @retval 0 Successful
 */
int uio_zero(size_t count, struct uio* uio);
