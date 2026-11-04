#include <crater/common.h>
#include <crater/init.h>
#include <crater/spinlock.h>
#include <crater/limine.h>
#include <crater/printk.h>
#include <crater/mutex.h>
#include <crater/trace.h>
#include <crater/panic.h>
#include <crater/mm.h>
#include <crater/init.h>
#include <crater/string.h>
#include <crater/errno.h>

#define DMA32_START 0x1000000
#define DMA32_END 0x100000000
#define NORMAL_START 0x100000000
#define NORMAL_END PHYSADDR_MAX

/* Must be volatile, so that way the null check doesn't get optimized away */
static volatile struct limine_mmap_request __limine_request mmap_request = {
	.request.id = LIMINE_MMAP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

static struct {
	struct limine_mmap_entry entries[1024];
	size_t entry_count;
} unsanitized_mmap = { 0 };

static int mmap_add_entry_unsanitized(const struct limine_mmap_entry* entry) {
	if (unsanitized_mmap.entry_count >= ARRAY_SIZE(unsanitized_mmap.entries))
		return -ENOSPC;
	unsanitized_mmap.entries[unsanitized_mmap.entry_count++] = *entry;
	return 0;
}

static struct {
	struct limine_mmap_entry entries[1024]; /* Around 6 pages */
	size_t entry_count;
} sanitized_mmap = { 0 };

/*
 * This function may make it look like there is a bug in mmap_sanitize() when merging memory map
 * entries. No, different types can map to the same string as you can clearly see.
 */
static const char* mmap_entry_type_to_string(enum limine_mmap_type type) {
	switch (type) {
	case LIMINE_MMAP_USABLE:
	case LIMINE_MMAP_BOOTLOADER_RECLAIMABLE:
	case LIMINE_MMAP_EXECUTABLE_AND_MODULES:
		return "System RAM";
	case LIMINE_MMAP_ACPI_RECLAIMABLE:
		return "ACPI Reclaimable";
	case LIMINE_MMAP_RESERVED:
	case LIMINE_MMAP_RESERVED_MAPPED:
	case LIMINE_MMAP_FRAMEBUFFER:
		return "Reserved";
	case LIMINE_MMAP_ACPI_NVS:
		return "ACPI Non-volatile Storage";
	case LIMINE_MMAP_BAD_MEMORY:
		return "Bad Memory";
	}
	return "Unknown";
}

static inline bool mmap_entry_usable(const struct limine_mmap_entry* entry) {
	return (entry->type == LIMINE_MMAP_USABLE || entry->type == LIMINE_MMAP_BOOTLOADER_RECLAIMABLE);
}

static inline bool mmap_entry_is_ram(const struct limine_mmap_entry* entry) {
	switch (entry->type) {
	case LIMINE_MMAP_USABLE:
	case LIMINE_MMAP_BOOTLOADER_RECLAIMABLE:
	case LIMINE_MMAP_EXECUTABLE_AND_MODULES:
	case LIMINE_MMAP_ACPI_RECLAIMABLE:
	case LIMINE_MMAP_RESERVED_MAPPED:
		return true;
	default:
		return false;
	}
}

static inline bool mmap_entry_usable_strict(const struct limine_mmap_entry* entry) {
	return entry->type == LIMINE_MMAP_USABLE;
}

#define MMAP_TYPE_BIT(t) (1u << (t))

/**
 * Limine already sanitizes the firmware memory map, but this function does a few more things that
 * limine doesn't do:
 *
 * - First page of memory is ALWAYS reserved (some base revisions do this, some do not)
 * - ALL memory map entries are aligned by 4K, not just usable/bootloader reclaimable ones
 * - Merge entries of the same type when touching
 */
static void mmap_sanitize(void) {
	sanitized_mmap.entries[0] = (struct limine_mmap_entry){ .base = 0, .length = PAGE_SIZE, .type = LIMINE_MMAP_RESERVED };
	size_t sanitized_n = 1;

	/* First, align every entry by a page, usable entries shrink inward, unusable entries grow outward */
	for (u64 i = 0; i < unsanitized_mmap.entry_count && sanitized_n < ARRAY_SIZE(sanitized_mmap.entries); i++) {
		struct limine_mmap_entry entry = unsanitized_mmap.entries[i];
		physaddr_t base = entry.base;
		physaddr_t end;
		if (unlikely(__builtin_add_overflow(entry.base, entry.length, &end) || end >= PHYSADDR_MAX - PAGE_SIZE))
			end = ROUND_DOWN(PHYSADDR_MAX, PAGE_SIZE);

		/* Limine guaruntees that usable regions are 4K aligned, but it's done again anyway since it's an easy thing to do */
		if (mmap_entry_usable(&entry)) {
			base = ROUND_UP(base, PAGE_SIZE);
			end = ROUND_DOWN(end, PAGE_SIZE);
		} else {
			base = ROUND_DOWN(base, PAGE_SIZE);
			end = ROUND_UP(end, PAGE_SIZE);
		}
		if (unlikely(end <= base)) {
			printk(PRINTK_WARN "mm: Dropping unrepresentable entry at %#lx (length %zu, type %s)\n",
					entry.base, entry.length, mmap_entry_type_to_string(entry.type));
			continue;
		}

		entry.base = base;
		entry.length = end - base;
		sanitized_mmap.entries[sanitized_n++] = entry;
	}

	/* This is the "rule table" on how to handle overlaps. Ordering doesn't matter a whole lot, but it can affect how much memory gets reported */
	static const struct {
		unsigned int victim, winner;
	} clip_passes[] = {
		{
			.victim = MMAP_TYPE_BIT(LIMINE_MMAP_USABLE) | MMAP_TYPE_BIT(LIMINE_MMAP_BOOTLOADER_RECLAIMABLE),
			.winner = ~(MMAP_TYPE_BIT(LIMINE_MMAP_USABLE) | MMAP_TYPE_BIT(LIMINE_MMAP_BOOTLOADER_RECLAIMABLE)) /* Everything wins except these two */
		},
		{
			.victim = MMAP_TYPE_BIT(LIMINE_MMAP_USABLE),
			.winner = MMAP_TYPE_BIT(LIMINE_MMAP_BOOTLOADER_RECLAIMABLE) /* Bootloader reclaimable wins over usable */
		}
	};

	/* Now handle overlaps */
	for (size_t pass = 0; pass < ARRAY_SIZE(clip_passes); pass++) {
		for (size_t i = 0; i < sanitized_n; i++) {
			struct limine_mmap_entry* entry = &sanitized_mmap.entries[i];
			if (!(MMAP_TYPE_BIT(entry->type) & clip_passes[pass].victim) || entry->length == 0)
				continue;

			physaddr_t entry_base = entry->base;
			physaddr_t entry_end = entry_base + entry->length;

			for (size_t j = 0; j < sanitized_n && entry_base < entry_end; j++) {
				struct limine_mmap_entry* against = &sanitized_mmap.entries[j];
				physaddr_t against_end;
				if (j == i || !(MMAP_TYPE_BIT(against->type) & clip_passes[pass].winner) || against->length == 0)
					continue;

				against_end = against->base + against->length;
				if (against_end <= entry_base || against->base >= entry_end)
					continue;

				/* Handle front overlap, tail overlap, and the entry being right in the middle */
				if (against->base <= entry_base) {
					entry_base = (against_end < entry_end) ? against_end : entry_end;
				} else if (against_end >= entry_end) {
					entry_end = against->base;
				} else {
					if (likely(sanitized_n < ARRAY_SIZE(sanitized_mmap.entries))) {
						sanitized_mmap.entries[sanitized_n++] = (struct limine_mmap_entry){
							.base = against_end, .length = entry_end - against_end, .type = entry->type
						};
					} else {
						printk(PRINTK_WARN "mm: Memory map full, dropping region [%#lx-%#lx)\n", against_end, entry_end);
					}
					entry_end = against->base;
				}
			}

			entry->base = entry_base;
			entry->length = entry_end > entry_base ? entry_end - entry_base : 0;
		}
	}

	/* Now sort the entries */
	for (size_t i = 1; i < sanitized_n; i++) {
		struct limine_mmap_entry key = sanitized_mmap.entries[i];
		size_t j = i;
		while (j > 0 && sanitized_mmap.entries[j - 1].base > key.base) {
			sanitized_mmap.entries[j] = sanitized_mmap.entries[j - 1];
			j--;
		}
		sanitized_mmap.entries[j] = key;
	}

	/* Now drop empty entries and merge any touching/overlapping same type */
	size_t entry_count = 0;
	for (size_t i = 0; i < sanitized_n; i++) {
		struct limine_mmap_entry* curr = &sanitized_mmap.entries[i];
		if (curr->length == 0)
			continue;
		if (entry_count > 0) {
			struct limine_mmap_entry* prev = &sanitized_mmap.entries[entry_count - 1];
			physaddr_t prev_end = prev->base + prev->length;
			if (prev->type == curr->type && curr->base <= prev_end) {
				physaddr_t curr_end = curr->base + curr->length;
				if (curr_end > prev_end)
					prev->length = curr_end - prev->base;
				continue;
			}
		}
		sanitized_mmap.entries[entry_count++] = *curr;
	}

	sanitized_mmap.entry_count = entry_count;
}

/* Check if an address + size is within a memory map entry */
static bool mmap_entry_check(const struct limine_mmap_entry* entry, physaddr_t base, size_t size) {
	if (size == 0)
		return false;
	physaddr_t base_top;
	if (__builtin_add_overflow(base, size, &base_top))
		return false;
	physaddr_t entry_top;
	if (unlikely(__builtin_add_overflow(entry->base, entry->length, &entry_top)))
		return false;

	return (base >= entry->base && base_top <= entry_top);
}

/* Get a memory map entry from a single page */
static const struct limine_mmap_entry* mmap_get_entry_from_page(physaddr_t page) {
	struct limine_mmap_entry* ret = NULL;
	for (size_t i = 0; i < sanitized_mmap.entry_count; i++) {
		if (mmap_entry_check(&sanitized_mmap.entries[i], page, PAGE_SIZE)) {
			ret = &sanitized_mmap.entries[i];
			break;
		}
	}
	return ret;
}

/* Check if a memory region is free or not */
static bool mmap_region_is_usable_strict(physaddr_t base, size_t size) {
	if (size == 0)
		return false;
	physaddr_t end;
	if (__builtin_add_overflow(base, size, &end))
		return false;

	if (end >= PHYSADDR_MAX - PAGE_SIZE)
		return false;
	end = ROUND_UP(end, PAGE_SIZE);
	base = ROUND_DOWN(base, PAGE_SIZE);

	const struct limine_mmap_entry* entry = mmap_get_entry_from_page(base);
	return (entry && mmap_entry_usable_strict(entry)) ? mmap_entry_check(entry, base, end - base) : false;
}

/* Get the very last free address according to the memory map */
static physaddr_t mmap_get_last_ram_address_inclusive(void) {
	physaddr_t ret = 0;
	for (u64 i = 0; i < sanitized_mmap.entry_count; i++) {
		struct limine_mmap_entry* entry = &sanitized_mmap.entries[i];
		if (mmap_entry_is_ram(entry))
			ret = entry->base + entry->length - 1;
	}
	return ret;
}

/* Return a free address from the memory map, but with a minimum address */
static physaddr_t mmap_get_free_address_from_size_min_strict(physaddr_t min, size_t size) {
	for (size_t i = 0; i < sanitized_mmap.entry_count; i++) {
		struct limine_mmap_entry* entry = &sanitized_mmap.entries[i];
		if (!mmap_entry_usable_strict(entry))
			continue;

		physaddr_t base = entry->base;
		physaddr_t end = entry->base + entry->length;
		if (base < min)
			base = min;
		base = ROUND_UP(base, PAGE_SIZE);
		if (base < end && end - base >= size)
			return base;
	}
	return 0;
}

/* Allocate space from the memory map, not safe to use after zone initialization */
static physaddr_t mmap_alloc(size_t size) {
	if (size >= SIZE_MAX - PAGE_SIZE)
		return 0;
	size = ROUND_UP(size, PAGE_SIZE);

	physaddr_t ret = mmap_get_free_address_from_size_min_strict(NORMAL_START, size);
	if (ret == 0) {
		ret = mmap_get_free_address_from_size_min_strict(DMA32_START, size);
		if (unlikely(ret == 0))
			ret = mmap_get_free_address_from_size_min_strict(PAGE_SIZE, size);
	}
	if (ret) {
		struct limine_mmap_entry entry = { .base = ret, .length = size, .type = LIMINE_MMAP_EXECUTABLE_AND_MODULES };
		int err = mmap_add_entry_unsanitized(&entry);
		if (unlikely(err)) {
			printk(PRINTK_CRIT "mm: %s() failed to add memory map entry entry, err: %d\n", __func__, err);
			return 0;
		}
		mmap_sanitize();
	}

	return ret;
}

/* Return the number of bytes of usable memory, size returned is always a multiple of a page size */
static u64 mmap_total_usable(void) {
	u64 ret = 0;
	for (u64 i = 0; i < sanitized_mmap.entry_count; i++) {
		struct limine_mmap_entry* entry = &sanitized_mmap.entries[i];
		if (mmap_entry_usable(entry))
			ret += entry->length;
	}
	return ret;
}

struct mem_area {
	physaddr_t base; /* Start of the memory area */
	u64 size; /* The size of the area rounded to a power of two */
	u64 real_size; /* The actual size of the area, usually the same as size */
	unsigned long __atomic free_blocks[MAX_ORDER + 1]; /* Amount of free blocks on every layer */
	unsigned long total_blocks; /* Number of blocks the last layer */
	unsigned int layer_count; /* Usually MAX_ORDER + 1 */
	struct {
		unsigned long* free_list;
		bool atomic; /* Can allocations from this area sleep? */
		union {
			spinlock_t spinlock;
			mutex_t mutex;
		};
	} pages; /* For managing the actual memory in the list */
	unsigned int __atomic alloc_refcnt; /* How many threads are allocating from this area */
};

static inline void mem_area_lock(struct mem_area* area, unsigned long* irq_flags) {
	if (area->pages.atomic)
		spinlock_acquire_irq_save(&area->pages.spinlock, irq_flags);
	else
		mutex_acquire(&area->pages.mutex);
}

static inline void mem_area_unlock(struct mem_area* area, unsigned long* irq_flags) {
	if (area->pages.atomic)
		spinlock_release_irq_restore(&area->pages.spinlock, irq_flags);
	else
		mutex_release(&area->pages.mutex);
}

static unsigned long find_first_free(unsigned long* free_list, unsigned int layer) {
	u8* free_list8 = (u8*)free_list;

	unsigned long block_count = 1ul << layer;
	unsigned long block = 0;

	const unsigned long ulong_bits = sizeof(unsigned long) * 8;

	/* First, test 1 bit at a time until alignment */
	while (block < block_count && (block_count + block - 1) % ulong_bits) {
		size_t byte_index = (block_count + block - 1) >> 3;
		unsigned int bit_index = (block_count + block - 1) & 7;

		if (!(free_list8[byte_index] & (1ul << bit_index)))
			return block;

		block++;
	}

	/* Now test several bits at a time */
	size_t ulong_index = ((block_count + block - 1) >> 3) / sizeof(unsigned long);
	free_list = free_list + ulong_index;
	while (block + ulong_bits < block_count) {
		if (*free_list != ULONG_MAX)
			return block + __builtin_ctzl(~(*free_list));

		block += ulong_bits;
		free_list++;
	}

	/* Now test the rest of the bits 1 at a time */
	while (block < block_count) {
		size_t byte_index = (block_count + block - 1) >> 3;
		unsigned int bit_index = (block_count + block - 1) & 7;

		if ((free_list8[byte_index] & (1ul << bit_index)) == 0)
			return block;

		block++;
	}

	return ULONG_MAX;
}

static inline void __alloc_block(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) >> 3;
	unsigned int bit_index = (block_count + block - 1) & 7;
	((u8*)free_list)[byte_index] |= (1 << bit_index);
}

