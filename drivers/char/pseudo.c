#include <crater/device.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/init.h>

static void inactive(struct chrdev* chrdev) {
	(void)chrdev;
	bug("Unreachable");
}

static int full_read(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	return uio_zero(uio->resid, uio);
}

static int full_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)uio;
	(void)ioflags;
	(void)cred;
	return -ENOSPC;
}

static const struct chrdev_ops full_ops = {
	.open = NULL,
	.close = NULL,
	.read = full_read,
	.write = full_write,
	.ioctl = NULL,
	.inactive = inactive
};

static struct chrdev full_chrdev = {
	.major = 1,
	.minor = 7,
	.ops = &full_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

static int null_read(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)uio;
	(void)ioflags;
	(void)cred;
	return 0;
}

static int null_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	uio_skip(uio->resid, uio);
	return 0;
}

static const struct chrdev_ops null_ops = {
	.open = NULL,
	.close = NULL,
	.read = null_read,
	.write = null_write,
	.ioctl = NULL,
	.inactive = inactive
};

static struct chrdev null_chrdev = {
	.major = 1,
	.minor = 3,
	.ops = &null_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

static int zero_read(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	return uio_zero(uio->resid, uio);
}

static int zero_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	uio_skip(uio->resid, uio);
	return 0;
}

static const struct chrdev_ops zero_ops = {
	.open = NULL,
	.close = NULL,
	.read = zero_read,
	.write = zero_write,
	.ioctl = NULL,
	.inactive = inactive
};

static struct chrdev zero_chrdev = {
	.major = 1,
	.minor = 5,
	.ops = &zero_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

static void pseudo_devices_init(void) {
	int err = chrdev_register(&full_chrdev, "full", 0666);
	if (err)
		printk(PRINTK_ERR "pseudo: Failed to register full device: %d\n", err);
	err = chrdev_register(&null_chrdev, "null", 0666);
	if (err)
		printk(PRINTK_ERR "pseudo: Failed to register null device: %d\n", err);
	err = chrdev_register(&zero_chrdev, "zero", 0666);
	if (err)
		printk(PRINTK_ERR "pseudo: Failed to register zero device: %d", err);
}

INIT_TASK_DECLARE(device_init_task);
INIT_TASK_DEFINE(pseudo_devices_init_task, INIT_TASK_SCOPE_BSP, pseudo_devices_init, &device_init_task);
