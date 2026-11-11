#include <crater/limine.h>
#include <crater/page.h>
#include <crater/printk.h>
#include <crater/panic.h>
#include <crater/mm.h>
#include <crater/init.h>

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
	size_t entry_count;
	struct limine_mmap_entry entries[1024];
} unsanitized_mmap = { .entry_count = 0, .entries = { [0 ... 1023] = { .base = 0, .length = 0, .type = 0 } } };

static int mmap_add_entry_unsanitized(const struct limine_mmap_entry* entry) {
	if (unsanitized_mmap.entry_count >= ARRAY_SIZE(unsanitized_mmap.entries))
		return -ENOSPC;
	unsanitized_mmap.entries[unsanitized_mmap.entry_count++] = *entry;
	return 0;
}

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

static inline bool mmap_entry_usable_strict(const struct limine_mmap_entry* entry) {
	return entry->type == LIMINE_MMAP_USABLE;
}

static inline bool mmap_entry_reclaimable(const struct limine_mmap_entry* entry) {
	/*
	 * Don't return true when the entry is LIMINE_MMAP_ACPI_RECLAIMABLE because we do not
	 * care to reclaim it. uACPI also actively uses it.
	 */
	return entry->type == LIMINE_MMAP_BOOTLOADER_RECLAIMABLE;
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

#define MMAP_TYPE_BIT(t) (1u << (t))

static struct {
	size_t entry_count;
	struct limine_mmap_entry entries[1024]; /* Around 6 pages */
} sanitized_mmap = { .entry_count = 0, .entries = { [0 ... 1023] = { .base = 0, .length = 0, .type = 0 } } };

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

struct zone {
	const char* name;
	struct zone* fallback; /* The zone to try when this one has no memory, NULL if there isn't one */
	spinlock_t lock;
	struct list_head free_lists[MAX_ORDER + 1]; /* struct page, linked with free_link */
	unsigned long __atomic free_pages; /* Only written with the lock held, can be read without it */
};

static struct zone dma_zone = { .name = "DMA", .fallback = NULL, .lock = SPINLOCK_INITIALIZER, .free_pages = atomic_init(0) };
static struct zone dma32_zone = { .name = "DMA32", .fallback = &dma_zone, .lock = SPINLOCK_INITIALIZER, .free_pages = atomic_init(0) };
static struct zone normal_zone = { .name = "Normal", .fallback = &dma32_zone, .lock = SPINLOCK_INITIALIZER, .free_pages = atomic_init(0) };

static_assert(DMA32_START % (PAGE_SIZE << MAX_ORDER) == 0, "DMA32_START not aligned to the biggest block size");
static_assert(NORMAL_START % (PAGE_SIZE << MAX_ORDER) == 0, "NORMAL_START not aligned to the biggest block size");

/* Get a pointer to a zone from MM flags. Returns the most restrictive zone */
static inline struct zone* get_zone_mm(mm_t mm_flags) {
	if (mm_flags & MM_ZONE_DMA)
		return &dma_zone;
	if (mm_flags & MM_ZONE_DMA32)
		return &dma32_zone;
	return &normal_zone;
}

/* Get a pointer to a zone from the page frame number */
static inline struct zone* get_zone_pfn(size_t pfn) {
	if (pfn < (DMA32_START >> PAGE_SHIFT))
		return &dma_zone;
	if (pfn < (NORMAL_START >> PAGE_SHIFT))
		return &dma32_zone;
	return &normal_zone;
}

/* Put a block on a free list, zone->lock must be held */
static inline void free_list_add(struct zone* zone, struct page* page, unsigned int order) {
	atomic_store(&page->buddy.order, order);
	atomic_fetch_or(&page->flags, PAGE_FLAG_FREE);
	list_add(&zone->free_lists[order], &page->free_link);
}

/* Take a block off of the free list it's on, zone->lock must be held */
static inline void free_list_remove(struct page* page) {
	list_remove(&page->free_link);
	atomic_fetch_and(&page->flags, ~PAGE_FLAG_FREE);
}

/* Allocate a block from a zone, the order of the block is stored in the returned page */
static struct page* zone_alloc(struct zone* zone, unsigned int order) {
	struct page* page = NULL;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&zone->lock, &irq_flags);

	for (unsigned int i = order; i <= MAX_ORDER; i++) {
		if (list_empty(&zone->free_lists[i]))
			continue;

		/* Split the block until it's the right size */
		page = list_first_entry(&zone->free_lists[i], struct page, free_link);
		free_list_remove(page);
		while (i > order) {
			i--;
			free_list_add(zone, page + (1ul << i), i);
		}

		atomic_store(&page->buddy.order, order);
		atomic_sub_fetch_explicit(&zone->free_pages, 1ul << order, memory_order_relaxed);
		break;
	}

	spinlock_release_irq_restore(&zone->lock, &irq_flags);
	return page;
}

