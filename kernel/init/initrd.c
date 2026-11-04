#include <crater/slab.h>
#include <crater/vfs.h>
#include <crater/printk.h>
#include <crater/panic.h>
#include <crater/string.h>
#include <crater/limine.h>
#include <arch/posix.h>
#include "internal.h"

#define USTAR_BLOCK_SIZE 512

enum ustar_type {
	USTAR_FILE,
	USTAR_HARDLINK,
	USTAR_SYMLINK,
	USTAR_CHARDEV,
	USTAR_BLOCK,
	USTAR_DIR,
	USTAR_FIFO
};

struct ustar_header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char modtime[12];
	char checksum[8];
	char type_flag[1];
	char link[100];
	char indicator[6];
	char version[2];
	char uname[32];
	char gname[32];
	char dev_major[8];
	char dev_minor[8];
	char prefix[155];
};

struct ustar_entry {
	char name[PATHNAME_MAX + 1];
	mode_t mode;
	uid_t uid;
	gid_t gid;
	size_t size;
	struct timespec modtime;
	u16 checksum;
	enum ustar_type type_flag;
	char link[101];
	char indicator[6];
	char version[2];
	u32 dev_minor;
	u32 dev_major;
};

static enum vtype ustar_type_to_vtype(enum ustar_type type) {
	switch (type) {
	case USTAR_FILE:
	case USTAR_HARDLINK: /* vfs_link() uses VTYPE_REGULAR to create a hardlink */
		return VTYPE_REGULAR;
	case USTAR_SYMLINK:
		return VTYPE_LINK;
	case USTAR_CHARDEV:
		return VTYPE_CHDEV;
	case USTAR_BLOCK:
		return VTYPE_BLKDEV;
	case USTAR_DIR:
		return VTYPE_DIR;
	case USTAR_FIFO:
		return VTYPE_FIFO;
	}
	return VTYPE_REGULAR;
}

static const char* ustar_type_to_string(enum ustar_type type) {
	switch (type) {
	case USTAR_FILE:
		return "USTAR_FILE";
	case USTAR_HARDLINK:
		return "USTAR_HARDLINK";
	case USTAR_SYMLINK:
		return "USTAR_SYMLINK";
	case USTAR_CHARDEV:
		return "USTAR_CHARDEV";
	case USTAR_BLOCK:
		return "USTAR_BLOCK";
	case USTAR_DIR:
		return "USTAR_DIR";
	case USTAR_FIFO:
		return "USTAR_FIFO";
	}
	return "unknown";
}

static unsigned long long parse_integer(const char* str, size_t len) {
	unsigned long long res = 0;
	while (len--) {
		res *= 8;
		res += *str - '0';
		str++;
	}
	return res;
}

static void build_entry(struct ustar_entry* entry, const void* addr) {
	const struct ustar_header* header = addr;

	entry->name[0] = '\0';
	if (header->prefix[0]) {
		strlcpy(entry->name, header->prefix, sizeof(entry->name));
		strlcat(entry->name, "/", sizeof(entry->name));
	}
	strlcat(entry->name, header->name, sizeof(entry->name));

	entry->mode = parse_integer(header->mode, sizeof(header->mode) - 1);
	entry->uid = parse_integer(header->uid, sizeof(header->uid) - 1);
	entry->gid = parse_integer(header->gid, sizeof(header->gid) - 1);
	entry->size = parse_integer(header->size, sizeof(header->size) - 1);
	entry->modtime.tv_sec = parse_integer(header->modtime, sizeof(header->modtime) - 1);
	entry->modtime.tv_nsec = 0;
	entry->checksum = parse_integer(header->checksum, sizeof(header->checksum) - 1);
	entry->type_flag = parse_integer(header->type_flag, sizeof(header->type_flag));
	entry->dev_major = parse_integer(header->dev_major, sizeof(header->dev_major) - 1);
	entry->dev_minor = parse_integer(header->dev_minor, sizeof(header->dev_minor) - 1);

	strlcpy(entry->link, header->link, sizeof(entry->link));
	memcpy(entry->indicator, header->indicator, sizeof(entry->indicator));
	memcpy(entry->version, header->version, sizeof(entry->version));
}

