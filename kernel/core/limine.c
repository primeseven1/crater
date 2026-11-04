#include <crater/limine.h>
#include <crater/init.h>
#include <crater/printk.h>
#include <crater/sched_types.h>

/* clang-format off */

/* Tell the bootloader where to start looking for requests. Literally just four magic numbers */
__attribute__((section(".limine_requests_start_marker"), aligned(8), used))
static u64 start_marker[4] = { 
	0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, 0x785c6ed015d3e316, 0x181e920a7852b9d9
};

#define LIMINE_BASE_REVISION_MAGIC 0x6a7b384944536bdc

/* Compiler does not know that the bootloader will modify this, so it must be made volatile */
static volatile struct {
	u64 magic1, base_revision, requested_base_revision;
} base_revision __limine_request = {
	.magic1 = 0xf9562b2d5c95a6c8, /* Literally just a magic number */
	.base_revision = LIMINE_BASE_REVISION_MAGIC, /* Bootloader will set to the actual loaded revision */
	.requested_base_revision = LIMINE_KERNEL_BASE_REVISION /* Set to zero by the bootloader if it supports the revision the kernel wants */
};

static volatile struct limine_stack_size_request __limine_request stack_size = {
	.request.id = LIMINE_STACK_SIZE_REQUEST,
	.request.revision = 0,
	.response = NULL,
	.stack_size = THREAD_STACK_SIZE
};

struct limine_executable_file_request __limine_request g_limine_executable_file_request = {
	.request.id = LIMINE_EXECUTABLE_FILE_REQUEST,
	.request.revision = 0,
	.response = NULL
};

/* Tell the bootloader where to stop searching for requests, this is only two magic numbers for some reason */
__attribute__((section(".limine_requests_end_marker"), aligned(8), used))
static u64 end_marker[2] = { 0xadc0e0531bb10d03, 0x9572709f31764c62 };

static void limine_base_revision_init(void) {
	if (base_revision.base_revision == LIMINE_BASE_REVISION_MAGIC) {
		if (base_revision.requested_base_revision != 0)
			panic("Not loaded by a limine compliant loader\n");

		base_revision.base_revision = LIMINE_KERNEL_BASE_REVISION;
		printk(PRINTK_WARN "limine: BUG: Loader did not set second element of the base revision to the loaded one\n");
		printk(PRINTK_WARN "limine: Assuming base revision %d\n", LIMINE_KERNEL_BASE_REVISION);
		return;
	}

	if (base_revision.base_revision == LIMINE_KERNEL_BASE_REVISION) {
		if (base_revision.requested_base_revision != 0)
			printk(PRINTK_WARN "limine: BUG: Loader forgot to set the last element of the base revision to zero\n");
		printk("limine: Using base revision %ld\n", base_revision.base_revision);
	} else {
		printk("limine: Using base revision %ld, but the kernel wants revision %d\n", base_revision.base_revision, LIMINE_KERNEL_BASE_REVISION);
	}
}

INIT_TASK_DEFINE(limine_base_revision_init_task, INIT_TASK_SCOPE_BSP, limine_base_revision_init);

int limine_match_base_revision(int* revisions, size_t revision_count) {
	for (size_t i = 0; i < revision_count; i++) {
		if (base_revision.base_revision == (u64)revisions[i])
			return revisions[i];
	}
	return -1;
}

int limine_base_revision(void) {
	return base_revision.base_revision;
}
