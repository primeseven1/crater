#include <crater/fd.h>
#include <crater/slab.h>
#include <crater/init.h>
#include <crater/proc.h>

static struct slab_cache* file_cache = NULL;

static void destroy_file(struct file* file) {
	if (file->vnode) {
		vfs_close(file->vnode, file_flags_to_vfs_flags(file->flags));
		VOP_RELEASE(file->vnode);
	}

	slab_cache_free(file_cache, file);
}

#define FILE_HOLD(f) atomic_add_fetch(&(f)->refcnt, 1)
#define FILE_RELEASE(f) \
	do { \
		if (atomic_sub_fetch(&(f)->refcnt, 1) == 0) \
			destroy_file(f); \
	} while (0)

struct fd* fd_create_table(void) {
	return kcalloc(FD_TABLE_ENTRY_COUNT, sizeof(struct fd), MM_ZONE_NORMAL);
}

void fd_destroy_table(struct fd* table) {
	if (table) {
		for (int i = 0; i < FD_TABLE_ENTRY_COUNT; i++) {
			if (table[i].file)
				FILE_RELEASE(table[i].file);
		}
		kfree(table);
	}
}

static struct file* alloc_file(void) {
	struct file* ret = slab_cache_alloc(file_cache);
	if (ret) {
		ret->vnode = NULL;
		ret->offset = 0;
		ret->flags = 0;
		mutex_init(&ret->offset_flags_mtx);
		atomic_store(&ret->refcnt, 1);
	}
	return ret;
}

struct file* fd_get(int fd) {
	if (fd < 0 || fd >= FD_TABLE_ENTRY_COUNT)
		return NULL;

	struct file* file = NULL;
	struct proc* proc = current_proc();
	if (proc->fd_table) {
		mutex_acquire(&proc->fd_mtx);
		file = proc->fd_table[fd].file;
		if (file)
			FILE_HOLD(file);
		mutex_release(&proc->fd_mtx);
	}

	return file;
}

void fd_release(struct file* file) {
	if (file)
		FILE_RELEASE(file);
}

int fd_get_flags(int fd, int* flags) {
	if (fd < 0 || fd >= FD_TABLE_ENTRY_COUNT)
		return -EBADF;

	int err = -EBADF;
	struct proc* proc = current_proc();
	if (proc->fd_table) {
		mutex_acquire(&proc->fd_mtx);
		if (proc->fd_table[fd].file) {
			*flags = proc->fd_table[fd].flags;
			err = 0;
		}
		mutex_release(&proc->fd_mtx);
	}

	return err;
}

int fd_set_flags(int fd, int flags) {
	if (fd < 0 || fd >= FD_TABLE_ENTRY_COUNT)
		return -EBADF;

	int err = -EBADF;
	struct proc* proc = current_proc();
	if (proc->fd_table) {
		mutex_acquire(&proc->fd_mtx);
		if (proc->fd_table[fd].file) {
			proc->fd_table[fd].flags = flags;
			err = 0;
		}
		mutex_release(&proc->fd_mtx);
	}

	return err;
}

static int find_free_fd(struct proc* proc) {
	int fd = -1;
	for (int i = 0; i < FD_TABLE_ENTRY_COUNT; i++) {
		if (!proc->fd_table[i].file) {
			fd = i;
			break;
		}
	}
	return fd;
}

static int __fd_new(struct proc* proc, struct vnode* vnode, int file_flags, int fd_flags, int* out_fd) {
	if (unlikely(!proc->fd_table))
		return -EMFILE;

	struct file* file = alloc_file();
	if (!file)
		return -ENOMEM;

	file->vnode = vnode;
	file->flags = file_flags;

	int err = 0;
	mutex_acquire(&proc->fd_mtx);

	int fd = find_free_fd(proc);
	if (fd >= 0) {
		VOP_HOLD(vnode);
		proc->fd_table[fd] = (struct fd){ .file = file, .flags = fd_flags };
	} else {
		err = -EMFILE;
	}

	mutex_release(&proc->fd_mtx);

	if (err == 0) {
		*out_fd = fd;
	} else {
		file->vnode = NULL; /* prevent destroy_file() from closing the vnode */
		destroy_file(file);
	}
	return err;
}

