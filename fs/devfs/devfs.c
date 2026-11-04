#include <crater/vfs.h>
#include <crater/init.h>
#include <crater/printk.h>
#include <crater/device.h>
#include <crater/slab.h>
#include <crater/panic.h>

struct devfs_node {
	struct vnode vnode;
	struct vnode* physical;
	struct chrdev* chrdev;
};

static int devfs_create(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)dvp;
	(void)name;
	(void)attr;
	(void)out;
	(void)cred;
	return -ENOTDIR;
}

static int devfs_remove(struct vnode* dvp, struct vnode* vnode, const char* name, const struct cred* cred) {
	(void)dvp;
	(void)vnode;
	(void)name;
	(void)cred;
	return -ENOTDIR;
}

static inline struct chrdev* live_chrdev(struct vnode* vnode) {
	struct chrdev* chrdev = container_of(vnode, struct devfs_node, vnode)->chrdev;
	return atomic_load(&chrdev->dead) ? NULL : chrdev;
}

static int devfs_open(struct vnode** vnode, int flags, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(*vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->open ? chrdev->ops->open(chrdev, flags, cred) : 0;
}

static int devfs_close(struct vnode* vnode, int flags, const struct cred* cred) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	return dnode->chrdev->ops->close ? dnode->chrdev->ops->close(dnode->chrdev, flags, cred) : 0;
}

static int devfs_read(struct vnode* vnode, struct uio* uio, int ioflags, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->read ? chrdev->ops->read(chrdev, uio, ioflags, cred) : -EINVAL;
}

static int devfs_write(struct vnode* vnode, struct uio* uio, int ioflags, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->write ? chrdev->ops->write(chrdev, uio, ioflags, cred) : -EINVAL;
}

static int devfs_lookup(struct vnode* ref, const char* name, struct vnode** out, const struct cred* cred) {
	(void)ref;
	(void)name;
	(void)out;
	(void)cred;
	return -ENOTDIR;
}

static int devfs_sync(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static int devfs_getattr(struct vnode* vnode, struct vattr* out, const struct cred* cred) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	VOP_LOCK(dnode->physical);
	int err = VOP_GETATTR(dnode->physical, out, cred);
	VOP_UNLOCK(dnode->physical);
	return err;
}

static int devfs_setattr(struct vnode* vnode, const struct vattr* attr, int flags, const struct cred* cred) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	VOP_LOCK(dnode->physical);
	int err = VOP_SETATTR(dnode->physical, attr, flags, cred);
	VOP_UNLOCK(dnode->physical);
	return err;
}

static int devfs_link(struct vnode* dvp, struct vnode* svp, const char* name, const struct cred* cred) {
	(void)dvp;
	(void)svp;
	(void)name;
	(void)cred;
	return -ENOTDIR;
}

static int devfs_symlink(struct vnode* dvp, const char* name, const char* target, const struct vattr* attr, const struct cred* cred) {
	(void)dvp;
	(void)name;
	(void)target;
	(void)attr;
	(void)cred;
	return -ENOTDIR;
}

static void devfs_freelink(char* str) {
	(void)str;
}

static int devfs_getdents(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* out_rcount) {
	(void)dir;
	(void)buf;
	(void)count;
	(void)off;
	(void)out_rcount;
	return -ENOTDIR;
}

static int devfs_ioctl(struct vnode* vnode, unsigned long req, void __user* arg, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->ioctl ? chrdev->ops->ioctl(chrdev, req, arg, cred) : -ENOTTY;
}

/* Let devices do their own locking */

