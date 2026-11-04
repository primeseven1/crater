#include <crater/device.h>
#include <crater/init.h>
#include <crater/hashtable.h>
#include <crater/panic.h>
#include <crater/mm.h>
#include <crater/printk.h>

static struct hashtable* chrdev_tbl;
static MUTEX_DEFINE(chrdev_mtx);

static int chrdev_table_add(struct chrdev* chrdev) {
	int key[] = { chrdev->major, chrdev->minor };
	mutex_acquire(&chrdev_mtx);

	struct chrdev* tmp;
	int err = hashtable_search(chrdev_tbl, &key, sizeof(key), &tmp);
	if (err == 0) {
		err = -EEXIST;
	} else {
		atomic_store(&chrdev->dead, false);
		err = hashtable_insert(chrdev_tbl, &key, sizeof(key), &chrdev);
		if (err == 0)
			chrdev_hold(chrdev);
	}

	mutex_release(&chrdev_mtx);
	return err;
}

static int chrdev_table_remove(struct chrdev* chrdev) {
	int key[] = { chrdev->major, chrdev->minor };
	mutex_acquire(&chrdev_mtx);

	struct chrdev* tmp;
	int err = hashtable_search(chrdev_tbl, &key, sizeof(key), &tmp);
	if (err == 0 && chrdev == tmp) {
		bug(hashtable_remove(chrdev_tbl, &key, sizeof(key)) != 0);
		atomic_store(&chrdev->dead, true);
	} else {
		err = -ENODEV;
	}

	mutex_release(&chrdev_mtx);
	return err;
}

int chrdev_register(struct chrdev* chrdev, const char* path, mode_t mode) {
	if (!chrdev || chrdev->major < 0 || chrdev->minor < 0 || !chrdev->ops)
		return -EINVAL;

	int err = chrdev_table_add(chrdev);
	if (err == 0 && path) {
		struct devfs_device_info info = { .type = VTYPE_CHDEV, .un.chrdev = chrdev };
		err = devfs_register(&info, path, mode);
		if (err) {
			bug(chrdev_table_remove(chrdev) != 0);
			chrdev_release(chrdev);
		}
	}

	return err;
}

int chrdev_unregister(struct chrdev* chrdev) {
	if (!chrdev || chrdev->major < 0 || chrdev->minor < 0)
		return -EINVAL;

	int err = chrdev_table_remove(chrdev);
	if (err)
		return err;

	struct devfs_device_info info = { .type = VTYPE_CHDEV, .un.chrdev = chrdev };
	err = devfs_unregister(&info);
	bug(err != 0 && err != -ENOENT);

	chrdev_release(chrdev);
	return 0;
}

int chrdev_get(int major, int minor, struct chrdev** out) {
	if (major < 0 || minor < 0 || !out)
		return -EINVAL;

	int key[] = { major, minor };
	mutex_acquire(&chrdev_mtx);

	struct chrdev* chrdev;
	int err = hashtable_search(chrdev_tbl, &key, sizeof(key), &chrdev);
	if (err == 0) {
		chrdev_hold(chrdev);
		*out = chrdev;
	} else {
		err = -ENXIO;
	}

	mutex_release(&chrdev_mtx);
	return err;
}

static void device_init(void) {
	chrdev_tbl = hashtable_create(64, sizeof(struct chrdev*));
	if (!chrdev_tbl)
		out_of_memory();
}

INIT_TASK_DECLARE(heap_init_task);
INIT_TASK_DEFINE(device_init_task, INIT_TASK_SCOPE_BSP, device_init, &heap_init_task);
