#pragma once

#include <crater/time.h>
#include <crater/mutex.h>
#include <crater/cred.h>
#include <crater/errno.h>
#include <crater/uio.h>
#include <arch/posix.h>

#define PATHNAME_MAX 4095
#define MAX_LINK_DEPTH 24

#define VATTR_MODE_OTHERS_EXECUTE 00001
#define VATTR_MODE_OTHERS_SEARCH VATTR_MODE_OTHERS_EXECUTE
#define VATTR_MODE_OTHERS_WRITE 00002
#define VATTR_MODE_OTHERS_READ 00004
#define VATTR_MODE_OTHERS_ALL 00007
#define VATTR_MODE_GROUP_EXECUTE 00010
#define VATTR_MODE_GROUP_SEARCH VATTR_MODE_GROUP_EXECUTE
#define VATTR_MODE_GROUP_WRITE 00020
#define VATTR_MODE_GROUP_READ 00040
#define VATTR_MODE_GROUP_ALL 00070
#define VATTR_MODE_USER_EXECUTE 00100
#define VATTR_MODE_USER_SEARCH VATTR_MODE_USER_EXECUTE
#define VATTR_MODE_USER_WRITE 00200
#define VATTR_MODE_USER_READ 00400
#define VATTR_MODE_USER_ALL 00700
#define VATTR_MODE_STICKY 01000
#define VATTR_MODE_SGID 02000
#define VATTR_MODE_SUID 04000

#define VATTR_MODE (1 << 0)
#define VATTR_UID (1 << 1)
#define VATTR_GID (1 << 2)
#define VATTR_ATIME (1 << 3)
#define VATTR_MTIME (1 << 4)
#define VATTR_CTIME (1 << 5)
#define VATTR_SIZE (1 << 6)
#define VATTR_ALL (VATTR_MODE | VATTR_UID | VATTR_GID | VATTR_ATIME | VATTR_MTIME | VATTR_CTIME | VATTR_SIZE)

#define VFLAG_ROOT (1 << 0) /* Root directory of the file system */
#define VFLAG_DEAD (1 << 1) /* Only applies to directories, this means a directory was removed */

#define VFS_LOOKUP_LINK_DEPTH_MASK 0xFF
#define VFS_LOOKUP_PARENT (1 << 8)
#define VFS_LOOKUP_NOFOLLOW (1 << 9)

struct vnode;
struct mount;

enum vtype {
	VTYPE_REGULAR,
	VTYPE_DIR,
	VTYPE_CHDEV,
	VTYPE_BLKDEV,
	VTYPE_FIFO,
	VTYPE_LINK,
	VTYPE_SOCKET
};

struct fsattr {
	size_t io_size;
	size_t block_size, block_count;
	size_t free_blocks, free_blocks_unprivileged;
	size_t inode_count, free_inode_count, free_inode_count_unprivileged;
	unsigned long fsid;
	unsigned long flags;
	size_t max_name_size;
};

struct filesystem_type {
	const char* name;
	int (*mount)(struct vnode* backing, void* data, struct mount** out_mnt);
	int (*unmount)(struct mount*);
	int (*sync)(struct mount*);
	int (*statfs)(struct mount*, struct fsattr* out);
};

struct mount {
	const struct filesystem_type* fs_type;
	struct vnode* root; /* Root of the file system */
	struct vnode* covered; /* The vnode in the parent file system this is mounted on */
	struct list_node link; /* For global mount point list */
	long __atomic refcnt; /* Number of open file handles/number of mount points */
};

struct vattr {
	enum vtype type;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	int fsid;
	ino_t inode;
	int nlinks;
	size_t size, fsblocksize;
	struct timespec atime, mtime, ctime;
	int rdevmajor, rdevminor;
	int devmajor, devminor;
	size_t blocksused;
};

