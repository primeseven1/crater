#pragma once

#include <crater/types.h>
#include <crater/errno.h>

#define RINGBUFFER_SIZE_MINIMUM 8

struct ringbuffer {
	void* buffer;
	size_t write, read;
	size_t size;
};

/**
 * @brief Get the amount of data in a ringbuffer
 * @param rb The ringbuffer
 * @return The amount of data in the ringbuffer
 */
static inline size_t ringbuffer_datacount(struct ringbuffer* rb) {
	return (rb->write - rb->read) & (rb->size - 1);
}

/**
 * @brief Get the amount of free space in a ringbuffer
 * @param rb The ringbuffer
 * @return The number of free bytes in the ringbuffer
 */
static inline size_t ringbuffer_freespace(struct ringbuffer* rb) {
	return rb->size - ringbuffer_datacount(rb) - 1;
}

/**
 * @brief Discard all data in a ringbuffer
 * @param rb The ringbuffer
 */
static inline void ringbuffer_clear(struct ringbuffer* rb) {
	rb->read = rb->write;
}

/**
 * @brief Initialize a ringbuffer
 *
 * The size is rounded to a power of two. The size must at least
 * be RINGBUFFER_SIZE_MINIMUM, otherwise -EINVAL is returned.
 *
 * The buffer is allocated using kmalloc(). If the size of the ringbuffer
 * is more than a couple pages, vmalloc() is used instead.
 *
 * @param rb The ringbuffer to initialize
 * @param size The size of the ringbuffer.
 *
 * @retval -ENOMEM Out of memory
 * @retval -EINVAL Size is less than RINGBUFFER_SIZE_MINIMUM
 * @retval -ERANGE Size overflows when rounding to a power of two
 * @retval 0 Success
 */
int ringbuffer_init(struct ringbuffer* rb, size_t size);

/**
 * @brief Free the resources of a ringbuffer
 * @param rb The ringbuffer
 */
void ringbuffer_destroy(struct ringbuffer* rb);

/**
 * @brief Read from a ringbuffer
 *
 * If `dest` is NULL, data is discarded
 *
 * @param rb The ringbuffer to read from
 * @param dest Where to copy the data to
 * @param count The maximum number of bytes to copy
 *
 * @return The number of bytes read
 */
size_t ringbuffer_read(struct ringbuffer* rb, void* dest, size_t count);

/**
 * @brief Read from a ringbuffer with a user provided cursor
 *
 * @param rb The ringbuffer to read from
 * @param dest Where to copy the data to
 * @param offset Offset from the current read pointer
 * @param count The maximum number of bytes to read
 *
 * @return The number of bytes read
 */
size_t ringbuffer_peek(struct ringbuffer* rb, void* dest, size_t offset, size_t count);

/**
 * @brief Write data to a ringbuffer
 *
 * @param rb The ringbuffer to write to
 * @param src Where to copy from
 * @param count The maximum number of bytes to copy
 *
 * @return The number of bytes written
 */
size_t ringbuffer_write(struct ringbuffer* rb, const void* src, size_t count);

/**
 * @brief Write data to a ringbuffer only if everything fits
 *
 * @param rb The ringbuffer
 * @param src Where to copy from
 * @param count The number of bytes to copy
 *
 * @return The number of bytes written, returns 0 if there is not enough free space
 */
size_t ringbuffer_write_all(struct ringbuffer* rb, const void* src, size_t count);

struct element_ringbuffer {
	struct ringbuffer rb;
	size_t esize;
};

/**
 * @brief Create an element ringbuffer
 *
 * @param erb The ringbuffer
 * @param ecount The number of elements
 * @param esize The size of each element
 *
 * @retval -EINVAL Bad ringbuffer size
 * @retval -ERANGE ecount * esize overflows, or the total size overflows when being rounded to a power of two
 * @retval -ENOMEM Cannot allocate memory for the ringbuffer
 * @retval 0 Successful
 */
int element_ringbuffer_init(struct element_ringbuffer* erb, size_t ecount, size_t esize);

/**
 * @brief Destroy an element ringbuffer
 * @param erb The ringbuffer
 */
void element_ringbuffer_destroy(struct element_ringbuffer* erb);

/**
 * @brief Get the number of elements available to read in the ringbuffer
 * @param erb The ringbuffer
 * @return The number of elements
 */
static inline size_t element_ringbuffer_datacount(struct element_ringbuffer* erb) {
	return ringbuffer_datacount(&erb->rb) / erb->esize;
}

/**
 * @brief Get the number of free elements in the ringbuffer
 * @param erb The ringbuffer
 * @return The number of free elements
 */
static inline size_t element_ringbuffer_freespace(struct element_ringbuffer* erb) {
	return ringbuffer_freespace(&erb->rb) / erb->esize;
}

/**
 * @brief Discard all data in an element ringbuffer
 * @param erb The ringbuffer
 */
static inline void element_ringbuffer_clear(struct element_ringbuffer* erb) {
	ringbuffer_clear(&erb->rb);
}

/**
 * @brief Read elements from a ringbuffer
 *
 * @param erb The ringbuffer
 * @param dest The destination
 * @param count The number of elements
 *
 * @return The number of elements read
 */
static inline size_t element_ringbuffer_read(struct element_ringbuffer* erb, void* dest, size_t count) {
	count = MIN(element_ringbuffer_datacount(erb), count);
	return ringbuffer_read(&erb->rb, dest, count * erb->esize) / erb->esize;
}

/**
 * @brief Read elements from a ringbuffer, but with a user provided cursor
 *
 * @param erb The ringbuffer
 * @param dest The destination
 * @param offset Offset from the current read pointer
 * @param count The number of elements to read
 *
 * @return The number of elements read
 */
static inline size_t element_ringbuffer_peek(struct element_ringbuffer* erb, void* dest, size_t offset, size_t count) {
	const size_t datacount = element_ringbuffer_datacount(erb);
	if (offset >= datacount)
		count = 0;
	else
		count = MIN(count, datacount - offset);
	return ringbuffer_peek(&erb->rb, dest, offset * erb->esize, count * erb->esize) / erb->esize;
}

/**
 * @brief Write elements to a ringbuffer
 *
 * @param erb The ringbuffer
 * @param src Where to copy from
 * @param count The number of elements to write
 *
 * @return The number of elements written
 */
static inline size_t element_ringbuffer_write(struct element_ringbuffer* erb, const void* src, size_t count) {
	count = MIN(element_ringbuffer_freespace(erb), count);
	return ringbuffer_write(&erb->rb, src, count * erb->esize) / erb->esize;
}
