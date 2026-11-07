#include <crater/slab.h>
#include <crater/vmm.h>
#include <crater/init.h>
#include <crater/printk.h>
#include <crater/hashtable.h>
#include <crater/panic.h>
#include <crater/vfs.h>
#include <crater/timekeeper.h>
#include <crater/string.h>
#include <crater/trace.h>
#include <crater/device.h>

static unsigned long __atomic id = atomic_init(1);

struct tmpfs_node {
	struct vnode vnode;
	struct vattr attr;
	union {
		struct {
			u8* data;
			size_t cap;
		} regular;
		struct hashtable* directory;
		char* symlink;
	} un;
};

struct tmpfs_mount {
	struct mount mount;
	unsigned long id;
	ino_t __atomic next_ino;
};

static int check_args(const struct vnode* dvp, const char* name) {
	if (!dvp || !name || strlen(name) == 0)
		return -EINVAL;
	if (strlen(name) > NAME_MAX)
		return -ENAMETOOLONG;
	if (dvp->type != VTYPE_DIR)
		return -ENOTDIR;
	return 0;
}

static const struct vnode_ops tmpfs_node_ops; /* Defined at the end of the file */
static struct slab_cache* tmpnode_cache;

static struct tmpfs_node* new_node(struct tmpfs_mount* tmount, enum vtype type, int flags, const struct vattr* attr) {
	struct tmpfs_node* ret = slab_cache_alloc(tmpnode_cache);
	if (!ret)
		return NULL;

	const bool is_dev = (type == VTYPE_CHDEV || type == VTYPE_BLKDEV);
	VOP_INIT(&ret->vnode, type, &tmpfs_node_ops, flags, &tmount->mount);
	ret->attr = (struct vattr){
		.type = type,
		.mode = attr ? attr->mode : 0,
		.uid = attr ? attr->uid : 0, .gid = attr ? attr->gid : 0,
		.fsid = tmount->id,
		.inode = atomic_fetch_add(&tmount->next_ino, 1),
		.nlinks = 0,
		.size = 0, .fsblocksize = PAGE_SIZE,
		.atime = { .tv_sec = 0, .tv_nsec = 0 }, .mtime = { .tv_sec = 0, .tv_nsec = 0 }, .ctime = { .tv_sec = 0, .tv_nsec = 0 },
		.rdevmajor = (is_dev && attr) ? attr->rdevmajor : 0, .rdevminor = (is_dev && attr) ? attr->rdevminor : 0,
		.devmajor = 0, .devminor = 0,
		.blocksused = 0
	};
	memset(&ret->un, 0, sizeof(ret->un));

	return ret;
}

static void destroy_node(struct tmpfs_node* tnode) {
	switch (tnode->vnode.type) {
	case VTYPE_REGULAR:
		if (tnode->un.regular.data)
			vfree(tnode->un.regular.data);
		break;
	case VTYPE_DIR:
		if (tnode->un.directory)
			hashtable_destroy(tnode->un.directory);
		break;
	case VTYPE_LINK:
		kfree(tnode->un.symlink);
		break;
	default:
		break;
	}
	slab_cache_free(tmpnode_cache, tnode);
}

static int search_directory(struct tmpfs_node* node, const char* name, struct tmpfs_node** out) {
	if (node->vnode.type != VTYPE_DIR)
		return -ENOTDIR;
	struct tmpfs_node* tmp;
	int err = hashtable_search(node->un.directory, name, strlen(name), &tmp);
	if (err == 0 && out)
		*out = tmp;
	return err;
}

static int init_directory(struct tmpfs_node* tnode, struct tmpfs_node* parent) {
	if (tnode->vnode.type != VTYPE_DIR)
		return -ENOTDIR;
	tnode->un.directory = hashtable_create(24, sizeof(struct tmpfs_node*));
	if (!tnode->un.directory)
		return -ENOMEM;

	/* destroy_node() will destroy this for us on failure */
	int err = hashtable_insert(tnode->un.directory, ".", 1, &tnode);
	if (err)
		return err;
	return hashtable_insert(tnode->un.directory, "..", 2, &parent);
}

