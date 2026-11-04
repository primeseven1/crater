#include <crater/vfs.h>
#include <crater/slab.h>
#include <crater/hashtable.h>
#include <crater/proc.h>
#include <crater/rwlock.h>
#include <crater/uio.h>
#include <crater/printk.h>

static struct vnode_ops noops = { 0 };
static struct vnode vfs_root = {
	.type = VTYPE_DIR,
	.ops = &noops,
	.flags = VFLAG_ROOT,
	.belongs_to = NULL,
	.un.mounted_here = NULL,
	.refcnt = atomic_init(1),
	.mtx = MUTEX_INITIALIZER(vfs_root.mtx)
};
static LIST_HEAD_DEFINE(mounted_filesystems);
static MUTEX_DEFINE(mounted_filesystems_mtx);
static struct hashtable* fs_table;

static const struct filesystem_type* get_filesystem_type_from_name(const char* name) {
	const struct filesystem_type* ret;
	return (hashtable_search(fs_table, name, strlen(name), &ret) == 0) ? ret : NULL;
}

int vfs_register(const struct filesystem_type* fs_type) {
	const struct filesystem_type* tmp;
	if (hashtable_search(fs_table, fs_type->name, strlen(fs_type->name), &tmp) == 0)
		return -EEXIST;
	return hashtable_insert(fs_table, fs_type->name, strlen(fs_type->name), &fs_type);
}

int vfs_get_filesystem_type(const char* name, const struct filesystem_type** out) {
	if (!name || !out)
		return -EINVAL;

	const struct filesystem_type* tmp = get_filesystem_type_from_name(name);
	if (!tmp)
		return -ENODEV;

	*out = tmp;
	return 0;
}

int vfs_mount(const char* fs_name, struct vnode* ref, const char* mp, struct vnode* backing) {
	if (!fs_name)
		return -ENOSYS;
	const struct filesystem_type* type = get_filesystem_type_from_name(fs_name);
	if (!type)
		return -ENODEV;

	struct vnode* mp_vnode;
	int err = vfs_lookup(ref, mp, NULL, 0, &mp_vnode);
	if (err)
		return err;
	if (mp_vnode->type != VTYPE_DIR) {
		err = -ENOTDIR;
		goto err;
	}
	if (mp_vnode->flags & VFLAG_DEAD) {
		err = -ENOENT;
		goto err;
	}

	struct mount* mnt;
	err = type->mount(backing, NULL, &mnt);
	if (err) {
		VOP_UNLOCK(mp_vnode);
		VOP_RELEASE(mp_vnode);
		return err;
	}

	mnt->fs_type = type;
	mnt->covered = mp_vnode; /* Keep the ref from vfs_lookup() */
	atomic_store(&mnt->refcnt, 0);
	list_node_init(&mnt->link);
	mp_vnode->un.mounted_here = mnt;
	VOP_UNLOCK(mp_vnode); /* Unlock from vfs_lookup(), but don't unref the vnode here */

	mutex_acquire(&mounted_filesystems_mtx);
	list_add(&mounted_filesystems, &mnt->link);
	mutex_release(&mounted_filesystems_mtx);

	return 0;
err:
	VOP_UNLOCK(mp_vnode);
	VOP_RELEASE(mp_vnode);
	return err;
}

static bool has_trailing_slash(const char* path) {
	size_t len = strlen(path);
	if (len == 0 || path[len - 1] != '/')
		return false;
	for (size_t i = 0; i < len; i++) {
		if (path[i] != '/')
			return true;
	}
	return false;
}