/* Give a block back to a zone, the block gets merged with its buddy for as long as the buddy is free */
static void zone_free(struct zone* zone, struct page* page, unsigned int order) {
	size_t pfn = page - page_array;
	const unsigned long freed = 1ul << order;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&zone->lock, &irq_flags);

	bug(atomic_load_explicit(&page->flags, memory_order_seq_cst) & PAGE_FLAG_FREE); /* Double free */
	while (order < MAX_ORDER) {
		size_t buddy_pfn = pfn ^ (1ul << order);
		if (buddy_pfn >= page_count)
			break;

		/*
		 * Only the first page of a free block has the free flag set, so a buddy that is allocated, reserved,
		 * or split into smaller blocks will stop the merge here.
		 */
		struct page* buddy = &page_array[buddy_pfn];
		if (!(atomic_load(&buddy->flags) & PAGE_FLAG_FREE) || atomic_load(&buddy->buddy.order) != order)
			break;

		free_list_remove(buddy);
		pfn &= buddy_pfn; /* The merged block starts at whichever one comes first */
		order++;
	}

	free_list_add(zone, &page_array[pfn], order);
	atomic_add_fetch_explicit(&zone->free_pages, freed, memory_order_relaxed);

	spinlock_release_irq_restore(&zone->lock, &irq_flags);
}

static u64 __atomic total_pages = 0;

void mm_get_free_pages(size_t* total_page_count, size_t* free_page_count) {
	*total_page_count = atomic_load_explicit(&total_pages, memory_order_relaxed);
	*free_page_count = atomic_load_explicit(&dma_zone.free_pages, memory_order_relaxed) +
		atomic_load_explicit(&dma32_zone.free_pages, memory_order_relaxed) +
		atomic_load_explicit(&normal_zone.free_pages, memory_order_relaxed);
}

void out_of_memory(void) {
	panic("Out of memory");
}

int get_page_from_address(physaddr_t address, struct page** page) {
	size_t pfn = get_pfn_from_address(address);
	if (pfn == SIZE_MAX)
		return -ENOMEM;

	/* Pages owned by the allocator can only be held if someone else has it allocated */
	struct page* _page = &page_array[pfn];
	int err = 0;
	if (atomic_load(&_page->flags) & PAGE_FLAG_RESERVED)
		hold_page(_page);
	else
		err = try_hold_page(_page) ? 0 : -EACCES;

	if (err == 0)
		*page = _page;
	return err;
}

struct page* alloc_pages(mm_t mm_flags, unsigned int order) {
	if (order > MAX_ORDER)
		return NULL;

	struct page* page = NULL;
	for (struct zone* zone = get_zone_mm(mm_flags); zone && !page; zone = zone->fallback)
		page = zone_alloc(zone, order);
	if (!page) {
		if (mm_flags & MM_NOFAIL)
			out_of_memory();
		return NULL;
	}

	bug(page_head(page) != page);
	for (size_t i = 1; i < 1ul << order; i++) {
		bug(page_head_set(&page[i], page) != &page[i]);
		bug(atomic_load(&page[i].refcnt) != 0);
	}

	bug(atomic_fetch_add(&page->refcnt, 1) != 0);
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
	if (atomic_load(&page->flags) & PAGE_FLAG_RESERVED)
		return;

	/* Make all the page's head pointers point to itself */
	unsigned int order = atomic_load(&page->buddy.order);
	for (size_t i = 1; i < 1ul << order; i++)
		page_head_set(&page[i], &page[i]);