static int devfs_lock(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static int devfs_unlock(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static void devfs_inactive(struct vnode* vnode) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	chrdev_release(dnode->chrdev);
	VOP_RELEASE(dnode->physical);
	kfree(dnode);
}

static const struct vnode_ops devfs_node_ops = {
	.create = devfs_create,
	.mkdir = devfs_create,
	.mknod = devfs_create,
	.rmdir = devfs_remove,
	.open = devfs_open,
	.close = devfs_close,
	.read = devfs_read,
	.write = devfs_write,
	.lookup = devfs_lookup,
	.sync = devfs_sync,
	.getattr = devfs_getattr,
	.setattr = devfs_setattr,
	.link = devfs_link,
	.symlink = devfs_symlink,
	.unlink = devfs_remove,
	.freelink = devfs_freelink,
	.getdents = devfs_getdents,
	.ioctl = devfs_ioctl,
	.lock = devfs_lock,
	.unlock = devfs_unlock,
	.inactive = devfs_inactive
};

int devfs_get_node(struct vnode* physical, struct vnode** out, const struct cred* cred) {
	if (!physical || !out)
		return -EINVAL;
	if (physical->type == VTYPE_BLKDEV)
		return -ENXIO;
	if (physical->type != VTYPE_CHDEV)
		return -EINVAL;

	struct vattr physical_attrs;
	int err = VOP_GETATTR(physical, &physical_attrs, cred);
	if (err)
		return err;

	struct chrdev* chrdev;
	err = chrdev_get(physical_attrs.rdevmajor, physical_attrs.rdevminor, &chrdev);
	if (err)
		return err;

	struct devfs_node* dnode = kmalloc(sizeof(*dnode), MM_ZONE_NORMAL);
	if (!dnode) {
		chrdev_release(chrdev);
		return -ENOMEM;
	}

	VOP_INIT(&dnode->vnode, physical->type, &devfs_node_ops, 0, physical->belongs_to);
	VOP_HOLD(physical); /* vfs_open() drops the lookup ref after the swap */
	dnode->physical = physical;
	dnode->chrdev = chrdev;

	*out = &dnode->vnode;
	return 0;
}

struct devfs_mount {
	struct mount* tmpfs_mount;
	struct list_node link;
};

struct devfs_registered_device {
	char* path;
	int major, minor;
	enum vtype type;
	mode_t mode;
	gid_t gid;
	uid_t uid;
	struct {
		struct chrdev* chrdev;
	} un;
	struct list_node link;
};

static LIST_HEAD_DEFINE(devfs_mount_list);
static LIST_HEAD_DEFINE(registered_devices);
static MUTEX_DEFINE(devfs_mtx);

static int make_parent_directories(struct vnode* root, const char* path) {
	const char* last = strrchr(path, '/');
	if (!last)
		return 0;

	size_t len = (size_t)(last - path);
	char* buf = kmalloc(len + 1, MM_ZONE_NORMAL);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, path, len);
	buf[len] = '\0';

	const struct vattr attr = { .type = VTYPE_DIR, .mode = 0755, .uid = 0, .gid = 0 };
	int err = 0;
	for (size_t i = 1; i <= len; i++) {
		if (buf[i] != '/' && buf[i] != '\0')
			continue;
		if (buf[i - 1] == '/')
			continue;

		char tmp = buf[i];
		buf[i] = '\0';
		err = vfs_mkdir(root, buf, &attr, NULL);
		buf[i] = tmp;
		if (err && err != -EEXIST)
			break;

		err = 0;
	}

	kfree(buf);
	return err;
}

static void remove_parent_directories(struct vnode* root, char* path) {
	char* slash;
	while ((slash = strrchr(path, '/'))) {
		*slash = '\0';
		int err = vfs_rmdir(root, path);
		if (err) {
			if (unlikely(err != -ENOTEMPTY))
				printk(PRINTK_WARN "devfs: Failed to remove parent directories after unregister: %d\n", err);
			break;
		}
	}
}

static inline void add_one(struct devfs_mount* dmount, struct devfs_registered_device* device) {
	const struct vattr attrs = {
		.type = device->type,
		.mode = device->mode,
		.gid = device->gid,
		.uid = device->uid,
		.rdevmajor = device->major,
		.rdevminor = device->minor
	};

	int err = make_parent_directories(dmount->tmpfs_mount->root, device->path);
	if (err == 0)
		err = vfs_mknod(dmount->tmpfs_mount->root, device->path, &attrs, NULL);
	if (err)
		printk(PRINTK_WARN "devfs: Failed to make node %s: %d\n", device->path, err);
}

static inline void add_all(struct devfs_mount* dmount) {
	struct devfs_registered_device* pos;
	list_for_each_entry(pos, &registered_devices, link)
		add_one(dmount, pos);
}

static int register_device_locked(struct devfs_registered_device* device) {
	struct devfs_registered_device* it;
	list_for_each_entry(it, &registered_devices, link) {
		if (strcmp(it->path, device->path) == 0)
			return -EEXIST;
	}

	list_add(&registered_devices, &device->link);
	struct devfs_mount* pos;
	list_for_each_entry(pos, &devfs_mount_list, link)
		add_one(pos, device);

	return 0;
}

int devfs_register(const struct devfs_device_info* info, const char* path, mode_t mode) {
	if (!info || !path || !info->un.chrdev || (info->type != VTYPE_CHDEV && info->type != VTYPE_BLKDEV))
		return -EINVAL;

	while (*path == '/')
		path++;
	if (*path == '\0')
		return -EINVAL;
	if (path[strlen(path) - 1] == '/')
		return -EISDIR;

	struct devfs_registered_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->path = kstrdup(path, MM_ZONE_NORMAL);
	if (!device->path) {
		kfree(device);
		return -ENOMEM;
	}

	const struct cred* cred = current_cred();
	if (info->type == VTYPE_CHDEV) {
		device->major = info->un.chrdev->major;
		device->minor = info->un.chrdev->minor;
		device->type = VTYPE_CHDEV;
		device->mode = mode;
		device->gid = cred->gid;
		device->uid = cred->uid;
		device->un.chrdev = info->un.chrdev;
		list_node_init(&device->link);
	} else {
		bug("devfs_register(): VTYPE_BLKDEV unimplemented");
	}

	mutex_acquire(&devfs_mtx);
	int err = register_device_locked(device);
	if (err == 0)
		chrdev_hold(info->un.chrdev);
	mutex_release(&devfs_mtx);
	if (err) {
		kfree(device->path);
		kfree(device);
	}

	return err;
}