static int create(struct vnode* ref, const char* path, enum vtype type, const struct vattr* attr, struct vnode** out) {
	char* lastcomp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!lastcomp)
		return -ENOMEM;

	struct vnode* parent;
	int err = vfs_lookup(ref, path, lastcomp, VFS_LOOKUP_PARENT, &parent);
	if (err)
		goto out;
	if (parent->flags & VFLAG_DEAD) {
		err = -ENOENT;
		goto cleanup;
	}
	if (strcmp(lastcomp, ".") == 0 || strcmp(lastcomp, "..") == 0) {
		err = -EEXIST;
		goto cleanup;
	}
	if (type != VTYPE_DIR && has_trailing_slash(path)) {
		err = -EISDIR;
		goto cleanup;
	}

	struct vnode* _out;
	if (type == VTYPE_REGULAR)
		err = VOP_CREATE(parent, lastcomp, attr, &_out, current_cred());
	else if (type == VTYPE_DIR)
		err = VOP_MKDIR(parent, lastcomp, attr, &_out, current_cred());
	else if (type == VTYPE_CHDEV || type == VTYPE_BLKDEV || type == VTYPE_FIFO)
		err = VOP_MKNOD(parent, lastcomp, attr, &_out, current_cred());
	else
		err = -ENOSYS;
	if (err)
		goto cleanup;

	if (out)
		*out = _out;
	else
		VOP_RELEASE(_out);

cleanup:
	VOP_UNLOCK(parent); /* locked and ref'd by vfs_lookup() */
	VOP_RELEASE(parent);
out:
	kfree(lastcomp);
	return err;
}

static int remove(struct vnode* ref, const char* path, bool directory) {
	char* lastcomp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!lastcomp)
		return -ENOMEM;
	struct vnode* parent;
	int err = vfs_lookup(ref, path, lastcomp, VFS_LOOKUP_PARENT, &parent);
	if (err)
		goto cleanup;
	if (parent->flags & VFLAG_DEAD) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(parent);
		err = -ENOENT;
		goto cleanup;
	}
	if (strcmp(lastcomp, ".") == 0 || strcmp(lastcomp, "..") == 0) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(parent);
		if (directory)
			err = -EBUSY;
		else
			err = -EISDIR;
		goto cleanup;
	}

	struct vnode* child;
	err = VOP_LOOKUP(parent, lastcomp, &child, current_cred());
	if (err) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(parent);
		goto cleanup;
	}
	VOP_LOCK(child);

	if (directory) {
		if (child->type != VTYPE_DIR)
			err = -ENOTDIR;
		else if (child->un.mounted_here)
			err = -EBUSY;
		else
			err = VOP_RMDIR(parent, child, lastcomp, current_cred());
		if (err == 0)
			child->flags |= VFLAG_DEAD;
	} else {
		if (child->type == VTYPE_DIR)
			err = -EISDIR;
		else if (has_trailing_slash(path))
			err = -ENOTDIR;
		else
			err = VOP_UNLINK(parent, child, lastcomp, current_cred());
	}

	VOP_UNLOCK(child);
	VOP_UNLOCK(parent);
	VOP_RELEASE(child);
	VOP_RELEASE(parent);
cleanup:
	kfree(lastcomp);
	return err;
}

int vfs_create(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out) {
	return create(ref, path, VTYPE_REGULAR, attr, out);
}

int vfs_mkdir(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out) {
	return create(ref, path, VTYPE_DIR, attr, out);
}

int vfs_mknod(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out) {
	if (!attr || (attr->type != VTYPE_CHDEV && attr->type != VTYPE_BLKDEV && attr->type != VTYPE_FIFO))
		return -EINVAL;
	return create(ref, path, attr->type, attr, out);
}

int vfs_rmdir(struct vnode* ref, const char* path) {
	return remove(ref, path, true);
}

int vfs_open(struct vnode* ref, const char* path, int flags, struct vnode** out) {
	if (!path || !out)
		return -EINVAL;

	struct vnode* tmp;
	int err = vfs_lookup(ref, path, NULL, 0, &tmp);
	if (err)
		return err;

	struct vnode* new = tmp; /* VOP_OPEN might change this */
	err = VOP_OPEN(&new, flags, current_cred());
	VOP_UNLOCK(tmp); /* Locked by vfs_lookup() */

	if (err) {
		VOP_RELEASE(tmp); /* Ref from vfs_lookup */
	} else {
		if (new != tmp)
			VOP_RELEASE(tmp); /* VOP_OPEN gave a different node, so drop the lookup ref */
		*out = new;
	}

	return err;
}

