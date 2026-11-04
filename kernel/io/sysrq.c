#include <crater/printk.h>
#include <crater/input.h>
#include <acpi/sleep.h>
#include "internal.h"

struct sysrq {
	const char* name;
	const char* help;
	bool multiple_keycodes;
	union {
		unsigned int keycode;
		unsigned int* keycodes;
	};
	int (*func)(unsigned int);
};

static _Noreturn int sysrq_reboot(unsigned int keycode) {
	(void)keycode;
	acpi_reboot();
}

static int sysrq_loglevel(unsigned int keycode) {
	switch (keycode) {
	case KEYCODE_0:
		printk_set_level(0);
		return 0;
	case KEYCODE_1:
		printk_set_level(1);
		return 0;
	case KEYCODE_2:
		printk_set_level(2);
		return 0;
	case KEYCODE_3:
		printk_set_level(3);
		return 0;
	case KEYCODE_4:
		printk_set_level(4);
		return 0;
	case KEYCODE_5:
		printk_set_level(5);
		return 0;
	case KEYCODE_6:
		printk_set_level(6);
		return 0;
	}
	return -EINVAL;
}

static unsigned int loglevel_keycodes[] = {
	KEYCODE_0, KEYCODE_1, KEYCODE_2, KEYCODE_3, KEYCODE_4, KEYCODE_5, KEYCODE_6, KEYCODE_RESERVED
};

static struct sysrq sysrq_arr[] = {
	{ .name = "reboot", .multiple_keycodes = false, .keycode = KEYCODE_B, .help = "Reboot the system (b)", .func = sysrq_reboot },
	{ .name = "loglevel", .multiple_keycodes = true, .keycodes = loglevel_keycodes, .help = "Set the loglevel (0,6)", .func = sysrq_loglevel }
};

void do_sysrq(unsigned int keycode) {
	if (keycode == KEYCODE_H) {
		for (size_t i = 0; i < ARRAY_SIZE(sysrq_arr); i++)
			printk(PRINTK_INFO "sysrq: %s\n", sysrq_arr[i].help);
		return;
	}

	struct sysrq* rq = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(sysrq_arr); i++) {
		if (!sysrq_arr[i].multiple_keycodes) {
			if (sysrq_arr[i].keycode != keycode)
				continue;
			rq = &sysrq_arr[i];
			goto loop_out;
		}
		unsigned int* code = sysrq_arr[i].keycodes;
		while (*code != KEYCODE_RESERVED) {
			if (*code == keycode) {
				rq = &sysrq_arr[i];
				goto loop_out;
			}
			code++;
		}
	}

loop_out:
	if (!rq)
		return;

	int err = rq->func(keycode);
	if (err)
		printk(PRINTK_ERR "sysrq: %s failed with code %d\n", rq->name, err);
}
