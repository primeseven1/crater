#include <crater/common.h>
#include <crater/hashtable.h>
#include <crater/string.h>
#include <crater/limine.h>
#include <crater/vmm.h>
#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/cmdline.h>
#include <crater/init.h>

static struct hashtable* cmdline_hashtable = NULL;

const char* cmdline_get(const char* arg) {
	if (!cmdline_hashtable)
		return NULL;

	const char* ret;
	int err = hashtable_search(cmdline_hashtable, arg, strlen(arg), &ret);
	if (err)
		return NULL;
	return ret;
}

static void cmdline_init(void) {
	const struct limine_executable_file_response* response = g_limine_executable_file_request.response;
	if (unlikely(!response || !response->executable_file))
		return;
	const char* cmdline = response->executable_file->string;
	if (unlikely(!cmdline))
		return;

	size_t cmdline_size = strlen(cmdline);
	if (cmdline_size++ == 0)
		return;
	if (cmdline_size > PAGE_SIZE) {
		printk(PRINTK_WARN "cmdline: Kernel command line is longer than a page\n");
		cmdline_size = PAGE_SIZE;
	}

	/* Estimating ~10 arguments, should be more than enough */
	cmdline_hashtable = hashtable_create(10, sizeof(char*));
	if (!cmdline_hashtable)
		return;

	char* cmdline_copy = vm_map(NULL, cmdline_size, PGPROT_READ | PGPROT_WRITE, 0, NULL);
	if (IS_PTR_ERR(cmdline_copy))
		return;
	strlcpy(cmdline_copy, cmdline, cmdline_size);
	char* const cmdline_mmap_base = cmdline_copy;

	int err = 0;
	char* save_outer = NULL;
	for (char* tok = strtok_r(cmdline_copy, " ", &save_outer); tok != NULL; tok = strtok_r(NULL, " ", &save_outer)) {
		char* save_inner = NULL;
		char* key = strtok_r(tok, "=", &save_inner);
		char* value = strtok_r(NULL, "=", &save_inner);
		if (!key || !value)
			break;

		err = hashtable_insert(cmdline_hashtable, key, strlen(key), &value);
		if (err)
			break;
	}

	printk(PRINTK_INFO "cmdline: %s\n", cmdline);
	err = vm_protect(cmdline_mmap_base, cmdline_size, PGPROT_READ, VMM_SEALED);
	if (unlikely(err))
		printk(PRINTK_ERR "cmdline: Failed to make command line read only: %d\n", err);
}

INIT_TASK_DECLARE(vmm_init_task, heap_init_task);
INIT_TASK_DEFINE(cmdline_init_task, INIT_TASK_SCOPE_BSP, cmdline_init, &vmm_init_task, &heap_init_task);