static int remove_one(struct devfs_mount* dmount, struct devfs_registered_device* device) {
	char* lastcomp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!lastcomp)
		return -ENOMEM;

	struct vnode* parent;
	struct vnode* root = dmount->tmpfs_mount->root;
	int err = vfs_lookup(root, device->path, lastcomp, VFS_LOOKUP_NOFOLLOW | VFS_LOOKUP_PARENT, &parent);
	if (err) {
		kfree(lastcomp);
		return err;
	}

	struct vnode* device_node;
	err = VOP_LOOKUP(parent, lastcomp, &device_node, current_cred()); /* Returns the vnode unlocked, unlike vfs_lookup() */
	if (err)
		goto out;

	struct vattr attrs;
	VOP_LOCK(device_node);

	err = VOP_GETATTR(device_node, &attrs, current_cred());
	if (err)
		goto out_device_node;

	if (attrs.type != device->type || attrs.rdevmajor != device->major || attrs.rdevminor != device->minor) {
		err = -ESTALE;
		goto out_device_node;
	}
	err = VOP_UNLINK(parent, device_node, lastcomp, current_cred());

out_device_node:
	VOP_UNLOCK(device_node);
	VOP_RELEASE(device_node);
out:
	VOP_UNLOCK(parent);
	VOP_RELEASE(parent);
	kfree(lastcomp);

	if (err == 0) {
		char* path = kstrdup(device->path, MM_ZONE_NORMAL);
		if (path) {
			remove_parent_directories(root, path);
			kfree(path);
		}
	}

	return err;
}

int devfs_unregister(const struct devfs_device_info* info) {
	if (!info || !info->un.chrdev || (info->type != VTYPE_CHDEV && info->type != VTYPE_BLKDEV))
		return -EINVAL;

	int err = -ENOENT;
	mutex_acquire(&devfs_mtx);

	struct devfs_registered_device* device, *tmp;
	list_for_each_entry_safe(device, tmp, &registered_devices, link) {
		if (device->type != info->type || device->un.chrdev != info->un.chrdev)
			continue;

		list_remove(&device->link);
		struct devfs_mount* pos;
		list_for_each_entry(pos, &devfs_mount_list, link) {
			err = remove_one(pos, device);
			if (err != 0 && err != -ENOENT && err != -ESTALE)
				printk(PRINTK_WARN "devfs: remove_one() failed: %d\n", err);
		}

		err = 0;

		chrdev_release(device->un.chrdev);
		kfree(device->path);
		kfree(device);
	}

	mutex_release(&devfs_mtx);
	return err;
}

static const struct filesystem_type* tmpfs_type;

static int devfs_mnt_mount(struct vnode* backing, void* data, struct mount** out) {
	struct devfs_mount* dmount = kmalloc(sizeof(*dmount), MM_ZONE_NORMAL);
	if (!dmount)
		return -ENOMEM;

	int err = tmpfs_type->mount(backing, data, &dmount->tmpfs_mount);
	if (err) {
		kfree(dmount);
		return err;
	}

	list_node_init(&dmount->link);
	mutex_acquire(&devfs_mtx);
	add_all(dmount);
	list_add(&devfs_mount_list, &dmount->link);
	mutex_release(&devfs_mtx);

	*out = dmount->tmpfs_mount;
	return 0;
}

static int devfs_mnt_unmount(struct mount* mount) {
	mutex_acquire(&devfs_mtx);

	struct devfs_mount* dmount = NULL;
	struct devfs_mount* tmp;
	list_for_each_entry(tmp, &devfs_mount_list, link) {
		if (tmp->tmpfs_mount == mount) {
			dmount = tmp;
			break;
		}
	}
	bug(dmount == NULL);

	int err = tmpfs_type->unmount(mount);
	if (err == 0) {
		list_remove(&dmount->link);
		kfree(dmount);
	}

	mutex_release(&devfs_mtx);
	return err;
}

static int devfs_mnt_sync(struct mount* mount) {
	return tmpfs_type->sync(mount);
}

static int devfs_mnt_statfs(struct mount* mount, struct fsattr* attr) {
	return tmpfs_type->statfs(mount, attr);
}

static const struct filesystem_type devfs_type = {
	.name = "devfs",
	.mount = devfs_mnt_mount,
	.unmount = devfs_mnt_unmount,
	.sync = devfs_mnt_sync,
	.statfs = devfs_mnt_statfs
};

static void devfs_init(void) {
	int err = vfs_get_filesystem_type("tmpfs", &tmpfs_type); 
	if (err) {
		printk(PRINTK_ERR "devfs: Failed to get tmpfs type: %d\n", err);
		return;
	}

	err = vfs_register(&devfs_type);
	if (err)
		printk(PRINTK_ERR "devfs: Failed to register type: %d\n", err);
}

INIT_TASK_DECLARE(vfs_init_task, tmpfs_init_task);
INIT_TASK_DEFINE(devfs_init_task, INIT_TASK_SCOPE_BSP, devfs_init, &vfs_init_task, &tmpfs_init_task);