static int create_node(struct vnode* dvp, const char* name, enum vtype type, const struct vattr* attr, const char* symlink_target, struct vnode** out) {
	int err = check_args(dvp, name);
	if (err)
		return err;

	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	err = search_directory(tdvp, name, NULL);
	if (err == 0)
		return -EEXIST;

	struct tmpfs_node* tnode = new_node(container_of(dvp->belongs_to, struct tmpfs_mount, mount), type, 0, attr);
	if (!tnode)
		return -ENOMEM;
	if (type == VTYPE_DIR) {
		err = init_directory(tnode, tdvp);
		if (err)
			goto err;
	} else if (type == VTYPE_LINK) {
		tnode->un.symlink = kstrdup(symlink_target, MM_ZONE_NORMAL);
		if (!tnode->un.symlink) {
			err = -ENOMEM;
			goto err;
		}
		tnode->attr.size = strlen(tnode->un.symlink);
	}

	err = hashtable_insert(tdvp->un.directory, name, strlen(name), &tnode);
	if (err)
		goto err;

	tnode->attr.nlinks++;
	if (type == VTYPE_DIR) {
		tnode->attr.nlinks++;
		tdvp->attr.nlinks++;
	}

	if (out) {
		VOP_HOLD(&tnode->vnode);
		*out = &tnode->vnode;
	}
	return 0;
err:
	destroy_node(tnode);
	return err;
}

static int tmpfs_create(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)cred;
	if (!attr)
		return -EINVAL;
	return create_node(dvp, name, VTYPE_REGULAR, attr, NULL, out);
}

static int tmpfs_mkdir(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)cred;
	if (!attr)
		return -EINVAL;
	return create_node(dvp, name, VTYPE_DIR, attr, NULL, out);
}

static int tmpfs_mknod(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)cred;
	if (!attr || (attr->type != VTYPE_CHDEV && attr->type != VTYPE_BLKDEV && attr->type != VTYPE_FIFO))
		return -EINVAL;
	return create_node(dvp, name, attr->type, attr, NULL, out);
}

static inline bool key_equals(const struct hashtable_node* node, const char* str) {
	const size_t len = strlen(str);
	return node->key_size == len && memcmp(node->key, str, len) == 0;
}

static foreach_iteration_desicion_t tmpfs_foreach_isempty(struct hashtable* table, struct hashtable_node* node, void* ctx) {
	(void)table;
	bool* empty = ctx;

	if (!key_equals(node, ".") && !key_equals(node, "..")) {
		*empty = false;
		return FOREACH_ITERATION_DESICION_BREAK;
	}

	return FOREACH_ITERATION_DESICION_CONTINUE;
}

static inline int remove_from_directory(struct tmpfs_node* tdvp, struct tmpfs_node* tchild, const char* name) {
	struct tmpfs_node* tmp;
	int err = search_directory(tdvp, name, &tmp);
	bug(err != 0 || tmp != tchild);
	return hashtable_remove(tdvp->un.directory, name, strlen(name));
}

static int tmpfs_rmdir(struct vnode* dvp, struct vnode* vnode, const char* name, const struct cred* cred) {
	(void)cred;
	if (!vnode)
		return -EINVAL;
	int err = check_args(dvp, name);
	if (err)
		return err;
	if (vnode->type != VTYPE_DIR)
		return -ENOTDIR;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	bool empty = true;
	hashtable_for_each_entry(tnode->un.directory, tmpfs_foreach_isempty, &empty);
	if (!empty)
		return -ENOTEMPTY;

	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	err = remove_from_directory(tdvp, tnode, name);
	if (err == 0) {
		tnode->attr.nlinks -= 2; /* its entry in dvp, and its own . */
		tdvp->attr.nlinks--; /* .. */
		VOP_RELEASE(vnode);
	}
	return err;
}

static int tmpfs_open(struct vnode** vnode, int flags, const struct cred* cred) {
	struct vnode* v = *vnode;
	if (v->type == VTYPE_SOCKET || v->type == VTYPE_FIFO)
		return -ENXIO;

	if (v->type == VTYPE_CHDEV || v->type == VTYPE_BLKDEV) {
		struct vnode* devnode;
		int err = devfs_get_node(v, &devnode, cred);
		if (err)
			return err;
		err = VOP_OPEN(&devnode, flags, cred);
		if (err) {
			VOP_RELEASE(devnode);
			return err;
		}
		*vnode = devnode;
		return 0;
	}

	return 0;
}

static int tmpfs_close(struct vnode* vnode, int flags, const struct cred* cred) {
	(void)vnode;
	(void)flags;
	(void)cred;
	return 0;
}