static inline void __free_block(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) >> 3;
	unsigned int bit_index = (block_count + block - 1) & 7;
	((u8*)free_list)[byte_index] &= ~(1 << bit_index);
}

static inline bool __is_block_free(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) >> 3;
	unsigned int bit_index = (block_count + block - 1) & 7;
	return ((((u8*)free_list)[byte_index] & (1 << bit_index)) == 0);
}

/* Allocate blocks, but also manages the other blocks corresponding to the block on other layers */
static int _alloc_block(struct mem_area* area, unsigned int layer, unsigned long block) {
	if (layer >= area->layer_count)
		return -EINVAL;

	unsigned long block_count = 1ul << layer;
	if (block >= block_count)
		return -EFAULT;
	if (!__is_block_free(area->pages.free_list, block_count, block))
		return -EALREADY;

	__alloc_block(area->pages.free_list, block_count, block);
	atomic_sub_fetch_explicit(&area->free_blocks[layer], 1, memory_order_relaxed);

	unsigned long tmp = block;
	unsigned int tmp2 = layer;

	/* Allocate the bigger blocks */
	while (layer--) {
		block_count = 1ul << layer;
		block >>= 1;
		if (__is_block_free(area->pages.free_list, block_count, block)) {
			__alloc_block(area->pages.free_list, block_count, block);
			atomic_sub_fetch_explicit(&area->free_blocks[layer], 1, memory_order_relaxed);
		}
	}

	block = tmp;
	layer = tmp2;

	/* Allocate the smaller blocks below the layer we allocated on */
	unsigned long count = 2;
	while (++layer < area->layer_count) {
		block_count = 1ul << layer;
		block <<= 1;
		for (unsigned long i = 0; i < count; i++) {
			bug(!__is_block_free(area->pages.free_list, block_count, block + i));
			__alloc_block(area->pages.free_list, block_count, block + i);
		}
		atomic_sub_fetch_explicit(&area->free_blocks[layer], count, memory_order_relaxed);
		count <<= 1;
	}

	return 0;
}

