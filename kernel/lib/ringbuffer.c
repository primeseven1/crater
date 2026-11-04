#include <crater/slab.h>
#include <crater/vmm.h>
#include <crater/ringbuffer.h>
#include <crater/panic.h>

#define RB_KMALLOC_LIMIT (PAGE_SIZE * 2)

int ringbuffer_init(struct ringbuffer* rb, size_t size) {
	if (size < RINGBUFFER_SIZE_MINIMUM)
		return -EINVAL;
	if (size & (size - 1)) {
		size = roundup_pow2(size);
		if (size == 0)
			return -ERANGE;
	}
	rb->buffer = size < RB_KMALLOC_LIMIT ? kzalloc(size, MM_ZONE_NORMAL) : vmalloc(size);
	if (!rb->buffer)
		return -ENOMEM;

	rb->size = size;
	rb->read = 0;
	rb->write = 0;

	return 0;
}

void ringbuffer_destroy(struct ringbuffer* rb) {
	if (rb->size < RB_KMALLOC_LIMIT)
		kfree(rb->buffer);
	else
		vfree(rb->buffer);
}

size_t ringbuffer_read(struct ringbuffer* rb, void* dest, size_t count) {
	const size_t datacount = ringbuffer_datacount(rb);
	count = (count < datacount) ? count : datacount;

	const size_t firstpassoffset = rb->read & (rb->size - 1);
	const size_t firstpassremaining = rb->size - firstpassoffset;
	const size_t firstpasscount = (count < firstpassremaining) ? count : firstpassremaining;

	if (dest) {
		memcpy(dest, (u8*)rb->buffer + firstpassoffset, firstpasscount);
		if (firstpasscount != count)
			memcpy((u8*)dest + firstpasscount, rb->buffer, count - firstpasscount);
	}

	rb->read = (rb->read + count) & (rb->size - 1);
	return count;
}

size_t ringbuffer_peek(struct ringbuffer* rb, void* dest, size_t offset, size_t count) {
	const size_t datacount = ringbuffer_datacount(rb);
	if (offset >= datacount)
		return 0;

	const size_t read = (rb->read + offset) & (rb->size - 1);
	const size_t offsetdatacount = datacount - offset;
	count = (count < offsetdatacount) ? count : offsetdatacount;

	const size_t firstpassoffset = read & (rb->size - 1);
	const size_t firstpassremaining = rb->size - firstpassoffset;
	const size_t firstpasscount = (count < firstpassremaining) ? count : firstpassremaining;

	memcpy(dest, (u8*)rb->buffer + firstpassoffset, firstpasscount);
	if (firstpasscount != count)
		memcpy((u8*)dest + firstpasscount, rb->buffer, count - firstpasscount);

	return count;
}

size_t ringbuffer_write(struct ringbuffer* rb, const void* src, size_t count) {
	const size_t freespace = ringbuffer_freespace(rb);
	count = (count < freespace) ? count : freespace;

	const size_t firstpassoffset = rb->write & (rb->size - 1);
	const size_t firstpassremaining = rb->size - firstpassoffset;
	const size_t firstpasscount = count < firstpassremaining ? count : firstpassremaining;

	memcpy((u8*)rb->buffer + firstpassoffset, src, firstpasscount);
	if (firstpasscount != count)
		memcpy(rb->buffer, (u8*)src + firstpasscount, count - firstpasscount);

	rb->write = (rb->write + count) & (rb->size - 1);
	return count;
}

size_t ringbuffer_write_all(struct ringbuffer* rb, const void* src, size_t count) {
	size_t ret = 0;
	if (ringbuffer_freespace(rb) >= count) {
		ret = ringbuffer_write(rb, src, count);
		bug(ret != count); /* This won't trigger unless locking isn't done properly */
	}
	return ret;
}

int element_ringbuffer_init(struct element_ringbuffer* erb, size_t ecount, size_t esize) {
	size_t rb_size;
	if (__builtin_mul_overflow(ecount, esize, &rb_size))
		return -ERANGE;
	int err = ringbuffer_init(&erb->rb, rb_size);
	if (err == 0)
		erb->esize = esize;
	return err;
}

void element_ringbuffer_destroy(struct element_ringbuffer* erb) {
	ringbuffer_destroy(&erb->rb);
	erb->esize = 0;
}