static int tmpfs_read(struct vnode* vnode, struct uio* uio, int flags, const struct cred* cred) {
	(void)flags;
	(void)cred;
	if (vnode->type != VTYPE_REGULAR)
		return -EINVAL;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	const size_t size = tnode->attr.size;
	const size_t off = (size_t)uio->offset;
	if (off >= size)
		return 0;

	return uio_move(tnode->un.regular.data + off, size - off, uio);
}

static int reserve(struct tmpfs_node* tnode, size_t size) {
	if (size <= tnode->un.regular.cap)
		return 0;

	size_t new_cap = (size + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);
	u8* new_data = vmalloc(new_cap);
	if (!new_data)
		return -ENOMEM;
	if (tnode->un.regular.data) {
		memcpy(new_data, tnode->un.regular.data, tnode->un.regular.cap);
		vfree(tnode->un.regular.data);
	}
	tnode->un.regular.data = new_data;
	tnode->un.regular.cap = new_cap;

	return 0;
}

static int tmpfs_write(struct vnode* vnode, struct uio* uio, int flags, const struct cred* cred) {
	(void)flags;
	(void)cred;
	if (vnode->type != VTYPE_REGULAR)
		return -EINVAL;
	if (uio->resid == 0)
		return 0;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	const size_t off = (size_t)uio->offset;
	size_t end;
	if (__builtin_add_overflow(off, uio->resid, &end) || end > (size_t)SSIZE_MAX)
		return -EFBIG;

	int err = reserve(tnode, end);
	if (err)
		return err;

	tnode->attr.blocksused = tnode->un.regular.cap / 512;
	if (off > tnode->attr.size)
		memset(tnode->un.regular.data + tnode->attr.size, 0, off - tnode->attr.size);

	const size_t start = uio->resid;
	err = uio_move(tnode->un.regular.data + off, start, uio);
	const size_t written = start - uio->resid;
	if (written && off + written > tnode->attr.size)
		tnode->attr.size = off + written;

	return err;
}

static int tmpfs_lookup(struct vnode* ref, const char* name, struct vnode** out, const struct cred* cred) {
	(void)cred;
	if (!name || !out)
		return -EINVAL;
	if (strlen(name) == 0)
		return -ENOENT;
	if (strlen(name) > NAME_MAX)
		return -ENAMETOOLONG;
	if (ref->type != VTYPE_DIR)
		return -ENOTDIR;

	struct tmpfs_node* tnode = container_of(ref, struct tmpfs_node, vnode);
	struct tmpfs_node* tmp;
	int ret = search_directory(tnode, name, &tmp);
	if (ret == 0) {
		VOP_HOLD(&tmp->vnode);
		*out = &tmp->vnode;
	}
	return ret;
}

