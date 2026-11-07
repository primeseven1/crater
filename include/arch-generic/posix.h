#pragma once

#include <crater/types.h>
#include <crater/compiler.h>

#define NAME_MAX 255
#define IOV_MAX 1024

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_ACCMODE 03
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_ASYNC 020000
#define O_DIRECT 040000
#define O_LARGEFILE 0100000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_NOATIME 01000000
#define O_CLOEXEC 02000000
#define O_SYNC 04010000
#define O_PATH 010000000
#define O_TMPFILE 020200000

#define FD_CLOEXEC 1

typedef int pid_t;
typedef int tid_t;
typedef int gid_t;
typedef int uid_t;
typedef unsigned int mode_t;
typedef u64 ino_t;
typedef long off_t;
typedef u64 dev_t;
typedef unsigned long nlink_t;
typedef long blksize_t;
typedef u64 blkcnt_t;
typedef long time_t;
typedef long suseconds_t;

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

struct timeval {
	time_t tv_sec;
	suseconds_t tv_usec;
};

struct stat {
	dev_t dev;
	ino_t ino;
	nlink_t nlink;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	unsigned int __pad0;
	dev_t rdev;
	off_t size;
	blksize_t blksize;
	blkcnt_t blocks;
	struct timespec atim, mtim, ctim;
	long __unused[3];
};

struct dirent {
	ino_t ino;
	off_t off;
	unsigned short reclen;
	unsigned char type;
	char name[NAME_MAX + 1];
};

struct iovec {
	union {
		void* iov_base;
		void __user* iov_base_user;
	};
	size_t iov_len;
};
