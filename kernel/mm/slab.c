#include <crater/common.h>
#include <crater/slab.h>
#include <crater/mm.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/trace.h>
#include <crater/panic.h>
#include <crater/string.h>

/* The maximum size a slab can be before the object count goes to SLAB_AFTER_CUTOFF_COUNT */
#define SLAB_SIZE_CUTOFF 512
#define SLAB_AFTER_CUTOFF_OBJ_COUNT 16

static inline void* slab_alloc_struct(mm_t mm_flags, size_t size, struct page** out) {
	struct page* page = alloc_pages(mm_flags, get_order(size));
	if (!page)
		return NULL;

	void* ptr = page_hhdm_virtual(page);
	memset(ptr, 0, size);

	*out = page;
	return ptr;
}

static inline void slab_free_struct(struct page* page) {
	release_page(page);
}

static int slab_init(struct slab_cache* cache, struct page* slab_page, struct slab* slab) {
	slab->self_page = slab_page;

	size_t map_size = (cache->obj_count + 7) >> 3;
	size_t slab_size = cache->obj_size * cache->obj_count;

	/* Allocate free list bitmap */
	slab->free.virtual = slab_alloc_struct(cache->mm_flags, map_size, &slab->free.page);
	if (!slab->free.virtual)
		return -ENOMEM;

	/* Allocate a virtual address for the slab base */
	slab->base.virtual = slab_alloc_struct(cache->mm_flags, slab_size, &slab->base.page);
	if (!slab->base.virtual) {
		slab_free_struct(slab->free.page);
		return -ENOMEM;
	}

	slab->in_use = 0;
	list_node_init(&slab->link);
	return 0;
}

static int slab_cache_grow(struct slab_cache* cache) {
	struct page* slab_page;
	struct slab* slab = slab_alloc_struct((cache->mm_flags & MM_ATOMIC) | MM_ZONE_NORMAL, sizeof(*slab), &slab_page);
	if (!slab)
		return -ENOMEM;

	int err = slab_init(cache, slab_page, slab);
	if (err) {
		slab_free_struct(slab_page);
		return err;
	}

	list_add(&cache->empty, &slab->link);
	return 0;
}

static void* slab_take(struct slab_cache* cache, struct slab* slab) {
	size_t obj_num = SIZE_MAX;

	/* Simply find a free block within the bitmap */
	for (size_t i = 0; i < cache->obj_count; i++) {
		size_t byte_index = i >> 3;
		unsigned int bit_index = i & 7;

		if (!(slab->free.virtual[byte_index] & (1 << bit_index))) {
			slab->free.virtual[byte_index] |= (1 << bit_index);
			obj_num = i;
			break;
		}
	}

	if (obj_num == SIZE_MAX)
		return NULL;

	/* Now just get the object's pointer and call its constructor */
	void* obj = (u8*)slab->base.virtual + cache->obj_size * obj_num;
	if (cache->ctor)
		cache->ctor(obj);

	slab->in_use++;
	return obj;
}

static struct slab* __slab_find(struct list_head* slabs, unsigned long obj_count, size_t obj_size, void* obj) {
	struct slab* pos;
	list_for_each_entry(pos, slabs, link) {
		void* top = (u8*)pos->base.virtual + obj_count * obj_size;
		if (obj >= pos->base.virtual && obj < top)
			return pos;
	}

	return NULL;
}

/* Find a slab within a cache */
static struct slab* slab_find(struct slab_cache* cache, void* obj) {
	struct slab* slab = __slab_find(&cache->partial, cache->obj_count, cache->obj_size, obj);
	if (slab)
		return slab;
	slab = __slab_find(&cache->full, cache->obj_count, cache->obj_size, obj);
	if (slab)
		return slab;
	return __slab_find(&cache->empty, cache->obj_count, cache->obj_size, obj);
}

static struct slab* slab_release(struct slab_cache* cache, void* obj) {
	struct slab* slab = slab_find(cache, obj);
	if (!slab)
		return NULL; /* Let the caller do what it wants */

	/* Get the object number and then mark it as free */
	size_t obj_num = ((uintptr_t)obj - (uintptr_t)slab->base.virtual) / cache->obj_size;
	size_t byte_index = obj_num >> 3;
	size_t bit_index = obj_num & 7;

	/* Check for a double free here since the caller can't check directly */
	bug((slab->free.virtual[byte_index] & (1 << bit_index)) == 0);
	slab->free.virtual[byte_index] &= ~(1 << bit_index);

	/* Now call the destructor and return the slab the free happened on */
	if (cache->dtor)
		cache->dtor(obj);
	slab->in_use--;
	return slab;
}

static inline void slab_cache_lock(struct slab_cache* cache, unsigned long* irq_flags) {
	if (cache->mm_flags & MM_ATOMIC)
		spinlock_acquire_irq_save(&cache->spinlock, irq_flags);
	else
		mutex_acquire(&cache->mutex);
}

static inline void slab_cache_unlock(struct slab_cache* cache, unsigned long* irq_flags) {
	if (cache->mm_flags & MM_ATOMIC)
		spinlock_release_irq_restore(&cache->spinlock, irq_flags);
	else
		mutex_release(&cache->mutex);
}