int vfs_close(struct vnode* vnode, int flags) {
	if (!vnode)
		return -EINVAL;
	VOP_LOCK(vnode);
	int err = VOP_CLOSE(vnode, flags, current_cred());
	VOP_UNLOCK(vnode);
	return err;
}

static int vfs_rdwr(struct vnode* vnode, struct uio* uio, int flags, enum uio_rw rw, size_t* count) {
	if (!vnode || !uio || uio->rw != rw || !count)
		return -EINVAL;
	if (vnode->type == VTYPE_DIR)
		return -EISDIR;

	const size_t start = uio->resid;
	VOP_LOCK(vnode);
	int err = (rw == UIO_READ) ? VOP_READ(vnode, uio, flags, current_cred()) : VOP_WRITE(vnode, uio, flags, current_cred());
	VOP_UNLOCK(vnode);

	*count = start - uio->resid;
	if (err && *count)
		err = 0;

	return err;
}

int vfs_readv(struct vnode* vnode, struct uio* uio, int flags, size_t* rcount) {
	return vfs_rdwr(vnode, uio, flags, UIO_READ, rcount);
}

int vfs_writev(struct vnode* vnode, struct uio* uio, int flags, size_t* wcount) {
	return vfs_rdwr(vnode, uio, flags, UIO_WRITE, wcount);
}

int vfs_read(struct vnode* vnode, void* buf, size_t count, off_t off, int flags, size_t* rcount) {
	if (!vnode)
		return -EINVAL;

	const struct iovec iovec = { .iov_base = buf, .iov_len = count };
	struct uio uio;
	int err = uio_init(&uio, &iovec, 1, off, UIO_SYSSPACE, UIO_READ);
	if (unlikely(err))
		return err;

	return vfs_readv(vnode, &uio, flags, rcount);
}

int vfs_write(struct vnode* vnode, const void* buf, size_t count, off_t off, int flags, size_t* wcount) {
	if (!vnode)
		return -EINVAL;

	/* buf is never modified with UIO_WRITE */
	const struct iovec iovec = { .iov_base = (void*)buf, .iov_len = count };
	struct uio uio;
	int err = uio_init(&uio, &iovec, 1, off, UIO_SYSSPACE, UIO_WRITE);
	if (err)
		return err;

	return vfs_writev(vnode, &uio, flags, wcount);
}

int vfs_link(struct vnode* dref, const char* dpath, struct vnode* lref, const char* lpath, enum vtype type, const struct vattr* attr) {
	if (type != VTYPE_LINK && type != VTYPE_REGULAR)
		return -EINVAL;

	/* When creating a link of type regular, this means a hardlink is being created. Otherwise a symbolic link is made */
	struct vnode* target_node = NULL;
	if (type == VTYPE_REGULAR) {
		int err = vfs_lookup(dref, dpath, NULL, 0, &target_node);
		if (err)
			return err;
		VOP_UNLOCK(target_node); /* The second vfs_lookup() could cause a deadlock if it resolves to the same vnode */
		if (target_node->type == VTYPE_DIR) {
			VOP_RELEASE(target_node);
			return -EPERM; /* Directories cannot be hardlinked */
		}
	}

	int err = -ENOMEM;
	char* comp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!comp)
		goto cleanup;

	struct vnode* parent_node;
	err = vfs_lookup(lref, lpath, comp, VFS_LOOKUP_PARENT, &parent_node);
	if (err)
		goto cleanup;

	if ((parent_node->flags & VFLAG_DEAD) || has_trailing_slash(lpath)) {
		err = -ENOENT;
	} else if (strcmp(comp, ".") == 0 || strcmp(comp, "..") == 0) {
		err = -EEXIST;
	} else if (target_node) {
		if (target_node->belongs_to == parent_node->belongs_to) {
			VOP_LOCK(target_node);
			err = VOP_LINK(parent_node, target_node, comp, current_cred());
			VOP_UNLOCK(target_node);
		} else {
			err = -EXDEV;
		}
	} else {
		err = VOP_SYMLINK(parent_node, comp, dpath, attr, current_cred());
	}

	VOP_UNLOCK(parent_node);
	VOP_RELEASE(parent_node);