struct vnode_ops {
	int (*create)(struct vnode* dvp, const char*, const struct vattr*, struct vnode** out, const struct cred*); /* create a regular file, out might be null */
	int (*mkdir)(struct vnode* dvp, const char*, const struct vattr*, struct vnode** out, const struct cred*); /* create a directory, out might be null */
	int (*mknod)(struct vnode* dvp, const char*, const struct vattr*, struct vnode** out, const struct cred*); /* make a device node, out might be null */
	int (*rmdir)(struct vnode* dvp, struct vnode*, const char* name, const struct cred*); /* remove a directory */
	int (*open)(struct vnode**, int, const struct cred*); /* open a vnode, can be replaced, callers are expected to handle the replacement */
	int (*close)(struct vnode*, int, const struct cred*); /* close a vnode */
	int (*read)(struct vnode*, struct uio* uio, int ioflags, const struct cred*); /* read a file */
	int (*write)(struct vnode*, struct uio* uio, int ioflags, const struct cred*); /* write to a file */
	int (*lookup)(struct vnode* ref, const char*, struct vnode** out, const struct cred*); /* look up a single entry in a directory, expected to return the vnode unlocked but referenced */
	int (*sync)(struct vnode*); /* Sync a vnode to disk */
	int (*getattr)(struct vnode*, struct vattr* out, const struct cred*); /* Get a vnode's attributes */
	int (*setattr)(struct vnode*, const struct vattr*, int aflags, const struct cred*); /* Set a vnode's attributes */
	int (*link)(struct vnode* dvp, struct vnode* svp, const char* name, const struct cred*); /* Create a hardlink */
	int (*symlink)(struct vnode* dvp, const char* name, const char* target, const struct vattr*, const struct cred*); /* Create a symbolic link */
	int (*unlink)(struct vnode* dvp, struct vnode* child, const char* name, const struct cred*); /* Remove a file */
	int (*readlink)(struct vnode*, char** out, const struct cred*); /* Read a symbolic link */
	void (*freelink)(char* str); /* Free a symbolic link string */
	int (*getdents)(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* out_rcount); /* Get directory entries */
	int (*ioctl)(struct vnode*, unsigned long, void __user*, const struct cred*); /* For device vnodes, regular filesystems don't need this, even for nodes created with mknod() */
	int (*lock)(struct vnode*); /* if NULL, vnode->mtx is acquired by default */
	int (*unlock)(struct vnode*); /* if NULL, vnode->mtx is released by default */
	void (*inactive)(struct vnode*); /* Called when a vnode's refcount goes to zero */
};

struct vnode {
	enum vtype type;
	const struct vnode_ops* ops;
	int flags;
	struct mount* belongs_to;
	union {
		struct mount* mounted_here; /* VTYPE_DIR */
	} un;
	long __atomic refcnt;
	mutex_t mtx;
};

#define __VOP(op, vp, ...) \
	({ \
		struct vnode* const ____vp = (vp); \
		____vp->ops->op ? ____vp->ops->op(____vp, ##__VA_ARGS__) : -ENOSYS; \
	})

#define VOP_INIT(vp, t, o, f, m) \
	do { \
		(vp)->type = t; \
		(vp)->ops = o; \
		(vp)->flags = f; \
		(vp)->belongs_to = m; \
		__builtin_memset(&(vp)->un, 0, sizeof((vp)->un)); \
		atomic_store_explicit(&(vp)->refcnt, 1, memory_order_relaxed); \
		mutex_init(&(vp)->mtx); \
	} while (0)

#define VOP_CREATE(dvp, name, vap, out, cr) __VOP(create, dvp, name, vap, out, cr)
#define VOP_MKDIR(dvp, name, vap, out, cr) __VOP(mkdir, dvp, name, vap, out, cr)
#define VOP_MKNOD(dvp, name, vap, out, cr) __VOP(mknod, dvp, name, vap, out, cr)
#define VOP_RMDIR(dvp, vp, name, cr) __VOP(rmdir, dvp, vp, name, cr)
#define VOP_OPEN(vpp, flags, cr) \
	({ \
		struct vnode** const ____vpp = (vpp); \
		(*____vpp)->ops->open ? (*____vpp)->ops->open(____vpp, flags, cr) : -ENOSYS; \
	})
#define VOP_CLOSE(vp, flags, cr) __VOP(close, vp, flags, cr)
#define VOP_READ(vp, uio, flags, cr) __VOP(read, vp, uio, flags, cr)
#define VOP_WRITE(vp, uio, flags, cr) __VOP(write, vp, uio, flags, cr)
#define VOP_LOOKUP(ref, name, out, cr) __VOP(lookup, ref, name, out, cr)
#define VOP_SYNC(vp) __VOP(sync, vp)
#define VOP_GETATTR(vp, vap, cr) __VOP(getattr, vp, vap, cr)
#define VOP_SETATTR(vp, vap, flags, cr) __VOP(setattr, vp, vap, flags, cr)
#define VOP_LINK(dvp, svp, name, cr) __VOP(link, dvp, svp, name, cr)
#define VOP_SYMLINK(dvp, name, target, attr, cr) __VOP(symlink, dvp, name, target, attr, cr)
#define VOP_UNLINK(dvp, child, name, cr) __VOP(unlink, dvp, child, name, cr)
#define VOP_READLINK(vp, out, cr) __VOP(readlink, vp, out, cr)
#define VOP_FREELINK(vp, str) (vp)->ops->freelink(str)
#define VOP_GETDENTS(vp, buf, count, off, rc) __VOP(getdents, vp, buf, count, off, rc)
#define VOP_IOCTL(vp, req, arg, cr) \
	({ \
		struct vnode* const ____vp = (vp); \
		____vp->ops->ioctl ? ____vp->ops->ioctl(____vp, req, arg, cr) : -ENOTTY; \
	})
#define VOP_LOCK(vp) \
	({ \
		struct vnode* const ____vp = (vp); \
		____vp->ops->lock ? ____vp->ops->lock(____vp) : (mutex_acquire(&(____vp)->mtx), 0); \
	})
#define VOP_UNLOCK(vp) \
	({ \
		struct vnode* const ____vp = (vp); \
		____vp->ops->unlock ? ____vp->ops->unlock(____vp) : (mutex_release(&(____vp)->mtx), 0); \
	})
#define VOP_HOLD(v) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(v), struct vnode*), "__builtin_types_compatible_p(v, struct vnode*)"); \
		atomic_add_fetch_explicit(&(v)->refcnt, 1, memory_order_relaxed); \
	} while (0)