int fd_new(struct vnode* vnode, int file_flags, int fd_flags, int* out_fd) {
	return __fd_new(current_proc(), vnode, file_flags, fd_flags, out_fd);
}

int fd_close(int fd) {
	if (fd < 0 || fd >= FD_TABLE_ENTRY_COUNT)
		return -EBADF;

	struct proc* proc = current_proc();
	if (unlikely(!proc->fd_table))
		return -EBADF;

	mutex_acquire(&proc->fd_mtx);

	struct file* file = proc->fd_table[fd].file;
	if (file)
		proc->fd_table[fd] = (struct fd){ .file = NULL, .flags = 0 };

	mutex_release(&proc->fd_mtx);
	if (file)
		FILE_RELEASE(file);
	else
		return -EBADF;
	return 0;
}

void fd_clone(struct fd* table) {
	struct proc* proc = current_proc();
	if (unlikely(!proc->fd_table))
		return;

	mutex_acquire(&proc->fd_mtx);

	for (int i = 0; i < FD_TABLE_ENTRY_COUNT; i++) {
		if (proc->fd_table[i].file) {
			table[i] = proc->fd_table[i];
			FILE_HOLD(table[i].file);
		}
	}

	mutex_release(&proc->fd_mtx);
}

static int __fd_dup(struct proc* proc, int old_fd, int new_fd, int fd_flags, int* out_fd) {
	if (unlikely(!proc->fd_table))
		return -EBADF;

	/* A negative newfd will be ignored */
	if (old_fd < 0 || old_fd >= FD_TABLE_ENTRY_COUNT || new_fd >= FD_TABLE_ENTRY_COUNT)
		return -EBADF;

	struct file* old = NULL;
	int err = 0;

	mutex_acquire(&proc->fd_mtx);

	struct file* file = proc->fd_table[old_fd].file;
	if (!file) {
		err = -EBADF;
		goto out;
	}

	if (new_fd >= 0) {
		if (new_fd == old_fd) {
			*out_fd = new_fd;
			goto out;
		}

		old = proc->fd_table[new_fd].file;
		proc->fd_table[new_fd] = (struct fd){ .file = file, .flags = fd_flags };
		*out_fd = new_fd;
	} else {
		int fd = find_free_fd(proc);
		if (fd < 0) {
			err = -EMFILE;
			goto out;
		}
		proc->fd_table[fd] = (struct fd){ .file = file, .flags = fd_flags };
		*out_fd = fd;
	}

	FILE_HOLD(file);
out:
	mutex_release(&proc->fd_mtx);
	if (old)
		FILE_RELEASE(old);
	return err;
}

int fd_dup(int old_fd, int new_fd, int fd_flags, int* out_fd) {
	return __fd_dup(current_proc(), old_fd, new_fd, fd_flags, out_fd);
}

int fd_init_for_init(struct proc* init, struct vnode* console) {
	int fd;

	/* Create the stdin file descriptor */
	int err = __fd_new(init, console, FILE_READ | FILE_WRITE, 0, &fd);
	if (err) {
		bug(err != -ENOMEM);
		return err;
	}
	bug(fd != 0);

	/* Create stdout/stderr file descriptors */
	bug(__fd_dup(init, fd, 1, 0, &fd) != 0 || fd != 1);
	bug(__fd_dup(init, fd, 2, 0, &fd) != 0 || fd != 2);

	return 0;
}

static void fd_init(void) {
	file_cache = slab_cache_create(sizeof(struct file), alignof(struct file), MM_ZONE_NORMAL, NULL, NULL);
	if (!file_cache)
		out_of_memory();
}

INIT_TASK_DECLARE(zones_init_task);
INIT_TASK_DEFINE(fd_init_task, INIT_TASK_SCOPE_BSP, fd_init, &zones_init_task);