/* Frees blocks, but also manages the other blocks corresponding to the block on other layers */
static int _free_block(struct mem_area* area, unsigned int layer, unsigned long block) {
	if (layer >= area->layer_count)
		return -EINVAL;

	unsigned long block_count = 1ul << layer;
	if (block >= block_count)
		return -EFAULT;
	if (__is_block_free(area->pages.free_list, block_count, block))
		return -EALREADY;

	unsigned long tmp = block;
	unsigned int tmp2 = layer;

	while (1) {
		/* First free the current block */
		__free_block(area->pages.free_list, block_count, block);
		atomic_add_fetch_explicit(&area->free_blocks[layer], 1, memory_order_relaxed);

		/* Layer 0 has no buddy */
		if (layer == 0)
			break;

		/* 
		 * Now check the buddy, if it's free, then the two blocks can be merged,
		 * otherwise this part of the free process is done
		 */
		unsigned long buddy = block & 1 ? block - 1 : block + 1;
		if (!__is_block_free(area->pages.free_list, block_count, buddy))
			break;

		/* Now just move on to the next layer */
		layer--;
		block >>= 1;
		block_count = 1ul << layer;
	}

	block = tmp;
	layer = tmp2;

	/* Now finish by freeing the smaller blocks below the original layer we freed on */
	unsigned long count = 2;
	while (++layer < area->layer_count) {
		block_count = 1ul << layer;
		block <<= 1;
		for (unsigned long i = 0; i < count; i++) {
			bug(__is_block_free(area->pages.free_list, block_count, block + i));
			__free_block(area->pages.free_list, block_count, block + i);
		}
		atomic_add_fetch_explicit(&area->free_blocks[layer], count, memory_order_relaxed);
		count <<= 1;
	}

	return 0;
}

