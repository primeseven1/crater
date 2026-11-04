#pragma once

#include <crater/common.h>
#include <crater/types.h>
#include <crater/smp.h>

enum init_task_scope {
	INIT_TASK_SCOPE_BSP, /* Run only on the BSP (bootstrap processor) */
	INIT_TASK_SCOPE_AP, /* Run only on the AP's (application processors) */
	INIT_TASK_SCOPE_BSP_AP /* Run both on the BSP and AP's */
};

struct init_task {
	const char* name;
	enum init_task_scope scope;
	void (*func)(void);
	struct init_task** dependencies;
	struct cpumask done, running;
};

#define INIT_TASK_DECLARE(...) extern struct init_task __VA_ARGS__

/* You can use the static storage class specifier here */
#define INIT_TASK_ARRAY_DEFINE(n, ...) struct init_task* n[] = { __VA_ARGS__, NULL }

#ifndef __CHECKER__ /* sparse doesn't like __VA_OPT__ */
/* You CANNOT add any storage classes, so that way other init tasks can reference it */
#define INIT_TASK_DEFINE(n, s, fn, ...) \
	static struct init_task* __##n##_deps[] = { __VA_OPT__(__VA_ARGS__,) NULL }; \
	extern struct init_task n; \
	struct init_task __attribute__((section(".initt"), aligned(8))) n = { \
		.name = #n, .scope = s, .func = fn, .dependencies = __##n##_deps \
	}
#else /* __CHECKER__ */
#define INIT_TASK_DEFINE(n, s, fn, ...) \
	static struct init_task* __##n##_deps[] = { NULL }; \
	extern struct init_task n; \
	__attribute__((section(".initt"), aligned(8))) struct init_task n = { \
		.name = #n, .scope = s, .func = fn, .dependencies = __##n##_deps, .done = { 0 }, .running = { 0 } \
	}
#endif /* __CHECKER__ */

/**
 * @brief Run an init task
 *
 * If the task scope is not valid for the context (eg. an AP task on the BSP) the task is
 * ignored.
 *
 * @param task The task to run
 */
void init_task_run(struct init_task* task);

static inline void init_task_run_array(struct init_task** array) {
	for (struct init_task** t = array; *t; t++)
		init_task_run(*t);
}

_Noreturn void kernel_ap_main(void);
_Noreturn void kernel_main(void);
