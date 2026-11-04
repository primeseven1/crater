#include <crater/common.h>
#include <crater/limine.h>
#include <crater/init.h>
#include <crater/panic.h>
#include <crater/mm.h>

static volatile struct limine_hhdm_request __limine_request hhdm_request = {
	.request.id = LIMINE_HHDM_REQUEST,
	.request.revision = 0,
	.response = NULL
};

physaddr_t hhdm_physical(const void* virtual) {
	const struct limine_hhdm_response* hhdm = hhdm_request.response;
	return ((uintptr_t)virtual < hhdm->offset) ? 0 : (uintptr_t)virtual - hhdm->offset;
}

void* hhdm_virtual(physaddr_t physical) {
	if (!physical)
		return NULL;

	const struct limine_hhdm_response* hhdm = hhdm_request.response;
	return (physical > UINTPTR_MAX - hhdm->offset) ? NULL : (void*)((uintptr_t)physical + hhdm->offset);
}

void* hhdm_base(void) {
	return (void*)hhdm_request.response->offset;
}

static void hhdm_init(void) {
	if (unlikely(!hhdm_request.response))
		panic("No HHDM response");
}

INIT_TASK_DEFINE(hhdm_init_task, INIT_TASK_SCOPE_BSP, hhdm_init);