struct zone {
	mm_t zone_type; /* Has only 1 flag, either MM_ZONE_DMA, MM_ZONE_DMA32, or MM_ZONE_NORMAL */
	unsigned long area_count; /* The number of areas the zone has */
	struct mem_area* areas; /* The array of memory areas, in order by area->base */
};

/*
 * Selects a memory area to allocate from
 *
 * This function will allocate the block of memory, and store the information about that block
 * into layer and block. The atomic parameter will determine whether or not the allocation can sleep,
 * and will always return an area that is marked as atomic. The IRQ flags are used for locking if the
 * allocation cannot sleep.
 *
 * Acquires area->lock, and increments area->refcnt
 */
static struct mem_area* select_mem_area(struct zone* zone, unsigned int order,
		unsigned int* layer, unsigned long* block, bool atomic, unsigned long* irq_flags) {
	const int max_retries = 3;
	int retries = 0;
	unsigned long i = 0;
	while (retries < max_retries) {
		struct mem_area* best = NULL;
		unsigned int best_layer = 0;
		unsigned int best_index = 0;
		for (; i < zone->area_count; i++) {
			struct mem_area* a = &zone->areas[i];
			if (a->pages.atomic != atomic)
				continue;
			if (unlikely(a->layer_count <= order))
				continue;

			const unsigned int l = a->layer_count - order - 1;
			if (!atomic_load_explicit(&a->free_blocks[l], memory_order_relaxed))
				continue;

			const unsigned int crefs = atomic_load(&a->alloc_refcnt);
			const bool take_now = (crefs == 0 || retries > 0);
			if (take_now || !best || crefs < atomic_load(&best->alloc_refcnt)) {
				best = a;
				best_layer = l;
				best_index = i;
				if (take_now)
					break;
			}
		}

		if (unlikely(!best)) {
			i = 0;
			retries++;
			continue;
		}

		atomic_add_fetch(&best->alloc_refcnt, 1);
		mem_area_lock(best, irq_flags);
		if (atomic_load_explicit(&best->free_blocks[best_layer], memory_order_relaxed)) {
			*layer = best_layer;
			*block = find_first_free(best->pages.free_list, best_layer);
			bug(*block == ULONG_MAX); /* This means the accounting logic is fucked up if this triggers */
			return best;
		}
		mem_area_unlock(best, irq_flags);
		atomic_sub_fetch(&best->alloc_refcnt, 1);

		i = best_index + 1;
	}

	return NULL;
}

/* Get a memory area based on an address. */
static struct mem_area* get_mem_area(struct zone* zone, physaddr_t addr) {
	unsigned long low = 0;
	unsigned long high = zone->area_count;

	while (low < high) {
		unsigned long mid = low + ((high - low) >> 1);
		struct mem_area* area = &zone->areas[mid];
		physaddr_t area_end = area->base + (area->total_blocks << PAGE_SHIFT);
		if (addr < area->base)
			high = mid;
		else if (addr >= area_end)
			low = mid + 1;
		else
			return area;
	}

	return NULL;
}

/*
 * Allocate pages from a memory zone, this function makes sure the memory
 * region is actually system RAM before returning the address.
 */
