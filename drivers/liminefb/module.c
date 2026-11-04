#include <crater/common.h>
#include <crater/module.h>
#include <crater/limine.h>
#include <crater/slab.h>
#include <crater/vmm.h>
#include <crater/term.h>

#include "flanterm/src/flanterm_backends/fb.h"

static void* flanterm_alloc(size_t size) {
	if (size < PAGE_SIZE)
		return kzalloc(size, MM_ZONE_NORMAL);
	return vmalloc(size);
}

static void flanterm_free(void* ptr, size_t size) {
	if (size < PAGE_SIZE)
		kfree(ptr);
	else
		vfree(ptr);
}

static struct flanterm_context** contexts;
static u64 context_count = 0;

static void liminefb_term_write(const char* s, size_t count) {
	for (u64 i = 0; i < context_count; i++) {
		size_t start = 0;
		for (size_t j = 0; j < count; j++) {
			if (s[j] != '\n')
				continue;
			if (j > start)
				flanterm_write(contexts[i], s + start, j - start);
			const char* crlf = "\r\n";
			flanterm_write(contexts[i], crlf, __builtin_strlen(crlf));
			start = j + 1;
		}
		if (start < count)
			flanterm_write(contexts[i], s + start, count - start);
	}
}

static volatile struct limine_framebuffer_request __limine_request fb_request = {
	.request.id = LIMINE_FRAMEBUFFER_REQUEST,
	.request.revision = 0,
	.response = NULL
};

static int liminefb_init(void) {
	struct limine_framebuffer_response* fb_response = fb_request.response;
	if (unlikely(!fb_response))
		return -ENOPROTOOPT;

	if (fb_response->framebuffer_count == 0)
		return -ENODEV;

	context_count = fb_response->framebuffer_count;
	contexts = kzalloc(sizeof(*contexts) * context_count, MM_ZONE_NORMAL);
	if (!contexts)
		return -ENOMEM;
	for (u64 i = 0; i < context_count; i++) {
		struct limine_framebuffer* fb = fb_response->framebuffers[i];
		contexts[i] = flanterm_fb_init(flanterm_alloc, flanterm_free, (u32 __force*)fb->address, fb->width, fb->height, fb->pitch,
				fb->red_mask_size, fb->red_mask_shift, fb->green_mask_size, fb->green_mask_shift, fb->blue_mask_size, fb->blue_mask_shift,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 1, 0, 0, 0, FLANTERM_FB_ROTATE_0, true);
		if (!contexts[i]) {
			for (i = 0; i < context_count; i++) {
				if (!contexts[i])
					break;
				flanterm_deinit(contexts[i], flanterm_free);
			}
			kfree(contexts);
			return -ENOMEM;
		}
	}

	return term_driver_register(liminefb_term_write);
}

INIT_TASK_DECLARE(heap_init_task);
MODULE("liminefb", liminefb_init, NULL, &heap_init_task);
