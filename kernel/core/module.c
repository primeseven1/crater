#include <crater/common.h>
#include <crater/compiler.h>
#include <crater/module.h>
#include <crater/printk.h>
#include <crater/string.h>

extern struct module _ld_kernel_modules_start[];
extern struct module _ld_kernel_modules_end[];

static struct module* find_module(const char* name) {
	struct module* const end = _ld_kernel_modules_end;
	for (struct module* mod = _ld_kernel_modules_start; mod < end; mod++) {
		if (strcmp(name, mod->name) == 0)
			return mod;
	}
	return NULL;
}

static int __module_load(struct module* mod) {
	int err = 0;
	mutex_acquire(&mod->mtx);

	if (mod->loaded)
		goto out;
	if (!mod->init) {
		printk(PRINTK_ERR "module: %s has no init function\n", mod->name);
		err = -EFAULT;
		goto out;
	}

	for (struct init_task** deps = mod->init_task_deps; *deps; deps++)
		init_task_run(*deps);
	if (mod->module_deps) {
		for (const char** deps = mod->module_deps; *deps; deps++) {
			struct module* dep = find_module(*deps);
			if (dep) {
				err = __module_load(dep);
				if (err)
					goto out;
			} else {
				err = -ENOENT;
				goto out;
			}
		}
	}

	err = mod->init();
	if (err == 0) {
		mod->loaded = true;
		printk(PRINTK_INFO "module: %s module loaded successfully\n", mod->name);
	}

out:
	mutex_release(&mod->mtx);
	return err;
}

void module_load_builtins(void) {
	struct module* const end = _ld_kernel_modules_end;
	for (struct module* mod = _ld_kernel_modules_start; mod < end; mod++)
		__module_load(mod);
}

int module_load(const char* name) {
	struct module* mod = find_module(name);
	if (!mod)
		return -ENOENT;
	return __module_load(mod);
}