static physaddr_t __alloc_pages(struct zone* zone, bool atomic, unsigned int order) {
	size_t alloc_size = PAGE_SIZE << order;

	physaddr_t ret = 0;
	int err;

	unsigned int layer;
	unsigned long block;
	unsigned long irq_flags;

	struct mem_area* area = select_mem_area(zone, order, &layer, &block, atomic, &irq_flags);
	if (!area)
		goto out;

retry:
	ret = 0;

	/* 
	 * On the first attempt, select_mem_area will select a block for us. If we need a retry, 
	 * block is set to ULONG_MAX so that way we know to actually retry.
	 */
	if (block == ULONG_MAX) {
		block = find_first_free(area->pages.free_list, layer);
		if (block == ULONG_MAX) {
			mem_area_unlock(area, &irq_flags);
			atomic_sub_fetch(&area->alloc_refcnt, 1);
			area = select_mem_area(zone, order, &layer, &block, atomic, &irq_flags);
			if (!area)
				goto out;
		}
	}

	err = _alloc_block(area, layer, block);
	if (err) {
		printk(PRINTK_ERR "mm: _alloc_block failed in %s with error code %i\n", __func__, err);
		goto out;
	}

	ret = area->base + (block * alloc_size);

	/* Since the blocks that could be outside of the area are allocated, this should not happen */
	if (unlikely(ret + alloc_size > area->base + area->real_size)) {
		printk(PRINTK_ERR "mm: Tried allocating a block outside of area!\n");
		_free_block(area, layer, block);
		ret = 0;
		goto out;
	}

	/*
	 * Now make sure this region is actually marked usable in the memory map.
	 * This path is mostly hit before the reserved memory is marked as allocated,
	 * but it can still happen if there are holes in the memory map.
	 */
	if (unlikely(!mmap_region_is_usable_strict(ret, alloc_size))) {
		/* Go through each page, and see what usable pages there are, and free those blocks */
		for (size_t i = 0; i < alloc_size; i += PAGE_SIZE) {
			if (mmap_region_is_usable_strict(ret + i, PAGE_SIZE)) {
				block = ((ret + i) - area->base) >> PAGE_SHIFT;
				_free_block(area, area->layer_count - 1, block);
			}
		}

		block = ULONG_MAX;
		goto retry;
	}
out:
	if (area) {
		mem_area_unlock(area, &irq_flags);
		atomic_sub_fetch(&area->alloc_refcnt, 1);
	}
	return ret;
}

/*
 * Horribly inefficient, but i just want this to work. This shouldn't really run outside of really boot,
 * but in case it does for some reason, do locking
 */
static int __reserve_pages(struct zone* zone, physaddr_t addr, size_t size) {
	if (unlikely(size >= SIZE_MAX - PAGE_SIZE))
		return -ERANGE;

	size = ROUND_UP(size, PAGE_SIZE);
	for (size = ROUND_UP(size, PAGE_SIZE); size; addr += PAGE_SIZE, size -= PAGE_SIZE) {
		struct mem_area* area = get_mem_area(zone, addr);
		if (unlikely(!area))
			continue;

		unsigned long irq_flags;
		const unsigned int layer = area->layer_count - 1;
		unsigned long block = (addr - area->base) >> PAGE_SHIFT;

		mem_area_lock(area, &irq_flags);
		int err = _alloc_block(area, layer, block);
		mem_area_unlock(area, &irq_flags);
		if (err && err != -EALREADY) /* Can happen for things like overlapping reserved regions */
			printk(PRINTK_WARN "mm: %#16lx error %i\n", addr, err);
	}

	return 0;
}

/* Free pages from a specific memory zone. */
static int __free_pages(struct zone* zone, physaddr_t addr, unsigned int order) {
	struct mem_area* area = get_mem_area(zone, addr);
	if (!area)
		return -EFAULT;

	unsigned int layer = area->layer_count - order - 1;
	size_t alloc_size = PAGE_SIZE << order;
	unsigned long block = (addr - area->base) / alloc_size;

	unsigned long irq_flags;
	mem_area_lock(area, &irq_flags);
	int ret = _free_block(area, layer, block);
	mem_area_unlock(area, &irq_flags);
	return ret;
}

#define DMA_SIZE 0x1000000
#define DMA_AREA_COUNT ((DMA_SIZE >> MAX_ORDER) >> PAGE_SHIFT)
#define MAX_AREA_BMP_SIZE (((1 << (MAX_ORDER + 1)) >> 3) + 1)

/* Sanity check */
#if (DMA_SIZE >> MAX_ORDER) < PAGE_SIZE
#error "MAX order too large for DMA_SIZE"
#endif /* (DMA_SIZE >> MAX_ORDER) < PAGE_SIZE */

static struct mem_area dma_areas[DMA_AREA_COUNT];
static unsigned long dma_area_free_lists[DMA_AREA_COUNT][MAX_AREA_BMP_SIZE / sizeof(unsigned long)] = { 0 };
static struct zone dma_zone;

/* 
 * __dma32_zone and __normal_zone are not guarunteed to be used, The dma32_zone and 
 * normal_zone pointers can be linked with other zones if there is not enough space for them.
 */
static struct zone __dma32_zone;
static struct zone __normal_zone;
static struct zone* dma32_zone;
static struct zone* normal_zone;

/* 
 * Get a memory zone from MM flags, this function must select the most restrictive
 * zone if multiple zones are set for whatever reason. Returns the normal zone if
 * no flags are set.
 */
static inline struct zone* get_zone_mm(mm_t mm_flags) {
	if (mm_flags & MM_ZONE_DMA)
		return &dma_zone;
	if (mm_flags & MM_ZONE_DMA32)
		return dma32_zone;
	return normal_zone;
}

/*
 * Checks if an address range is in a memory zone, this function does assume
 * that all memory areas are ordered (as they should).
 */
static inline bool in_zone(struct zone* zone, physaddr_t base, physaddr_t top) {
	struct mem_area* first_area = &zone->areas[0];
	struct mem_area* last_area = &zone->areas[zone->area_count - 1];
	return (base >= first_area->base && top <= last_area->base + last_area->real_size);
}

/*
 * Gets a memory zone from a physical memory address, this function
 * returns NULL if the address range falls inside of two different zones
 */
static struct zone* get_zone_addr(physaddr_t addr, size_t size) {
	physaddr_t addr_top;
	if (__builtin_add_overflow(addr, size, &addr_top))
		return NULL;