static int tmpfs_sync(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static int tmpfs_getattr(struct vnode* vnode, struct vattr* attr, const struct cred* cred) {
	(void)cred;
	struct tmpfs_node* const tnode = container_of(vnode, struct tmpfs_node, vnode);
	memcpy(attr, &tnode->attr, sizeof(*attr));
	return 0;
}

static int tmpfs_setattr(struct vnode* vnode, const struct vattr* attr, int attrs, const struct cred* cred) {
	(void)cred;
	struct tmpfs_node* const tnode = container_of(vnode, struct tmpfs_node, vnode);

	if (attrs & VATTR_SIZE) {
		if (vnode->type == VTYPE_DIR)
			return -EISDIR;
		if (vnode->type != VTYPE_REGULAR)
			return -EINVAL;
		if (attr->size > (size_t)SSIZE_MAX)
			return -EFBIG;
		if (attr->size > tnode->attr.size) {
			int err = reserve(tnode, attr->size);
			if (err)
				return err;
			memset(tnode->un.regular.data + tnode->attr.size, 0, attr->size - tnode->attr.size);
		}

		/* This does not free the buffer, blocksused will stay at the old capacity until the page cache replaces this */
		tnode->attr.size = attr->size;
		tnode->attr.blocksused = tnode->un.regular.cap / 512;
	}
	if (attrs & VATTR_MODE)
		tnode->attr.mode = attr->mode;
	if (attrs & VATTR_UID)
		tnode->attr.uid = attr->uid;
	if (attrs & VATTR_GID)
		tnode->attr.gid = attr->gid;
	if (attrs & VATTR_ATIME)
		tnode->attr.atime = attr->atime;
	if (attrs & VATTR_MTIME)
		tnode->attr.mtime = attr->mtime;
	if (attrs & VATTR_CTIME)
		tnode->attr.ctime = attr->ctime;

	return 0;
}

static int tmpfs_link(struct vnode* dvp, struct vnode* svp, const char* name, const struct cred* cred) {
	(void)cred;
	int err = check_args(dvp, name);
	if (err)
		return err;
	if (!svp)
		return -EINVAL;
	if (dvp->belongs_to != svp->belongs_to)
		return -EXDEV;
	if (svp->type == VTYPE_DIR)
		return -EPERM;

	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	if (search_directory(tdvp, name, NULL) == 0)
		return -EEXIST;

	struct tmpfs_node* tsvp = container_of(svp, struct tmpfs_node, vnode);
	err = hashtable_insert(tdvp->un.directory, name, strlen(name), &tsvp);
	if (err == 0) {
		VOP_HOLD(svp);
		tsvp->attr.nlinks++;
	}

	return err;
}

static int tmpfs_symlink(struct vnode* dvp, const char* name, const char* target, const struct vattr* attr, const struct cred* cred) {
	(void)cred;
	if (!target || strlen(target) == 0)
		return -EINVAL;
	if (strlen(target) > PATHNAME_MAX)
		return -ENAMETOOLONG;
	return create_node(dvp, name, VTYPE_LINK, attr, target, NULL);
}

static int tmpfs_unlink(struct vnode* dvp, struct vnode* child, const char* name, const struct cred* cred) {
	(void)cred;
	int err = check_args(dvp, name);
	if (err)
		return err;
	if (!child)
		return -EINVAL;
	if (child->type == VTYPE_DIR)
		return -EISDIR;

	struct tmpfs_node* tchild = container_of(child, struct tmpfs_node, vnode);
	err = remove_from_directory(container_of(dvp, struct tmpfs_node, vnode), tchild, name);
	if (err == 0) {
		tchild->attr.nlinks--;
		VOP_RELEASE(child);
	}

	return err;
}

static int tmpfs_readlink(struct vnode* vnode, char** out, const struct cred* cred) {
	(void)cred;
	if (vnode->type != VTYPE_LINK)
		return -EINVAL;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	char* _out = kstrdup(tnode->un.symlink, MM_ZONE_NORMAL);
	if (!_out)
		return -ENOMEM;

	*out = _out;
	return 0;
}

static void tmpfs_freelink(char* link) {
	kfree(link);
}

struct tmpfs_dent_ctx {
	struct dirent* buf;
	const size_t count;
	size_t rcount;
	const off_t off;
	off_t npos;
};

static foreach_iteration_desicion_t tmpfs_foreach_getdents(struct hashtable* table, struct hashtable_node* node, void* ctx) {
	(void)table;
	struct tmpfs_dent_ctx* data = ctx;
	if (data->npos < data->off) {
		data->npos++;
		return FOREACH_ITERATION_DESICION_CONTINUE;
	}
	if (data->rcount == data->count)
		return FOREACH_ITERATION_DESICION_BREAK;

	struct tmpfs_node* tnode;
	memcpy(&tnode, node->value, sizeof(typeof(tnode))); /* typeof suppresses the compiler warning */

	struct dirent* entry = &data->buf[data->rcount];
	entry->ino = tnode->attr.inode;
	entry->off = data->npos;
	entry->reclen = sizeof(*entry);
	size_t name_len = node->key_size;
	if (name_len > sizeof(entry->name) - 1)
		name_len = sizeof(entry->name) - 1;
	memcpy(entry->name, node->key, name_len);
	entry->name[name_len] = '\0';
	entry->type = vfs_posix_type(tnode->vnode.type);
	data->rcount++;
	data->npos++;

	return FOREACH_ITERATION_DESICION_CONTINUE;
}

static int tmpfs_getdents(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* out_rcount) {
	if (off < 0)
		return -EINVAL;
	if (dir->type != VTYPE_DIR)
		return -ENOTDIR;

	*out_rcount = 0;
	if (count == 0)
		return 0;
	if (!buf)
		return -EINVAL;

	struct tmpfs_dent_ctx ctx = { .buf = buf, .count = count, .rcount = 0, .off = off, .npos = 0 };
	hashtable_for_each_entry(container_of(dir, struct tmpfs_node, vnode)->un.directory, tmpfs_foreach_getdents, &ctx);
	*out_rcount = ctx.rcount;
	return 0;
}

static void tmpfs_inactive(struct vnode* vnode) {
	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	bug(tnode->attr.nlinks != 0);
	destroy_node(tnode);
}

static const struct vnode_ops tmpfs_node_ops = {
	.create = tmpfs_create,
	.mkdir = tmpfs_mkdir,
	.rmdir = tmpfs_rmdir,
	.mknod = tmpfs_mknod,
	.open = tmpfs_open,
	.close = tmpfs_close,
	.read = tmpfs_read,
	.write = tmpfs_write,
	.lookup = tmpfs_lookup,
	.sync = tmpfs_sync,
	.getattr = tmpfs_getattr,
	.setattr = tmpfs_setattr,
	.link = tmpfs_link,
	.symlink = tmpfs_symlink,
	.unlink = tmpfs_unlink,
	.readlink = tmpfs_readlink,
	.freelink = tmpfs_freelink,
	.getdents = tmpfs_getdents,
	.ioctl = NULL,
	.lock = NULL,
	.unlock = NULL,
	.inactive = tmpfs_inactive
};

static int tmpfs_mnt_mount(struct vnode* backing, void* data, struct mount** out_mount) {
	(void)backing;
	(void)data;

	struct tmpfs_mount* tmount = kzalloc(sizeof(*tmount), MM_ZONE_NORMAL);
	if (!tmount)
		return -ENOMEM;

	tmount->id = atomic_fetch_add(&id, 1);
	atomic_store(&tmount->next_ino, 1);

	const struct vattr attr = { .uid = current_cred()->uid, .gid = current_cred()->gid, .mode = 0 };
	struct tmpfs_node* root = new_node(tmount, VTYPE_DIR, VNODE_FLAG_ROOT, &attr);
	if (!root) {
		kfree(tmount);
		return -ENOMEM;
	}
	int err = init_directory(root, root);
	if (err) {
		destroy_node(root);
		kfree(tmount);
		return err;
	}
	root->attr.nlinks = 2; /* . and .., both itself */

	tmount->mount.root = &root->vnode;
	*out_mount = &tmount->mount;
	return 0;
}

static int tmpfs_mnt_unmount(struct mount* mount) {
	(void)mount;
	return -ENOSYS;
}

static int tmpfs_mnt_sync(struct mount* mount) {
	(void)mount;
	return 0;
}

static int tmpfs_mnt_statfs(struct mount* mount, struct fsattr* attr) {
	size_t page_count, free_pages;
	mm_get_free_pages(&page_count, &free_pages);
	struct tmpfs_mount* mnt = container_of(mount, struct tmpfs_mount, mount);
	*attr = (struct fsattr){
		.io_size = PAGE_SIZE,
		.block_size = PAGE_SIZE,
		.block_count = page_count,
		.free_blocks = free_pages,
		.free_blocks_unprivileged = free_pages,
		.inode_count = SIZE_MAX,
		.free_inode_count = SIZE_MAX,
		.free_inode_count_unprivileged = SIZE_MAX,
		.fsid = mnt->id,
		.flags = 0,
		.max_name_size = NAME_MAX
	};
	return 0;
}

static const struct filesystem_type tmpfs_type = {
	.name = "tmpfs",
	.mount = tmpfs_mnt_mount,
	.unmount = tmpfs_mnt_unmount,
	.sync = tmpfs_mnt_sync,
	.statfs = tmpfs_mnt_statfs
};

static void tmpfs_init(void) {
	tmpnode_cache = slab_cache_create(sizeof(struct tmpfs_node), alignof(struct tmpfs_node), MM_ZONE_NORMAL, NULL, NULL);
	if (!tmpnode_cache)
		out_of_memory();
	int err = vfs_register(&tmpfs_type);
	if (unlikely(err))
		printk(PRINTK_WARN "tmpfs: Failed to register type, %d\n", err);
}

INIT_TASK_DECLARE(heap_init_task, vfs_init_task);
INIT_TASK_DEFINE(tmpfs_init_task, INIT_TASK_SCOPE_BSP, tmpfs_init, &heap_init_task, &vfs_init_task);