#define VOP_RELEASE(v) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(v), struct vnode*), "__builtin_types_compatible_p(v, struct vnode*)"); \
		if (atomic_sub_fetch_explicit(&(v)->refcnt, 1, memory_order_acq_rel) == 0) \
			(v)->ops->inactive(v); \
	} while (0)

/**
 * @brief Register a file system type
 * @param fs_type File system type to register
 * @retval -EEXIST File system type by name already exists
 * @retval -ENOMEM Out of memory
 * @retval 0 Successful
 */
int vfs_register(const struct filesystem_type* fs_type);

/**
 * @brief Get a file system type by name
 *
 * @param[in] name The name of the file system
 * @param[out] out The returned file system type
 *
 * @retval 0 Successful
 * @retval -ENODEV File system type is not registered
 */
int vfs_get_filesystem_type(const char* name, const struct filesystem_type** out);

/**
 * @brief Mount a file system
 *
 * @param fs_name File system name
 * @param ref Reference vnode, expected to be unlocked
 * @param mp Mount point
 * @param backing Backing vnode (eg. /dev/sda)
 *
 * @retval -ENODEV fs_name does not exist
 * @retval 0 Successful
 */
int vfs_mount(const char* fs_name, struct vnode* ref, const char* mp, struct vnode* backing);

/**
 * @brief Create a regular vnode
 *
 * @param[in] ref Reference for lookup
 * @param[in] path Path to the file
 * @param[in] attr File attributes
 * @param[out] out Where the created vnode is stored, optional
 *
 * @retval 0 Successful
 */ 
int vfs_create(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out);

/**
 * @brief Create a directory vnode
 *
 * @param[in] ref Reference for lookup
 * @param[in] path Path to where the directory will be made
 * @param[in] attr Directory attributes
 * @param[out] out Where the created vnode is stored, optional
 *
 * @retval 0 Successful
 * @retval -ENOENT Lookup failed
 * @retval -EEXIST Directory already exists
 */
int vfs_mkdir(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out);

/**
 * @brief Create a device/fifo node
 *
 * The type of node to create is based on attr->type. The device major/minor numbers are
 * from attr->rdevmajor and attr->rdevminor.
 *
 * @param ref The reference for lookup
 * @param path Path to the file
 * @param attr File attributes
 * @param out Where the created vnode is stored, optional
 *
 * @retval 0 Successful
 * @retval -ENOENT Directory does not exist
 * @retval -EEXIST File exists
 * @retval -EINVAL Attr is NULL, or attr->type is not VTYPE_BLKDEV/VTYPE_CHDEV/VTYPE_FIFO
 */
int vfs_mknod(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out);

/**
 * @brief Remove a directory vnode
 *
 * @param ref Reference for lookup
 * @param path Path to the directory
 *
 * @retval 0 Successful
 * @retval -ENOTEMPTY Directory not empty
 * @retval -ENOENT Lookup failed
 * @retval -ENOTDIR Not a directory
 */
int vfs_rmdir(struct vnode* ref, const char* path);

/**
 * @brief Open a vnode
 *
 * Combines vfs_lookup() and VOP_OPEN()
 *
 * @param[in] ref Reference for the path
 * @param[in] path Path to the file
 * @param[in] flags Open flags
 * @param[out] out Where to store the opened vnode
 *
 * @return Look at vfs_lookup() for details
 */
int vfs_open(struct vnode* ref, const char* path, int flags, struct vnode** out);

/**
 * @brief Close a vnode
 *
 * @param vnode The vnode to close
 * @param flags Close flags
 *
 * @return -errno on failure, 0 on success
 */
int vfs_close(struct vnode* vnode, int flags);

/**
 * @brief Read a file
 *
 * @param[in] vnode The vnode to read from
 * @param[in] uio The uio struct
 * @param[in] flags Read flags
 * @param[out] rcount The number of bytes read
 *
 * @retval 0 Successful
 */
int vfs_readv(struct vnode* vnode, struct uio* uio, int flags, size_t* rcount);