	if (in_zone(&dma_zone, addr, addr_top))
		return &dma_zone;
	if (in_zone(dma32_zone, addr, addr_top))
		return dma32_zone;
	if (in_zone(normal_zone, addr, addr_top))
		return normal_zone;

	return NULL;
}

static inline struct zone* fallback_zone(struct zone* zone) {
	switch (zone->zone_type) {
	case MM_ZONE_NORMAL:
		return dma32_zone;
	case MM_ZONE_DMA32:
		return &dma_zone;
	case MM_ZONE_DMA:
		return NULL;
	default:
		bug("Invalid zone type");
	}
}

static u64 __atomic pages_in_use = atomic_init(0);
static u64 mem_total = 0;

void mm_get_free_pages(size_t* total_page_count, size_t* free_page_count) {
	*total_page_count = mem_total >> PAGE_SHIFT;
	*free_page_count = (mem_total >> PAGE_SHIFT) - atomic_load(&pages_in_use);
}

void out_of_memory(void) {
	panic("Out of memory");
}

/* Yes... this is the same as out_of_memory(), this will change at some point */
static void out_of_memory_atomic(void) {
	panic("Out of memory");
}

static physaddr_t _alloc_pages(mm_t mm_flags, unsigned int order) {
	if (order > MAX_ORDER)
		return 0;

	struct zone* zone = get_zone_mm(mm_flags);

	const bool atomic = !!(mm_flags & MM_ATOMIC);
	const unsigned int max_retries = atomic ? 0 : 3;
	unsigned int retries = max_retries;

	physaddr_t ret = 0;
	while (1) {
		ret = __alloc_pages(zone, atomic, order);
		if (ret) {
			atomic_add_fetch(&pages_in_use, 1ul << order);
			break;
		}

		struct zone* fallback = fallback_zone(zone);
		if (fallback) {
			zone = fallback;
		} else if (mm_flags & MM_NOFAIL) {
			if (atomic)
				out_of_memory_atomic();
			else
				out_of_memory();
		} else if (retries-- == 0) {
			break;
		} else {
			zone = get_zone_mm(mm_flags);
		}
	}

	return ret;
}

static void _free_pages(physaddr_t addr, unsigned int order) {
	int err = -EINVAL;
	if (order <= MAX_ORDER && addr % PAGE_SIZE == 0 && addr >= PAGE_SIZE) {
		size_t alloc_size = PAGE_SIZE << order;
		struct zone* zone = get_zone_addr(addr, alloc_size);
		err = (zone) ? __free_pages(zone, addr, order) : -EFAULT;
	}

	if (err == 0) {
		atomic_sub_fetch(&pages_in_use, 1ul << order);
	} else {
		dump_stack();
		printk(PRINTK_ERR "mm: %s(%#lx, %u) failed: %d\n", __func__, addr, order, err);
	}
}

static unsigned int get_layer_count(size_t size) {
	unsigned int layers = 1;
	while (layers <= MAX_ORDER) {
		size_t block_size = size >> (layers - 1);
		if ((block_size / 2) < PAGE_SIZE)
			break;
		layers++;
	}
	return layers;
}

static void alloc_rounded_area(struct mem_area* area) {
	unsigned long start_block = area->real_size >> PAGE_SHIFT;
	unsigned long end_block = area->size >> PAGE_SHIFT;
	for (unsigned long block = start_block; block < end_block; block++)
		bug(_alloc_block(area, area->layer_count - 1, block) != 0);
}

/* All structures for this zone are statically allocated */
static void dma_zone_init(physaddr_t last_usable) {
	dma_zone.zone_type = MM_ZONE_DMA;
	dma_zone.areas = dma_areas;

	size_t rest = unlikely(last_usable < DMA_SIZE) ? last_usable : DMA_SIZE;
	for (dma_zone.area_count = 0; dma_zone.area_count < DMA_AREA_COUNT && rest; dma_zone.area_count++) {
		struct mem_area* area = &dma_areas[dma_zone.area_count];

		const size_t max_area_size = (1u << MAX_ORDER) * PAGE_SIZE;
		area->base = dma_zone.area_count * max_area_size;
		area->pages.free_list = dma_area_free_lists[dma_zone.area_count];
		area->size = max_area_size;
		area->real_size = rest < max_area_size ? rest : max_area_size;
		area->layer_count = get_layer_count(max_area_size);
		area->total_blocks = 1u << (area->layer_count - 1);
		atomic_store_explicit(&area->alloc_refcnt, 0, memory_order_relaxed);
		for (unsigned int layer = 0; layer < area->layer_count; layer++)
			atomic_store_explicit(&area->free_blocks[layer], 1ul << layer, memory_order_relaxed);

		const unsigned long atomic_count = 1;
		area->pages.atomic = dma_zone.area_count < atomic_count;
		if (area->pages.atomic)
			spinlock_init(&area->pages.spinlock);
		else
			mutex_init(&area->pages.mutex);

		if (area->size != area->real_size)
			alloc_rounded_area(area);

		rest -= area->real_size;
	}

	/* Allocate the first page of memory, an error should never happen in this context */
	bug(_alloc_block(&dma_areas[0], dma_areas[0].layer_count - 1, 0) != 0);
}

/* 
 * Initialize a memory area, free_list_zone is the zone the free list should be allocated from,
 * so this function can be used when not every zone is initialized yet.
 */