cleanup:
	kfree(comp);
	if (target_node)
		VOP_RELEASE(target_node);
	return err;
}

int vfs_unlink(struct vnode* ref, const char* path) {
	return remove(ref, path, false);
}

int vfs_getdents(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* rcount) {
	if (!dir || !buf || !rcount)
		return -EINVAL;
	if (dir->type != VTYPE_DIR)
		return -ENOTDIR;

	VOP_LOCK(dir);

	int err = 0;
	if (dir->flags & VFLAG_DEAD)
		*rcount = 0;
	else
		err = VOP_GETDENTS(dir, buf, count, off, rcount);

	VOP_UNLOCK(dir);
	return err;
}

int vfs_ioctl(struct vnode* vnode, unsigned long req, void __user* arg) {
	if (!vnode)
		return -EINVAL;
	return VOP_IOCTL(vnode, req, arg, current_cred());
}

static void follow_mount(struct vnode** vnode) {
	bug((*vnode)->type != VTYPE_DIR);
	while (1) {
		struct mount* mnt = (*vnode)->un.mounted_here;
		if (!mnt)
			break;
		struct vnode* next = mnt->root;
		VOP_HOLD(next);
		VOP_UNLOCK(*vnode);
		VOP_RELEASE(*vnode);
		*vnode = next;
		VOP_LOCK(*vnode);
	}
}

