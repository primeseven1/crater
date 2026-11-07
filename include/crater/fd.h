#pragma once

#include <crater/vfs.h>
#include <crater/slab.h>

#define FD_TABLE_ENTRY_COUNT 1024

struct proc; /* Avoid a recursive include */

struct file {
	struct vnode* vnode;
	off_t offset;
	int flags; /* FILE_* flags */
	mutex_t offset_flags_mtx;
	long __atomic refcnt;
};

struct fd {
	struct file* file;
	int flags; /* FD_* flags (defined in posix.h) */
};

#define FILE_READ (1 << 0)
#define FILE_WRITE (1 << 1)
#define FILE_APPEND (1 << 2)
#define FILE_NONBLOCKING (1 << 3)
#define FILE_DIRECT (1 << 4)
#define FILE_SYNC (1 << 5)
#define FILE_DSYNC (1 << 6)

/**
 * @brief Create a new fd table
 *
 * The table returned has FD_TABLE_ENTRY_COUNT entries.
 *
 * @return The table
 */
struct fd* fd_create_table(void);

/**
 * @brief Destroy a fd table
 * @param table The table to destroy
 */
void fd_destroy_table(struct fd* table);

/**
 * @brief Get a file in a process fd table
 * @param fd The file descriptor
 * @return NULL if there is no file associated with the FD
 */
struct file* fd_get(int fd);

/**
 * @brief Get a file descriptor's flags
 *
 * @param fd The file descriptor
 * @param flags The flags
 *
 * @retval -EBADF Invalid file descriptor
 * @retval 0 Successful
 */
int fd_get_flags(int fd, int* flags);

/**
 * @brief Set a file descriptor's flags
 *
 * @param fd The file descriptor
 * @param flags The flags
 *
 * @retval -EBADF Bad file descriptor
 * @retval 0 Successful
 */
int fd_set_flags(int fd, int flags);

/**
 * @brief Release the reference obtained by fd_get()
 * @param file The file to release
 */
void fd_release(struct file* file);

/**
 * @brief Create a new fd
 *
 * @param[in] vnode The vnode
 * @param[in] file_flags The file flags
 * @param[in] fd_flags The file descriptor flags
 * @param[out] out_fd Where to store the fd
 *
 * @retval -ENOMEM Out of memory
 * @retval -EMFILE Too many file descriptors open
 * @retval 0 Successful
 */
int fd_new(struct vnode* vnode, int file_flags, int fd_flags, int* out_fd);

/**
 * @brief Close a fd
 * @param fd The fd to close
 * @retval -EBADF No file associated with the fd
 * @retval 0 Successful
 */
int fd_close(int fd);

/**
 * @brief Clone the current process fd table into another fd table
 * @param table The table, this is expected to be allocated by fd_create_table().
 */
void fd_clone(struct fd* table);

/**
 * @brief Duplicate an open file descriptor
 *
 * @param old_fd The old file descriptor
 * @param new_fd The file descriptor to assocate old_fd with. Ignored if < 0.
 * @param flags The flags for the new file descriptor
 * @param out_fd Where the new file descriptor will be stored
 *
 * @retval -EBADF Bad old file descriptor, or the new file descriptor is >= FD_TABLE_ENTRY_COUNT
 * @retval -EMFILE Too many open file descriptors
 * @retval 0 Successful
 */
int fd_dup(int old_fd, int new_fd, int fd_flags, int* out_fd);

/**
 * @brief Initialize the fd table for the init process (PID1)
 *
 * @param init The init process
 * @param console The console device for file descriptors 0, 1, and 2
 *
 * @retval -ENOMEM Out of memory
 * @retval 0 Successful
 */
int fd_init_for_init(struct proc* init, struct vnode* console);

static inline bool fd_open_flags_valid(int oflags) {
	const int known = O_ACCMODE | O_NOCTTY | O_APPEND | O_NONBLOCK | O_SYNC | O_DSYNC | O_DIRECT |
		O_CREAT | O_EXCL | O_TRUNC | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_LARGEFILE;
	if (oflags & ~known)
		return false;
	const int accmode = oflags & O_ACCMODE;
	return (accmode == O_RDONLY || accmode == O_WRONLY || accmode == O_RDWR);
}

static inline int file_flags_to_vfs_flags(int file_flags) {
	int vfs_flags = 0;
	if (file_flags & FILE_NONBLOCKING)
		vfs_flags |= VFS_FLAG_NONBLOCKING;
	if (file_flags & (FILE_DIRECT | FILE_SYNC | FILE_DSYNC))
		vfs_flags |= VFS_FLAG_NOCACHE;
	return vfs_flags;
}