static int init_area(struct mem_area* area, physaddr_t base, size_t real_size, bool atomic, struct zone* alloc_zone) {
	u64 rounded_size = roundup_pow2_minimum(PAGE_SIZE, real_size);
	bug(rounded_size == 0);

	unsigned int layer_count = get_layer_count(rounded_size);
	if (layer_count == 1)
		return -ELOOP;

	size_t free_list_size = ((1 << layer_count) >> 3) + 1;
	unsigned int order = get_order(free_list_size);
	physaddr_t free_list = __alloc_pages(alloc_zone, false, order);
	if (!free_list) {
		free_list = __alloc_pages(alloc_zone, true, order);
		if (!free_list)
			return -ENOMEM;
	}

	area->base = base;
	area->real_size = real_size;
	area->size = rounded_size;
	area->layer_count = layer_count;
	area->total_blocks = 1 << (layer_count - 1);
	for (unsigned long layer = 0; layer < area->layer_count; layer++)
		atomic_store_explicit(&area->free_blocks[layer], 1ul << layer, memory_order_relaxed);
	area->pages.free_list = hhdm_virtual(free_list);
	area->pages.atomic = atomic;
	if (atomic)
		spinlock_init(&area->pages.spinlock);
	else
		mutex_init(&area->pages.mutex);
	memset(area->pages.free_list, 0, free_list_size);
	atomic_store_explicit(&area->alloc_refcnt, 0, memory_order_relaxed);

	if (rounded_size != real_size)
		alloc_rounded_area(area);
	return 0;
}

/* Initialize a memory zone, alloc_zone is the zone to allocate the memory structures from */
static int zone_init(struct zone* zone, mm_t zone_type,
		physaddr_t last_usable, physaddr_t min_start, physaddr_t max_end,
		struct zone* alloc_zone) {
	if (last_usable < min_start)
		return -ELOOP;
	if (last_usable < max_end)
		max_end = last_usable;

	struct zone* const original_alloc_zone = alloc_zone;

	/* First allocate the array of areas for the zone */
	size_t zone_size = max_end - min_start;
	const size_t max_area_size = PAGE_SIZE << (MAX_ORDER);
	unsigned long area_count = zone_size / max_area_size;
	if (unlikely(area_count == 0))
		area_count = 1;
	unsigned long atomic_count = (area_count * 5 + 99) / 100;
	if (atomic_count == 0)
		atomic_count = 1;
	unsigned int area_order = get_order(sizeof(struct mem_area) * area_count);
	physaddr_t _areas = __alloc_pages(original_alloc_zone, false, area_order);
	if (unlikely(!_areas)) {
		_areas = __alloc_pages(original_alloc_zone, true, area_order);
		if (unlikely(!_areas))
			return -ENOMEM;
	}

	struct mem_area* areas = hhdm_virtual(_areas);
	size_t rest = zone_size;
	zone->zone_type = zone_type;
	zone->areas = areas;
	for (zone->area_count = 0; zone->area_count < area_count; zone->area_count++) {
		size_t area_size = rest > max_area_size ? max_area_size : rest;
		int err = init_area(&areas[zone->area_count], min_start, area_size, zone->area_count < atomic_count, alloc_zone);

		if (likely(err == 0)) {
			min_start += area_size;
			rest -= area_size;
			alloc_zone = zone; /* At least one area, so an attempt can be made at using the current zone */
			continue;
		} else if (unlikely(err == -ELOOP)) {
			if (unlikely(--area_count == 0)) /* Don't bother with the zone */
				__free_pages(original_alloc_zone, _areas, area_order);
			else
				break; /* Able to create at least one zone, so it's still usable */
		} else if (err == -ENOMEM && zone == alloc_zone) {
			alloc_zone = original_alloc_zone; /* Swap back to the original zone, and try the allocation there */
			zone->area_count--;
			continue;
		}

		return err;
	}

	return 0;
}

static inline void reserve_pages(physaddr_t addr, size_t size) {
	int errdma = __reserve_pages(&dma_zone, addr, size);
	int errdma32 = __reserve_pages(dma32_zone, addr, size);
	int errnormal = __reserve_pages(normal_zone, addr, size);
	if (unlikely(errdma || errdma32 || errnormal)) {
		printk(PRINTK_ERR "mm: Failed to reserve region %#16lx: dma: %i, dma32: %i, normal: %i\n",
				addr, errdma, errdma32, errnormal);
	}
}

static void reserve_unusable_memory(physaddr_t last_ram) {
	size_t page_count = (last_ram + 1) >> PAGE_SHIFT;
	physaddr_t address = 0;
	while (page_count) {
		const struct limine_mmap_entry* entry = mmap_get_entry_from_page(address);
		size_t size = PAGE_SIZE;
		bool usable = false;
		if (entry) {
			size = entry->length;
			bug((size & (PAGE_SIZE - 1)) != 0);
			usable = mmap_entry_usable_strict(entry);
		}
		if (!usable)
			reserve_pages(address, size);

		address += size;
		bug(__builtin_sub_overflow(page_count, size >> PAGE_SHIFT, &page_count));
	}
}

static struct page* page_array;
static size_t page_count;

static inline size_t get_pfn_from_address(physaddr_t address) {
	size_t ret = address >> PAGE_SHIFT;
	return (ret >= page_count) ? SIZE_MAX : ret;
}

static inline size_t get_pfn_from_page(const struct page* page) {
	if (page < page_array || page >= page_array + page_count)
		return SIZE_MAX;
	return page - page_array;
}

static inline int get_address_from_pfn(size_t pfn, physaddr_t* out) {
	if (pfn >= page_count)
		return -ENOMEM;
	*out = pfn << PAGE_SHIFT;
	return 0;
}

/* To pair page_head(), but not accessable outside of this file */
static inline struct page* page_head_set(struct page* page, struct page* head) {
	return atomic_exchange(&page->buddy.head, head);
}

int get_page_from_address(physaddr_t address, struct page** page) {
	size_t pfn = get_pfn_from_address(address);
	if (pfn == SIZE_MAX)
		return -ENOMEM;

	struct page* _page = &page_array[pfn];
	int err = 0;
	if (mmap_region_is_usable_strict(address, PAGE_SIZE))
		err = try_hold_page(_page) ? 0 : -EACCES;
	else
		hold_page(_page);

	if (err == 0)
		*page = _page;
	return err;
}