int vfs_lookup(struct vnode* ref, const char* path, char* lastcomp, int flags, struct vnode** out) {
	if (!path || !out)
		return -EINVAL;
	if ((flags & VFS_LOOKUP_LINK_DEPTH_MASK) > MAX_LINK_DEPTH)
		return -ELOOP;

	size_t pathlen = strlen(path);
	if (pathlen == 0)
		return -ENOENT;
	if (pathlen > PATHNAME_MAX)
		return -ENAMETOOLONG;
	if (flags & VFS_LOOKUP_PARENT) {
		if (!lastcomp)
			return -EINVAL;
		strlcpy(lastcomp, ".", NAME_MAX + 1);
	}

	/* strtok_r strips trailing slashes */
	const bool trailing_slash = has_trailing_slash(path);

	struct proc* proc = current_proc();
	mutex_acquire(&proc->cwd_root_mtx);

	if (*path == '/') {
		ref = proc->root;
		while (*path == '/')
			path++;
	} else if (!ref) {
		ref = proc->cwd;
	}

	VOP_HOLD(ref);
	mutex_release(&proc->cwd_root_mtx);
	VOP_LOCK(ref);

	char* _path = kstrdup(path, MM_ZONE_NORMAL);
	if (!_path) {
		VOP_UNLOCK(ref);
		VOP_RELEASE(ref);
		return -ENOMEM;
	}

	int err = 0;
	char* saveptr = NULL;
	char* next_comp = (void*)-1;
	bool unlock_ref_on_err = true;
	for (char* comp = strtok_r(_path, "/", &saveptr); comp && next_comp; comp = next_comp) {
		if (strlen(comp) > NAME_MAX) {
			err = -ENAMETOOLONG;
			break;
		} else if (ref->type != VTYPE_DIR) {
			err = -ENOTDIR;
			break;
		} else if (!ref->ops || !ref->ops->lookup) {
			err = -ENOSYS;
			break;
		}
		next_comp = strtok_r(NULL, "/", &saveptr);

		const bool is_last = (next_comp == NULL);
		if (is_last && (flags & VFS_LOOKUP_PARENT)) {
			strlcpy(lastcomp, comp, NAME_MAX + 1);
			break;
		}

		if (strcmp(comp, ".") == 0)
			continue;
		bool is_dotdot = (strcmp(comp, "..") == 0);
		if (is_dotdot) {
			mutex_acquire(&proc->cwd_root_mtx);
			struct vnode* chroot = proc->root;
			mutex_release(&proc->cwd_root_mtx);
			if (ref == chroot)
				continue;

			while (ref->belongs_to && ref == ref->belongs_to->root && ref->belongs_to->covered) {
				struct vnode* next = ref->belongs_to->covered;
				VOP_HOLD(next);
				VOP_UNLOCK(ref);
				VOP_RELEASE(ref);
				ref = next;
				VOP_LOCK(ref);
			}
		}

		if (ref->flags & VFLAG_DEAD) {
			err = -ENOENT;
			break;
		}

		struct vnode* next;
		err = VOP_LOOKUP(ref, comp, &next, current_cred()); /* referenced, but not locked */
		if (err)
			break;

		/* only one vnode is locked at a time to avoid ABA deadlocks */
		VOP_UNLOCK(ref);
		unlock_ref_on_err = false;
		VOP_LOCK(next);

		if (!is_dotdot && next->type == VTYPE_DIR)
			follow_mount(&next);

		if (next->type == VTYPE_LINK) {
			if (is_last && (flags & VFS_LOOKUP_NOFOLLOW) && !trailing_slash) {
				VOP_RELEASE(ref);
				ref = next;
				break;
			}

			char* target;
			err = VOP_READLINK(next, &target, current_cred());
			VOP_UNLOCK(next);
			if (err) {
				VOP_RELEASE(next);
				break;
			}

			struct vnode* link_vnode;
			err = vfs_lookup(ref, target, NULL, (flags & VFS_LOOKUP_LINK_DEPTH_MASK) + 1, &link_vnode);
			VOP_FREELINK(next, target);
			VOP_RELEASE(next);
			if (err)
				break;

			VOP_RELEASE(ref);
			ref = link_vnode;
			if (is_last)
				break;

			/* Resume loop, symlink is now the target */
			unlock_ref_on_err = true;
			continue;
		}

		VOP_RELEASE(ref);
		ref = next;
		unlock_ref_on_err = true;
	}

	if (err) {
		if (unlock_ref_on_err)
			VOP_UNLOCK(ref);
		VOP_RELEASE(ref);
	} else {
		if (trailing_slash && ref->type != VTYPE_DIR && !(flags & VFS_LOOKUP_PARENT)) {
			err = -ENOTDIR;
			VOP_UNLOCK(ref);
			VOP_RELEASE(ref);
		} else {
			*out = ref;
		}
	}

	kfree(_path);
	return err;
}

int vfs_mount_root(const char* fs_type) {
	int err = vfs_mount(fs_type, NULL, "/", NULL);
	if (err)
		return err;

	struct vnode* root = &vfs_root;
	VOP_HOLD(root); /* follow_mount() expects the vnode with a ref */
	VOP_LOCK(root); /* follow_mount() also expects a locked vnode */
	follow_mount(&root);
	VOP_HOLD(root);

	struct proc* proc = current_proc();
	mutex_acquire(&proc->cwd_root_mtx);
	proc->root = root;
	proc->cwd = root;
	mutex_release(&proc->cwd_root_mtx);

	VOP_UNLOCK(root);
	return 0;
}

static void vfs_init(void) {
	fs_table = hashtable_create(16, sizeof(struct filesystem_type*));
	if (!fs_table)
		out_of_memory();

	struct proc* proc = current_proc();
	proc->root = &vfs_root;
	proc->cwd = &vfs_root;
	VOP_HOLD(proc->root);
	VOP_HOLD(proc->cwd);
}

INIT_TASK_DECLARE(heap_init_task, sched_init_task);
INIT_TASK_DEFINE(vfs_init_task, INIT_TASK_SCOPE_BSP, vfs_init, &heap_init_task, &sched_init_task);