static int do_file(const struct ustar_entry* entry, const void* data, const struct vattr* attr) {
	struct vnode* vn;
	int err = vfs_create(NULL, entry->name, attr, &vn);
	if (err)
		return err;

	struct vnode* tmp = vn;
	VOP_LOCK(vn);
	err = VOP_OPEN(&vn, 0, current_cred());
	VOP_UNLOCK(vn);
	if (err) {
		VOP_RELEASE(vn);
		return err;
	}
	bug(tmp != vn);

	size_t write_count;
	err = vfs_write(vn, data, entry->size, 0, 0, &write_count);
	if (unlikely(err == 0 && write_count != entry->size))
		err = -EIO;

	VOP_LOCK(vn);
	VOP_CLOSE(vn, 0, current_cred());
	VOP_UNLOCK(vn);

	VOP_RELEASE(vn);
	return err;
}

static void handle_entry(struct ustar_entry* entry, const void* data) {
	const struct vattr attr = {
		.type = ustar_type_to_vtype(entry->type_flag),
		.gid = entry->gid, .uid = entry->uid, .mode = entry->mode,
		.rdevmajor = entry->dev_major, .rdevminor = entry->dev_minor
	};

	int err;
	switch (entry->type_flag) {
	case USTAR_FILE:
		err = do_file(entry, data, &attr);
		break;
	case USTAR_HARDLINK:
		err = vfs_link(NULL, entry->link, NULL, entry->name, VTYPE_REGULAR, &attr);
		break;
	case USTAR_SYMLINK:
		err = vfs_link(NULL, entry->link, NULL, entry->name, VTYPE_LINK, &attr);
		break;
	case USTAR_BLOCK:
	case USTAR_CHARDEV:
		err = vfs_mknod(NULL, entry->name, &attr, NULL);
		break;
	case USTAR_DIR:
		err = vfs_mkdir(NULL, entry->name, &attr, NULL);
		if (err == -EEXIST)
			err = 0; /* This can happen when attempting to create /dev */
		break;
	default:
		err = -ENOSYS;
		break;
	}

	if (err)
		printk(PRINTK_ERR "initrd: Failed to unpack %s of type %s: %d\n", entry->name, ustar_type_to_string(entry->type_flag), err);
}

static struct limine_internal_module _initrd_mod = {
	.flags = 0,
	.path = "/initrd",
	.string = "initrd-default"
};
static struct limine_internal_module* initrd_mod[] =  { &_initrd_mod };
static volatile struct limine_module_request __limine_request mod_request = {
	.request.id = LIMINE_MODULE_REQUEST,
	.request.revision = 1,
	.response = NULL,
	.internal_modules = initrd_mod,
	.internal_module_count = ARRAY_SIZE(initrd_mod)
};

static struct limine_file* find_initrd(void) {
	struct limine_module_response* response = mod_request.response;
	if (!response)
		return NULL;

	struct limine_file* initrd = NULL;
	for (u64 i = 0; i < response->module_count; ++i) {
		/* initrd-default can be overridden by a user provided module */
		if (strcmp(response->modules[i]->string, "initrd-default") == 0) {
			initrd = response->modules[i];
		} else if (strcmp(response->modules[i]->string, "initrd") == 0) {
			initrd = response->modules[i];
			break;
		}
	}

	return initrd;
}

void initrd_init(void) {
	struct limine_file* initrd = find_initrd();
	if (!initrd) {
		printk(PRINTK_WARN "initrd: No initrd found\n");
		return;
	}

	struct ustar_entry* entry = kmalloc(sizeof(*entry), MM_ZONE_NORMAL);
	if (unlikely(!entry)) {
		printk("initrd: Out of memory\n");
		return;
	}

	const void* _entry = initrd->address;
	while (1) {
		build_entry(entry, _entry);
		if (strncmp("ustar", entry->indicator, 5) != 0)
			break;

		const void* data = (u8*)_entry + USTAR_BLOCK_SIZE;
		_entry = (u8*)data + ROUND_UP(entry->size, USTAR_BLOCK_SIZE);

		handle_entry(entry, data);
	}
	kfree(entry);
}