struct page* alloc_pages(mm_t mm_flags, unsigned int order) {
	physaddr_t address = _alloc_pages(mm_flags, order);
	if (!address)
		return NULL;

	size_t pfn = get_pfn_from_address(address);
	struct page* page = &page_array[pfn];
	for (size_t i = 1; i < 1ul << order; i++) {
		bug(page_head_set(&page[i], page) != &page[i]);
		bug(atomic_load(&page[i].refcnt) != 0);
	}
	bug(atomic_exchange(&page->buddy.order, order) != 0);

	hold_page(page);
	return page;
}

void* page_hhdm_virtual(const struct page* page) {
	void* ret = NULL;
	size_t pfn = get_pfn_from_page(page);
	if (pfn != SIZE_MAX) {
		physaddr_t address;
		int err = get_address_from_pfn(pfn, &address);
		if (err == 0)
			ret = hhdm_virtual(address);
	}
	return ret;
}

static void page_inactive(struct page* page) {
	bug(page_head(page) != page);

	/* Check if the page(s) need to be given back to the allocator */
	size_t pfn = get_pfn_from_page(page);
	if (pfn == SIZE_MAX)
		return;
	physaddr_t address;
	bug(get_address_from_pfn(pfn, &address) != 0);
	if (!mmap_region_is_usable_strict(address, PAGE_SIZE))
		return;

	/* Make all the page's head pointers point to itself */
	unsigned int order = atomic_exchange(&page->buddy.order, 0);
	for (size_t i = 1; i < 1ul << order; i++)
		page_head_set(&page[i], &page[i]);

	_free_pages(address, order);
}

void hold_page(struct page* page) {
	page = page_head(page);
	atomic_fetch_add(&page->refcnt, 1);
}

bool try_hold_page(struct page* page) {
	page = page_head(page);
	long refcnt = atomic_load(&page->refcnt);
	do {
		if (refcnt <= 0)
			return false;
	} while (!atomic_compare_exchange_weak(&page->refcnt, &refcnt, refcnt + 1));
	return true;
}

void release_page(struct page* page) {
	page = page_head(page);
	long refcnt = atomic_sub_fetch(&page->refcnt, 1);
	bug(refcnt < 0);
	if (refcnt == 0)
		page_inactive(page);
}

static void create_page_array(physaddr_t last_ram) {
	page_count = (last_ram + 1) >> PAGE_SHIFT;
	page_array = hhdm_virtual(mmap_alloc(page_count * sizeof(*page_array)));
	if (!page_array)
		out_of_memory();

	const struct limine_mmap_entry* entry = NULL;
	for (size_t i = 0; i < page_count; i++) {
		if (!entry || !mmap_entry_check(entry, PAGE_SIZE * i, PAGE_SIZE))
			entry = mmap_get_entry_from_page(PAGE_SIZE * i);

		struct page* page = &page_array[i];
		if (!entry || !mmap_entry_usable_strict(entry))
			page->flags |= PAGE_FLAG_RESERVED;

		page_head_set(page, page);
		atomic_store(&page->buddy.order, 0);
		list_node_init(&page->pagetable_tlb_batch_link);
		atomic_store(&page->refcnt, 0);
	}
}

static void zones_init(void) {
	/* Revision is checked because of the memory map */
	int supported_revisions[] = { 0, 1, 2, 3, 4, 5, 6 };
	int revision = limine_match_base_revision(supported_revisions, ARRAY_SIZE(supported_revisions));
	if (unlikely(revision == -1))
		panic("Limine base revision %d unsupported by PMM", limine_base_revision());

	struct limine_mmap_response* response = mmap_request.response;
	if (unlikely(!response || response->entry_count == 0))
		panic("Where the fuck is the memory map");
	for (size_t i = 0; i < response->entry_count; i++) {
		int err = mmap_add_entry_unsanitized(response->entries[i]);
		if (unlikely(err))
			panic("mmap_add_entry_unsanitized() failed: %d\n", err);
	}
	mmap_sanitize();
	printk(PRINTK_INFO "mm: Memory map:\n");
	for (size_t i = 0; i < sanitized_mmap.entry_count; i++) {
		struct limine_mmap_entry* entry = &sanitized_mmap.entries[i];
		physaddr_t base = entry->base;
		printk(PRINTK_INFO "mm: %#lx-%#lx: %s\n", base, base + entry->length, mmap_entry_type_to_string(entry->type));
	}

	mem_total = mmap_total_usable();
	physaddr_t last_address = mmap_get_last_ram_address_inclusive();
	create_page_array(last_address);

	dma_zone_init(last_address);

	int err = zone_init(&__dma32_zone, MM_ZONE_DMA32, last_address, DMA32_START, DMA32_END, &dma_zone);
	if (unlikely(err)) {
		if (unlikely(err == -ELOOP)) {
			dma32_zone = &dma_zone;
			normal_zone = &dma_zone;
			goto out;
		}
		panic("Failed to initialize zone DMA32: %i", err);
	}
	dma32_zone = &__dma32_zone;

	err = zone_init(&__normal_zone, MM_ZONE_NORMAL, last_address, NORMAL_START, NORMAL_END, dma32_zone);
	if (err) {
		if (likely(err == -ELOOP)) {
			normal_zone = dma32_zone;
			goto out;
		}
		panic("Failed to initialize zone normal: %i", err);
	}
	normal_zone = &__normal_zone;

out:
	if (unlikely(dma32_zone == &dma_zone))
		printk(PRINTK_DBG "mm: DMA32 linked to DMA\n");
	else if (dma32_zone == normal_zone)
		printk(PRINTK_DBG "mm: Normal linked to DMA32\n");
	reserve_unusable_memory(last_address);
}

INIT_TASK_DECLARE(limine_base_revision_init_task, stack_tracer_init_task, hhdm_init_task);
INIT_TASK_DEFINE(zones_init_task, INIT_TASK_SCOPE_BSP, zones_init, &limine_base_revision_init_task, &stack_tracer_init_task, &hhdm_init_task);
