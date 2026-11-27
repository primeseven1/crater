#include <crater/pagecache.h>
#include <crater/slab.h>
#include <crater/mm.h>
#include <crater/init.h>
#include <crater/panic.h>
#include <crater/string.h>

#define PCE_DIRTY (1 << 0)

struct pagecache_entry {
	struct page* page; /* The cache holds one reference */
	unsigned long index;
	int flags;
	struct rbtree_node node;
};

static struct slab_cache* entry_cache;

static inline size_t size_to_pages(size_t size) {
	return (size >> PAGE_SHIFT) + ((size & (PAGE_SIZE - 1)) != 0);
}

/* Number of bytes of a page that are inside the file */
static inline size_t page_valid_len(unsigned long index, size_t size) {
	const size_t start = (size_t)index << PAGE_SHIFT;
	if (start >= size)
		return 0;
	return (size - start > PAGE_SIZE) ? PAGE_SIZE : size - start;
}

static struct pagecache_entry* find_entry(struct pagecache* pc, unsigned long index, struct rbtree_node** out_parent, struct rbtree_node*** out_link) {
	struct rbtree_node** link = &pc->pages.root;
	struct rbtree_node* parent = NULL;

	while (*link) {
		struct pagecache_entry* e = rbtree_entry(*link, struct pagecache_entry, node);
		if (index == e->index)
			return e;
		parent = *link;
		link = &(*link)->child[index > e->index ? RBTREE_DIRECTION_RIGHT : RBTREE_DIRECTION_LEFT];
	}

	if (out_parent)
		*out_parent = parent;
	if (out_link)
		*out_link = link;
	return NULL;
}

static void remove_entry(struct pagecache* pc, struct pagecache_entry* e) {
	rbtree_remove(&pc->pages, &e->node);
	pc->nr_pages--;
	if (e->flags & PCE_DIRTY)
		pc->nr_dirty--;
	release_page(e->page);
	slab_cache_free(entry_cache, e);
}

void pagecache_init(struct pagecache* pc, const struct pagecache_ops* ops, void* priv) {
	pc->ops = ops;
	pc->private = priv;
	rbtree_init(&pc->pages);
	pc->nr_pages = 0;
	pc->nr_dirty = 0;
	mutex_init(&pc->mtx);
}

void pagecache_destroy(struct pagecache* pc) {
	mutex_acquire(&pc->mtx);

	struct pagecache_entry* e, *n;
	rbtree_postorder_for_each_entry_safe(e, n, &pc->pages, node) {
		release_page(e->page);
		slab_cache_free(entry_cache, e);
	}
	rbtree_init(&pc->pages);
	pc->nr_pages = 0;
	pc->nr_dirty = 0;

	mutex_release(&pc->mtx);
}

int pagecache_get_page(struct pagecache* pc, unsigned long index, size_t size, int flags, struct page** out) {
	struct rbtree_node* parent;
	struct rbtree_node** link;
	int err = 0;

	mutex_acquire(&pc->mtx);

	struct pagecache_entry* e = find_entry(pc, index, &parent, &link);
	if (e) {
		hold_page(e->page);
		*out = e->page;
		goto out;
	}

	/* Not cached. Pages inside the file come from the backing store, if there is one. */
	const size_t valid = page_valid_len(index, size);
	const bool from_backing = (valid && pc->ops && pc->ops->readpage);
	if (!from_backing && !(flags & PAGECACHE_CREATE)) {
		err = -ENOENT;
		goto out;
	}

	e = slab_cache_alloc(entry_cache);
	if (!e) {
		err = -ENOMEM;
		goto out;
	}
	struct page* page = alloc_page(MM_ZONE_NORMAL);
	if (!page) {
		slab_cache_free(entry_cache, e);
		err = -ENOMEM;
		goto out;
	}

	u8* buf = page_hhdm_virtual(page);
	if (from_backing) {
		err = pc->ops->readpage(pc, index, buf, valid);
		if (err) {
			release_page(page);
			slab_cache_free(entry_cache, e);
			goto out;
		}
		if (valid < PAGE_SIZE)
			memset(buf + valid, 0, PAGE_SIZE - valid);
	} else {
		memset(buf, 0, PAGE_SIZE);
	}

	e->index = index;
	e->page = page;
	e->flags = 0;
	rbtree_insert(&e->node, parent, link);
	rbtree_insert_fixup(&pc->pages, &e->node);
	pc->nr_pages++;

	hold_page(page); /* One for the cache, one for the caller */
	*out = page;
out:
	mutex_release(&pc->mtx);
	return err;
}

void pagecache_make_dirty(struct pagecache* pc, unsigned long index, struct page* page) {
	mutex_acquire(&pc->mtx);
	struct pagecache_entry* e = find_entry(pc, index, NULL, NULL);
	if (e && e->page == page && !(e->flags & PCE_DIRTY)) {
		e->flags |= PCE_DIRTY;
		pc->nr_dirty++;
	}
	mutex_release(&pc->mtx);
}

