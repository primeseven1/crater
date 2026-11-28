#pragma once

#include <crater/types.h>
#include <crater/rbtree.h>
#include <crater/mutex.h>
#include <crater/uio.h>

struct page;
struct pagecache;

struct pagecache_ops {
	int (*readpage)(struct pagecache* pc, unsigned long index, void* buf, size_t len); 
	int (*writepage)(struct pagecache* pc, unsigned long index, const void* buf, size_t len);
};

struct pagecache {
	const struct pagecache_ops* ops;
	void* private;
	struct rbtree pages;
	size_t nr_pages, nr_dirty;
	mutex_t mtx;
};

#define PAGECACHE_CREATE (1 << 0) /* Allocate a zeroed page if the page is a hole */

/**
 * @brief Initialize a page cache
 *
 * @param pc The page cache to initialize
 * @param ops Backing store hooks, can be NULL for a cache with no backing store (eg. tmpfs)
 * @param private Private data for the owner
 */
void pagecache_init(struct pagecache* pc, const struct pagecache_ops* ops, void* private);

/**
 * @brief Destroy a page cache
 *
 * Drops every page without writing anything back. Call pagecache_sync() first if needed.
 * Pages still held by someone else stay alive until they are released.
 *
 * @param pc The page cache
 */
void pagecache_destroy(struct pagecache* pc);

/**
 * @brief Get a page from the cache
 *
 * If the page is not cached and the index is inside the file, it is read with ops->readpage.
 * Pages past the end of the file are never read, they are zero filled instead.
 *
 * @param[in] pc The page cache
 * @param[in] index Page index in the file
 * @param[in] size Current file size
 * @param[in] flags PAGECACHE_* flags
 * @param[out] out The page, held. Release it with release_page()
 *
 * @retval 0 Successful
 * @retval -ENOENT The page is a hole, and PAGECACHE_CREATE was not given
 * @retval -ENOMEM Out of memory
 * @return Anything ops->readpage() returns
 */
int pagecache_get_page(struct pagecache* pc, unsigned long index, size_t size, int flags, struct page** out);

/**
 * @brief Mark a page as dirty
 *
 * Does nothing if the page at index is no longer page (eg. it was truncated away).
 *
 * @param pc The page cache
 * @param index Page index in the file
 * @param page The page returned by pagecache_get_page()
 */
void pagecache_make_dirty(struct pagecache* pc, unsigned long index, struct page* page);

/**
 * @brief Read file data through the cache into a uio
 *
 * Reads stop at the end of the file. Holes are read as zeroes without allocating pages.
 *
 * @param pc The page cache
 * @param uio The uio, must be UIO_READ
 * @param size Current file size
 *
 * @return 0 on success, -errno on failure. Check uio->resid to see if anything was read.
 */
int pagecache_read(struct pagecache* pc, struct uio* uio, size_t size);

/**
 * @brief Write data from a uio into the cache
 *
 * The caller is responsible for checking that the write does not overflow the file size.
 *
 * @param pc The page cache
 * @param uio The uio, must be UIO_WRITE
 * @param size The file size, updated if the write extends the file
 *
 * @return 0 on success, -errno on failure. Check uio->resid to see if anything was written.
 */
int pagecache_write(struct pagecache* pc, struct uio* uio, size_t* size);

/**
 * @brief Change the size of the cached file
 *
 * Drops every page past the new end of the file (dirty or not), and zeroes the tail
 * of the new last page. Growing a file needs no work because of the zero invariant.
 *
 * @param pc The page cache
 * @param new_size The new file size
 */
void pagecache_truncate(struct pagecache* pc, size_t new_size);

/**
 * @brief Write back every dirty page
 *
 * @param pc The page cache
 * @param size Current file size
 *
 * @retval 0 Successful, or the cache has no backing store
 * @return The first error from ops->writepage(), the remaining pages are still attempted
 */
int pagecache_sync(struct pagecache* pc, size_t size);

/**
 * @brief Drop clean pages that nobody else is using
 *
 * Does nothing for a cache with no backing store (ops->readpage is NULL), since the
 * data could not be read back.
 *
 * @param pc The page cache
 * @return The number of pages freed
 */
size_t pagecache_evict(struct pagecache* pc);
