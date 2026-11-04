#pragma once

#include <crater/init.h>
#include <crater/mutex.h>
#include <crater/errno.h>

struct module {
	const char* name;
	int (*init)(void);
	const char** module_deps;
	struct init_task** init_task_deps;
	bool loaded;
	mutex_t mtx;
};

/**
 * @brief Load all builtin kernel modules
 *
 * Called by the BSP after initialization
 */
void module_load_builtins(void);

/**
 * @brief Attempt to load a kernel module
 * @param name The name of the module
 * @return -errno on failure
 */
int module_load(const char* name);

#ifndef __CHECKER__
#define MODULE(n, initfn, moddeps, ...) \
	static struct init_task* __module_##initfn##_task_deps[] = { __VA_OPT__(__VA_ARGS__,) NULL }; \
	__attribute__((section(".modules"), aligned(8), used)) \
	static struct module __module_##initfn = { \
		.name = n, \
		.init = initfn, \
		.module_deps = moddeps, \
		.init_task_deps = __module_##initfn##_task_deps, \
		.loaded = false, \
		.mtx = MUTEX_INITIALIZER(__module_##initfn.mtx) \
	}
#else
#define MODULE(n, initfn, moddeps, ...) \
	static struct init_task* __module_##initfn##_task_deps[] = { NULL }; \
	__attribute__((section(".modules"), aligned(8), used)) \
	static struct module __module_##initfn = { \
		.name = n, \
		.init = initfn, \
		.module_deps = moddeps, \
		.init_task_deps = __module_##initfn##_task_deps, \
		.loaded = false, \
		.mtx = MUTEX_INITIALIZER(__module_##initfn.mtx) \
	}
#endif /* __CHECKER__ */