/**
 * @brief Write to a file
 *
 * @param[in] vnode The vnode to write to
 * @param[in] uio The uio struct
 * @param[in] flags Write flags
 * @param[out] wcount The number of bytes written
 *
 * @retval 0 Successful
 */
int vfs_writev(struct vnode* vnode, struct uio* uio, int flags, size_t* wcount);

/**
 * @brief Read a file
 *
 * @param[in] vnode The vnode to read from
 * @param[out] buf Where to store the file data
 * @param[in] count The number of bytes to read
 * @param[in] off Offset into the file
 * @param[in] flags Read flags
 * @param[out] rcount Actual number of bytes read
 *
 * @retval 0 Successful
 */
int vfs_read(struct vnode* vnode, void* buf, size_t count, off_t off, int flags, size_t* rcount);

/**
 * @brief Write to a file
 *
 * @param[in] vnode The vnode to write to
 * @param[in] buf The buffer to write
 * @param[in] count The number of bytes to write
 * @param[in] off Offset into the file
 * @param[in] flags flags
 * @param[out] wcount Pointer to store the write count
 *
 * @retval 0 Successful
 */
int vfs_write(struct vnode* vnode, const void* buf, size_t count, off_t off, int flags, size_t* wcount);

/**
 * @brief Create a symbolic link or hardlink
 *
 * @param dref Destination path reference for lookup
 * @param dpath Destination path for existing target
 * @param lref Link reference for lookup
 * @param lpath Path to where the new link will be created
 * @param type Type of link (VTYPE_REGULAR for hardlink, VTYPE_LINK for symbolic link)
 * @param attr File attributes, ignored for hardlinks
 *
 * @retval -EPERM Trying to hardlink a directory
 * @retval 0 Successful
 */
int vfs_link(struct vnode* dref, const char* dpath, struct vnode* lref, const char* lpath, enum vtype type, const struct vattr* attr);

/**
 * @brief Unlink a file
 *
 * @param ref Reference for the lookup
 * @param path Path to the file
 *
 * @retval 0 Successful
 * @retval -ENOENT File does not exist
 * @retval -EISDIR File path points to a directory (use rmdir instead)
 */
int vfs_unlink(struct vnode* ref, const char* path);

/**
 * @brief Get the directory entries for a vnode
 *
 * @param[in] dir The directory
 * @param[out] buf Where to store the directory entries
 * @param[in] count The number of entries to report
 * @param[in] off The offset
 * @param[out] rcount Number of directory entries written
 *
 * @retval -ENOTDIR Not a directory
 * @retval -EINVAL buf is NULL, or rcount is NULL
 * @retval 0 Successful
 */
int vfs_getdents(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* rcount);

/**
 * @brief Send a device-specific request to a vnode
 *
 * @param vnode The vnode
 * @param req The request number
 * @param arg The argument
 *
 * @retval -ENOTTY The vnode doesn't support the request
 * @return Whatever the driver returns, -errno on failure
 */
int vfs_ioctl(struct vnode* vnode, unsigned long req, void __user* arg);

/**
 * @brief Resolve a file system path to a vnode
 *
 * The vnode returned is referenced and locked.
 *
 * @param[in] ref The reference vnode, expected to be unlocked
 * @param[in] path The path to look up
 * @param[out] lastcomp Pointer to a last component buffer when looking up a parent vnode, buffer must be NAME_MAX + 1 in size or bigger
 * @param[in] flags VFS_LOOKUP_* flags
 * @param[out] out Where the resolved vnode is stored
 *
 * @retval -EINVAL path is NULL, or out is NULL, or lastcomp is NULL and VFS_LOOKUP_PARENT is used
 * @retval -ENOENT Not found
 * @retval -ENOSYS File system driver does not implement the required functions
 * @retval -ENOTDIR Component in the middle of the path does not resolve to a directory vnode, or ref is not a directory
 * @retval -ENAMETOOLONG Path length is more than PATHNAME_MAX, or a component in the path is longer than NAME_MAX
 * @retval -ELOOP Too many symbolic links
 * @retval 0 Successful
 */
int vfs_lookup(struct vnode* ref, const char* path, char* lastcomp, int flags, struct vnode** out);

/**
 * @brief Mount the root file system
 * @param fs_name Name of the file system
 */
int vfs_mount_root(const char* fs_name);

static inline unsigned char vfs_posix_type(enum vtype type) {
	switch (type) {
	case VTYPE_REGULAR:
		return DT_REG;
	case VTYPE_DIR:
		return DT_DIR;
	case VTYPE_CHDEV:
		return DT_CHR;
	case VTYPE_BLKDEV:
		return DT_BLK;
	case VTYPE_FIFO:
		return DT_FIFO;
	case VTYPE_LINK:
		return DT_LNK;
	case VTYPE_SOCKET:
		return DT_SOCK;
	}
	return DT_UNKNOWN;
}
