#include <crater/vfs.h>
#include <crater/printk.h>
#include <crater/panic.h>
#include "internal.h"

static int mount_devfs(void) {
	const struct vattr attrs = {
		.type = VTYPE_DIR,
		.mode = VATTR_MODE_USER_ALL | VATTR_MODE_GROUP_READ | VATTR_MODE_GROUP_SEARCH | VATTR_MODE_OTHERS_READ | VATTR_MODE_OTHERS_SEARCH,
		.gid = current_cred()->gid, .uid = current_cred()->uid,
	};

	int err = vfs_mkdir(NULL, "/dev", &attrs, NULL);
	if (unlikely(err)) {
		if (unlikely(err == -EEXIST)) {
			printk(PRINTK_WARN "init: /dev exists for some reason \n");
			err = 0;
		} else {
			return err;
		}
	}

	err = vfs_mount("devfs", NULL, "/dev", NULL);
	if (unlikely(err))
		return err;

	return 0;
}

void mount_root_initrd(void) {
	int err = vfs_mount_root("tmpfs");
	if (err)
		panic("Failed to mount root file system: %d\n", err);
	err = mount_devfs();
	if (err)
		printk(PRINTK_ERR "init: Failed to mount devfs: %d\n", err);
	initrd_init();
}