void* slab_cache_alloc(struct slab_cache* cache) {
	unsigned long irq_flags;
	slab_cache_lock(cache, &irq_flags);

	/* Allocate from partial slabs first */
	struct list_head* list = NULL;
	if (!list_empty(&cache->partial))
		list = &cache->partial;
	else if (!list_empty(&cache->empty))
		list = &cache->empty;

	void* ret = NULL;
	/* If no slabs are available, try growing the cache */
	if (!list) {
		if (slab_cache_grow(cache) == 0)
			list = &cache->empty;
		else
			goto out;
	}

	struct slab* slab = list_first_entry(list, struct slab, link);
	ret = slab_take(cache, slab);
	bug(ret == NULL); /* If this happens something bad has happened since the cache grew successfuly */

	/* Check to see if the slab is in the appropriate list. If not, move it. */
	if (slab->in_use == 1) {
		list_remove(&slab->link);
		list_add(&cache->partial, &slab->link);
	} else if (slab->in_use == cache->obj_count) {
		list_remove(&slab->link);
		list_add(&cache->full, &slab->link);
	}

out:
	slab_cache_unlock(cache, &irq_flags);
	return ret;
}

void slab_cache_free(struct slab_cache* cache, void* obj) {
	unsigned long irq_flags;
	slab_cache_lock(cache, &irq_flags);

	struct slab* slab = slab_release(cache, obj);
	if (!slab) {
		printk(PRINTK_ERR "mm: slab_release returned NULL, invalid object? obj: %p\n", obj);
		dump_stack();
		goto out;
	}

	/* Make sure the slab is in the appropriate list */
	if (slab->in_use == 0) {
		list_remove(&slab->link);
		list_add(&cache->empty, &slab->link);
	} else if (slab->in_use == cache->obj_count - 1) {
		list_remove(&slab->link);
		list_add(&cache->partial, &slab->link);
	}

out:
	slab_cache_unlock(cache, &irq_flags);
}

static struct slab_cache* self_cache = NULL;

static void slab_cache_init(struct slab_cache* cache, size_t obj_size, size_t align, mm_t mm_flags, void (*ctor)(void*), void (*dtor)(void*)) {
	cache->ctor = ctor;
	cache->dtor = dtor;
	list_head_init(&cache->full);
	list_head_init(&cache->partial);
	list_head_init(&cache->empty);
	cache->obj_size = ROUND_UP(obj_size, align);
	cache->obj_count = cache->obj_size < SLAB_SIZE_CUTOFF ? (PAGE_SIZE * 2) / cache->obj_size : SLAB_AFTER_CUTOFF_OBJ_COUNT;
	cache->align = align;
	cache->mm_flags = mm_flags;
	if (mm_flags & MM_ATOMIC)
		spinlock_init(&cache->spinlock);
	else
		mutex_init(&cache->mutex);
}

struct slab_cache* slab_cache_create(size_t obj_size, size_t align,
		mm_t mm_flags, void (*ctor)(void*), void (*dtor)(void*)) {
	if (obj_size == 0)
		return NULL;
	if (align == 0)
		align = 8;
	else if (align & (align - 1))
		return NULL;

	if (unlikely(!self_cache)) { /* This runs before the scheduler is up */
		struct page* self_cache_page = alloc_pages(MM_ZONE_NORMAL, get_order(sizeof(*self_cache)));
		if (unlikely(!self_cache_page))
			out_of_memory();
		self_cache = page_hhdm_virtual(self_cache_page);
		slab_cache_init(self_cache, sizeof(*self_cache), alignof(typeof(*self_cache)), MM_ZONE_NORMAL, NULL, NULL);
	}

	struct slab_cache* cache = slab_cache_alloc(self_cache);
	if (cache)
		slab_cache_init(cache, obj_size, align, mm_flags, ctor, dtor);

	return cache;
}

static inline bool slab_cache_try_lock(struct slab_cache* cache, unsigned long* irq_flags) {
	if (cache->mm_flags & MM_ATOMIC)
		return spinlock_try_acquire_irq_save(&cache->spinlock, irq_flags);
	return mutex_try_acquire(&cache->mutex);
}

int slab_cache_destroy(struct slab_cache* cache) {
	if (unlikely(cache == self_cache))
		return -EBUSY;

	unsigned long irq_flags;
	if (!slab_cache_try_lock(cache, &irq_flags))
		return -EWOULDBLOCK;
	if (!list_empty(&cache->partial) || !list_empty(&cache->full)) {
		slab_cache_unlock(cache, &irq_flags);
		return -EBUSY;
	}

	struct slab* slab, *tmp;
	list_for_each_entry_safe(slab, tmp, &cache->empty, link) {
		list_remove(&slab->link);
		slab_free_struct(slab->base.page);
		slab_free_struct(slab->free.page);
		slab_free_struct(slab->self_page);
	}

	slab_cache_unlock(cache, &irq_flags);
	slab_cache_free(self_cache, cache);
	return 0;
}
