#include <crater/common.h>
#include <crater/init.h>
#include <crater/printk.h>
#include <crater/panic.h>
#include <crater/string.h>
#include <crater/hashtable.h>
#include <crater/trace.h>
#include <crater/slab.h>
#include <crater/mm.h>
#include <crater/vmm.h>

#define HEAP_CANARY_XOR 0xdecafc0ffeeUL
#define HEAP_ALIGN BIGGEST_ALIGNMENT
#define HEAP_ZERO_SIZE_PTR ((void*)HEAP_ALIGN)

struct alloc_info {
	size_t size;
	bool cache_backing;
	union {
		struct slab_cache* cache;
		struct page* page;
	} backing_un;
} __attribute__((aligned(BIGGEST_ALIGNMENT)));
static_assert((sizeof(struct alloc_info) & (HEAP_ALIGN - 1)) == 0, "struct alloc_info is broken");

#define CACHE_COUNT 27
static struct slab_cache* normal_caches[CACHE_COUNT];
static struct slab_cache* atomic_caches[CACHE_COUNT];

static struct slab_cache* get_cache(size_t size, mm_t mm_flags) {
	struct slab_cache** caches = normal_caches;
	size_t count = ARRAY_SIZE(normal_caches);
	if (mm_flags & MM_ATOMIC) {
		caches = atomic_caches;
		count = ARRAY_SIZE(atomic_caches);
	}

	for (size_t i = 0; i < count; i++) {
		if (mm_flags == caches[i]->mm_flags && size <= caches[i]->obj_size)
			return caches[i];
	}

	return NULL;
}

void* kmalloc(size_t size, mm_t mm_flags) {
	if (size == 0)
		return HEAP_ZERO_SIZE_PTR;
	if (size >= SIZE_MAX - HEAP_ALIGN)
		return NULL;
	size = ROUND_UP(size, HEAP_ALIGN);

	size_t total_size;
	if (__builtin_add_overflow(size, sizeof(struct alloc_info) + sizeof(size_t), &total_size))
		return NULL;

	struct alloc_info* ai = NULL;
	struct slab_cache* cache = get_cache(total_size, mm_flags);
	struct page* page = NULL;
	if (cache)
		ai = slab_cache_alloc(cache);
	if (!ai) {
		page = alloc_pages(mm_flags, get_order(total_size));
		if (!page)
			return NULL;
		cache = NULL;
		ai = page_hhdm_virtual(page);
	}

	ai->size = size;
	ai->cache_backing = !!cache;
	if (ai->cache_backing)
		ai->backing_un.cache = cache;
	else
		ai->backing_un.page = page;

	u8* ret = (u8*)(ai + 1);
	size_t* canary = (size_t*)(ret + size);
	*canary = (uintptr_t)ret ^ HEAP_CANARY_XOR;
	return ret;
}

void kfree(void* ptr) {
	if (ptr == NULL || ptr == HEAP_ZERO_SIZE_PTR)
		return;

	struct alloc_info* ai = (struct alloc_info*)ptr - 1;
	size_t* canary = (size_t*)((u8*)ptr + ai->size);
	bug(*canary != ((uintptr_t)ptr ^ HEAP_CANARY_XOR));

	size_t total_size;
	bug(__builtin_add_overflow(ai->size, sizeof(struct alloc_info) + sizeof(size_t), &total_size) == true);
	if (ai->cache_backing)
		slab_cache_free(ai->backing_un.cache, ai);
	else
		release_page(ai->backing_un.page);
}

void* krealloc(void* old, size_t new_size, mm_t mm_flags) {
	if (old == NULL || old == HEAP_ZERO_SIZE_PTR)
		return kmalloc(new_size, mm_flags);
	if (!new_size) {
		kfree(old);
		return NULL;
	}

	new_size = ROUND_UP(new_size, HEAP_ALIGN);

	struct alloc_info* old_alloc_info = (struct alloc_info*)old - 1;
	size_t old_size = old_alloc_info->size;

	void* new = kmalloc(new_size, mm_flags);
	if (!new)
		return NULL;

	size_t copy_size = old_size < new_size ? old_size : new_size;
	memcpy(new, old, copy_size);

	kfree(old);
	return new;
}

static inline mm_t next_zone(mm_t zone) {
	switch (zone) {
	case MM_ZONE_DMA:
		return MM_ZONE_DMA32;
	case MM_ZONE_DMA32:
		return MM_ZONE_NORMAL;
	default:
		return MM_ZONE_DMA;
	}
}

static bool cache_set_init(struct slab_cache** caches, size_t count, mm_t mm_extra) {
	size_t size = 256;
	mm_t zone = MM_ZONE_DMA;
	for (size_t i = 0; i < count; i++) {
		caches[i] = slab_cache_create(size, HEAP_ALIGN, zone | mm_extra, NULL, NULL);
		if (!caches[i])
			return false;
		zone = next_zone(zone);
		if (zone == MM_ZONE_DMA && i != 0)
			size *= 2;
	}

	return true;
}

static void heap_init(void) {
	if (!cache_set_init(normal_caches, ARRAY_SIZE(normal_caches), 0) ||
			!cache_set_init(atomic_caches, ARRAY_SIZE(atomic_caches), MM_ATOMIC))
		out_of_memory();
}

INIT_TASK_DECLARE(zones_init_task, hhdm_init_task);
INIT_TASK_DEFINE(heap_init_task, INIT_TASK_SCOPE_BSP, heap_init, &zones_init_task, &hhdm_init_task);

#ifdef CONFIG_DEBUG

/* GDB can call these functions */

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__visible void* malloc(size_t size) {
	return kmalloc(size, MM_ZONE_NORMAL);
}

__visible void* realloc(void* old, size_t new_size) {
	return krealloc(old, new_size, MM_ZONE_NORMAL);
}

__visible void* calloc(size_t count, size_t size) {
	return kcalloc(count, size, MM_ZONE_NORMAL);
}

__visible void free(void* ptr) {
	kfree(ptr);
}

__diag_pop();

#endif /* CONFIG_DEBUG */
