#pragma once

#include <crater/compiler.h>
#include <crater/types.h>
#include <crater/spinlock.h>
#include <crater/mutex.h>
#include <crater/string.h>
#include <crater/mm.h>
#include <crater/list.h>
#include <crater/errno.h>

struct slab {
	struct page* self_page;
	struct {
		void* virtual;
		struct page* page;
	} base;
	struct {
		u8* virtual;
		struct page* page;
	} free;
	size_t in_use;
	struct list_node link;
};

struct slab_cache {
	void (*ctor)(void*);
	void (*dtor)(void*);
	struct list_head full, partial, empty;
	size_t obj_size;
	unsigned long obj_count;
	size_t align;
	mm_t mm_flags;
	union {
		spinlock_t spinlock;
		mutex_t mutex;
	};
};

/**
 * @brief Create a new slab cache
 *
 * If zero is passed to align, it will set it to 8. Not safe to call from an atomic context.
 *
 * @param obj_size The size of the object. This will be rounded to the alignment
 * @param align The alignment of the object, must be a power of 2
 * @param mm_flags The MM flags for this cache
 * @param ctor Object constructor
 * @param dtor Object destructor
 *
 * @return NULL if align isn't a power of 2, or there is no memory
 */
struct slab_cache* slab_cache_create(size_t obj_size, size_t align,
		mm_t mm_flags, void (*ctor)(void*), void (*dtor)(void*));

/**
 * @brief Destroy a slab cache
 * 
 * This function will make sure no slabs are empty before attempting to destroy.
 * Not safe to call from an atomic context.
 *
 * @param cache The cache to destroy
 *
 * @retval 0 Success
 * @retval -EBUSY There are still active allocations in the cache
 * @retval -EWOULDBLOCK The lock is acquired on the cache
 */
int slab_cache_destroy(struct slab_cache* cache);

/**
 * @brief Allocate memory from a slab cache
 *
 * This function will automatically try to grow the cache if all slabs are full.
 * Safe to call from an interrupt context assuming the cache was created with MM_ATOMIC.
 *
 * @param cache The cache to allocate from
 * @return NULL if there is no memory available, otherwise you get a mapped virtual address
 */
void* slab_cache_alloc(struct slab_cache* cache);

/**
 * @brief Free memory from a slab cache
 *
 * If the object isn't in any slab, it will print an error message and just return.
 * Safe to call from an interrupt context assuming the cache was created with MM_ATOMIC.
 *
 * @param obj The object to free
 */
void slab_cache_free(struct slab_cache* cache, void* obj);

/**
 * @brief Allocate kernel memory
 *
 * @param size The size of the allocation
 * @param mm_flags Conditions for the allocation
 *
 * @return The pointer to the memory, or NULL on failure
 */
void* kmalloc(size_t size, mm_t mm_flags);

/**
 * @brief Free memory from the kernel heap
 *
 * If `ptr` is NULL, this function is a no-op.
 *
 * @param ptr The pointer to free
 */
void kfree(void* ptr);

/**
 * @brief Resize a block of memory
 *
 * If `old` is NULL, this function just returns kmalloc(new_size).
 *
 * @param old The old pointer
 * @param new_size The size of the new block
 * @param mm_flags The conditions for the new allocation
 *
 * @return The pointer to the new block of memory
 */
void* krealloc(void* old, size_t new_size, mm_t mm_flags);

static inline void* kzalloc(size_t size, mm_t mm_flags) {
	void* ptr = kmalloc(size, mm_flags);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

static inline void* kcalloc(size_t ecount, size_t esize, mm_t mm_flags) {
	size_t size;
	return (__builtin_mul_overflow(ecount, esize, &size)) ? NULL : kzalloc(size, mm_flags);
}