	zone_free(get_zone_pfn(pfn), page, order);
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

void reclaim_page(struct page* page) {
	bug((atomic_fetch_and(&page->flags, ~PAGE_FLAG_NEED_RECLAIM) & PAGE_FLAG_NEED_RECLAIM) == 0 || atomic_load(&page->buddy.order) != 0 || page_head(page) != page);
	atomic_fetch_add_explicit(&total_pages, 1, memory_order_relaxed);
	release_page(page);
}

static void zones_init(void) {
	/* Revision is checked because of the memory map */
	int supported_revisions[] = { 0, 1, 2, 3, 4, 5, 6 };
	int revision = limine_match_base_revision(supported_revisions, ARRAY_SIZE(supported_revisions));
	if (unlikely(revision == -1))
		panic("Limine base revision %d unsupported by PMM", limine_base_revision());

	/* Prepare the bootloader memory map for the allocator */
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

	/* Create the page array */
	physaddr_t last_address = 0;
	for (u64 i = 0; i < sanitized_mmap.entry_count; i++) {
		struct limine_mmap_entry* entry = &sanitized_mmap.entries[i];
		if (mmap_entry_is_ram(entry))
			last_address = entry->base + entry->length;
	}
	page_count = last_address >> PAGE_SHIFT;
	page_array = hhdm_virtual(mmap_alloc(page_count * sizeof(*page_array)));
	if (!page_array)
		out_of_memory();
	const struct limine_mmap_entry* entry = NULL;
	for (size_t i = 0; i < page_count; i++) {
		if (!entry || !mmap_entry_check(entry, PAGE_SIZE * i, PAGE_SIZE))
			entry = mmap_get_entry_from_page(PAGE_SIZE * i);

		struct page* page = &page_array[i];
		int flags = 0;
		long refcnt = 0;
		if (!entry || !mmap_entry_usable(entry)) {
			flags |= PAGE_FLAG_RESERVED;
		} else if (mmap_entry_reclaimable(entry)) {
			flags |= PAGE_FLAG_NEED_RECLAIM;
			refcnt = 1; /* This allows release_page() to handle freeing. */
		}

		atomic_store(&page->flags, flags);
		page_head_set(page, page);
		atomic_store(&page->buddy.order, 0);
		list_node_init(&page->free_link);
		atomic_store(&page->refcnt, refcnt);
	}

	/* Initialize and populate the zones */
	struct zone* const zones[] = { &dma_zone, &dma32_zone, &normal_zone };
	for (size_t i = 0; i < ARRAY_SIZE(zones); i++) {
		for (unsigned int order = 0; order <= MAX_ORDER; order++)
			list_head_init(&zones[i]->free_lists[order]);
	}
	for (size_t i = 0; i < sanitized_mmap.entry_count; i++) {
		entry = &sanitized_mmap.entries[i];

		/* The strict variant prevents the bootloader reclaimable entries from ending up in the free list */
		if (!mmap_entry_usable_strict(entry))
			continue;

		size_t pfn = entry->base >> PAGE_SHIFT;
		const size_t end_pfn = (entry->base + entry->length) >> PAGE_SHIFT;
		bug(end_pfn > page_count);

		/* Now add the pages into the free list */
		while (pfn < end_pfn) {
			unsigned int order = MAX_ORDER;
			if (likely(pfn) && (unsigned int)__builtin_ctzl(pfn) < order)
				order = __builtin_ctzl(pfn);
			while ((1ul << order) > end_pfn - pfn)
				order--;

			zone_free(get_zone_pfn(pfn), &page_array[pfn], order); /* Using zone_free() here allows the buddy merge to still run */
			pfn += 1ul << order;
			atomic_fetch_add(&total_pages, 1ul << order);
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(zones); i++)
		printk(PRINTK_DBG "mm: Zone %s: %lu pages free\n", zones[i]->name, atomic_load_explicit(&zones[i]->free_pages, memory_order_relaxed));
}

INIT_TASK_DECLARE(limine_base_revision_init_task, stack_tracer_init_task, hhdm_init_task);
INIT_TASK_DEFINE(zones_init_task, INIT_TASK_SCOPE_BSP, zones_init, &limine_base_revision_init_task, &stack_tracer_init_task, &hhdm_init_task);
