#pragma once

#include <crater/vfs.h>
#include <crater/slab.h>

#define FD_TABLE_ENTRY_COUNT 1024

struct proc; /* Avoid a recursive include */

struct file {
	struct vnode* vnode;
	off_t offset;
	int flags;
	mutex_t offset_flags_mtx;
	long __atomic refcnt;
};

struct fd {
	struct file* file;
	int flags;
};

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