int pagecache_read(struct pagecache* pc, struct uio* uio, size_t size) {
	if (!uio || uio->rw != UIO_READ || uio->offset < 0)
		return -EINVAL;

	int err = 0;
	while (uio->resid && (size_t)uio->offset < size) {
		const size_t off = (size_t)uio->offset;
		const unsigned long index = off >> PAGE_SHIFT;
		const size_t poff = off & (PAGE_SIZE - 1);

		size_t count = PAGE_SIZE - poff;
		if (count > size - off)
			count = size - off;
		if (count > uio->resid)
			count = uio->resid;

		struct page* page;
		err = pagecache_get_page(pc, index, size, 0, &page);
		if (err == -ENOENT) {
			err = uio_zero(count, uio);
			if (err)
				break;
			continue;
		}
		if (err)
			break;

		/* The cache lock is not held here, the reference keeps the page alive */
		err = uio_move((u8*)page_hhdm_virtual(page) + poff, count, uio);
		release_page(page);
		if (err)
			break;
	}

	return err;
}

int pagecache_write(struct pagecache* pc, struct uio* uio, size_t* size) {
	if (!uio || uio->rw != UIO_WRITE || uio->offset < 0)
		return -EINVAL;

	int err = 0;
	while (uio->resid) {
		const size_t off = (size_t)uio->offset;
		const unsigned long index = off >> PAGE_SHIFT;
		const size_t poff = off & (PAGE_SIZE - 1);

		size_t count = PAGE_SIZE - poff;
		if (count > uio->resid)
			count = uio->resid;

		struct page* page;
		err = pagecache_get_page(pc, index, *size, PAGECACHE_CREATE, &page);
		if (err)
			break;

		const size_t before = uio->resid;
		err = uio_move((u8*)page_hhdm_virtual(page) + poff, count, uio);
		const size_t moved = before - uio->resid;
		if (moved) {
			pagecache_make_dirty(pc, index, page);
			if (off + moved > *size)
				*size = off + moved;
		}
		release_page(page);
		if (err)
			break;
	}

	return err;
}

void pagecache_truncate(struct pagecache* pc, size_t new_size) {
	const unsigned long first_gone = size_to_pages(new_size);
	mutex_acquire(&pc->mtx);

	/* Drop every page past the new end of the file */
	struct rbtree_node* node = rbtree_last_node(&pc->pages);
	while (node) {
		struct pagecache_entry* e = rbtree_entry(node, struct pagecache_entry, node);
		if (e->index < first_gone)
			break;
		node = rbtree_prev_node(node);
		remove_entry(pc, e);
	}

	/* Keep the zero invariant for the bytes past the end of the new last page */
	const size_t tail = new_size & (PAGE_SIZE - 1);
	if (tail) {
		struct pagecache_entry* e = find_entry(pc, new_size >> PAGE_SHIFT, NULL, NULL);
		if (e)
			memset((u8*)page_hhdm_virtual(e->page) + tail, 0, PAGE_SIZE - tail);
	}

	mutex_release(&pc->mtx);
}

int pagecache_sync(struct pagecache* pc, size_t size) {
	if (!pc->ops || !pc->ops->writepage)
		return 0;

	int ret = 0;
	mutex_acquire(&pc->mtx);

	struct pagecache_entry* e;
	rbtree_for_each_entry(e, &pc->pages, node) {
		if (pc->nr_dirty == 0)
			break;
		if (!(e->flags & PCE_DIRTY))
			continue;

		const size_t len = page_valid_len(e->index, size);
		int err = len ? pc->ops->writepage(pc, e->index, page_hhdm_virtual(e->page), len) : 0;
		if (err) {
			if (ret == 0)
				ret = err;
			continue;
		}
		e->flags &= ~PCE_DIRTY;
		pc->nr_dirty--;
	}

	mutex_release(&pc->mtx);
	return ret;
}

size_t pagecache_evict(struct pagecache* pc) {
	if (!pc->ops || !pc->ops->readpage)
		return 0;

	size_t freed = 0;
	mutex_acquire(&pc->mtx);

	struct rbtree_node* node = rbtree_first_node(&pc->pages);
	while (node) {
		struct pagecache_entry* e = rbtree_entry(node, struct pagecache_entry, node);
		node = rbtree_next_node(node);

		if (e->flags & PCE_DIRTY)
			continue;
		if (atomic_load(&e->page->refcnt) != 1)
			continue;

		remove_entry(pc, e);
		freed++;
	}

	mutex_release(&pc->mtx);
	return freed;
}

static void _pagecache_init(void) {
	entry_cache = slab_cache_create(sizeof(struct pagecache_entry), alignof(struct pagecache_entry), MM_ZONE_NORMAL, NULL, NULL);
	if (!entry_cache)
		out_of_memory();
}

INIT_TASK_DECLARE(heap_init_task);
INIT_TASK_DEFINE(pagecache_init_task, INIT_TASK_SCOPE_BSP, _pagecache_init, &heap_init_task);
