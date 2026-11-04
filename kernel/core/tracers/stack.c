#include <crater/common.h>
#include <crater/limine.h>
#include <crater/trace.h>
#include <crater/printk.h>
#include <crater/init.h>
#include <crater/elf.h>
#include <crater/errno.h>

extern const u8 _ld_kernel_start;
extern const u8 _ld_kernel_end;

static const struct elf64_sym* kernel_symtab = NULL;
static const char* kernel_syms_strtab = NULL;
static u32 kernel_sym_count = 0;

#define KERNEL_MIN_VIRTUAL 0xFFFFFFFF80000000

static const char* trace_kernel_symbol_name(const u8* ptr) {
	if (!kernel_symtab || !kernel_syms_strtab)
		return NULL;
	if (ptr < &_ld_kernel_start || ptr > &_ld_kernel_end)
		return NULL;

	size_t kernel_offset = (uintptr_t)&_ld_kernel_start - KERNEL_MIN_VIRTUAL;
	uintptr_t _ptr = (uintptr_t)ptr;
#ifdef CONFIG_KASLR
	_ptr -= kernel_offset + KERNEL_MIN_VIRTUAL;
#else
	_ptr -= kernel_offset;
#endif

	for (u32 i = 0; i < kernel_sym_count; i++) {
		u64 st_value = kernel_symtab[i].st_value;
		u64 st_size = kernel_symtab[i].st_size;
		if (_ptr >= st_value && _ptr < st_value + st_size)
			return kernel_syms_strtab + kernel_symtab[i].st_name;
	}

	return NULL;
}

static ssize_t trace_kernel_symbol_offset(const u8* ptr) {
	if (!kernel_symtab)
		return -1;
	if (ptr < &_ld_kernel_start || ptr > &_ld_kernel_end)
		return -1;

	size_t kernel_offset = (uintptr_t)&_ld_kernel_start - KERNEL_MIN_VIRTUAL;
	uintptr_t _ptr = (uintptr_t)ptr;
#ifdef CONFIG_KASLR
	_ptr -= kernel_offset + KERNEL_MIN_VIRTUAL;
#else
	_ptr -= kernel_offset;
#endif

	for (u32 i = 0; i < kernel_sym_count; i++) {
		u64 st_value = kernel_symtab[i].st_value;
		u64 st_size = kernel_symtab[i].st_size;
		if (_ptr >= st_value && _ptr < st_value + st_size)
			return _ptr - kernel_symtab[i].st_value;
	}

	return -1;
}

void dump_stack(void) {
	const void* const* stack_frame = __builtin_frame_address(0);
	const size_t kernel_offset = (uintptr_t)&_ld_kernel_start - KERNEL_MIN_VIRTUAL;

	const char* question = "";

	printk(PRINTK_CRIT "Stack trace:\n");
	for (int i = 0; i < 20; i++) {
		if (!stack_frame)
			break;
		const void* ret = stack_frame[1];
		if (!ret)
			break;

		const char* name = trace_kernel_symbol_name(ret);
		if (name) {
			ssize_t offset = trace_kernel_symbol_offset(ret);
			if (offset == 0)
				question = "? ";
			printk(PRINTK_CRIT " [%p] %s%s+%#zx\n", ret, question, name, (size_t)offset);
			question = "";
		} else {
			question = "? ";
		}

		stack_frame = stack_frame[0];
	}
	printk(PRINTK_CRIT "End stack trace: (kernel offset: %#zx)\n", kernel_offset);
}

static void stack_tracer_init(void) {
	const struct limine_executable_file_response* response = g_limine_executable_file_request.response;
	if (unlikely(!response)) {
		printk(PRINTK_WARN "core: %s failed: No file response", __func__);
		return;
	}
	struct elf64_ehdr* ehdr = response->executable_file->address;
	if (unlikely(!elf64_header_ok(ehdr))) {
		printk("core: %s failed: Invalid ELF\n", __func__);
		return;
	}

	/* Now find the symbol table second header and the string table associated with it */
	struct elf64_shdr* shdr_table = (struct elf64_shdr*)((u8*)ehdr + ehdr->e_shoff);
	for (u16 i = 0; i < ehdr->e_shnum; i++) {
		if (shdr_table[i].sh_type != ELF_SHT_SYMTAB)
			continue;

		/* Check to see if the string table is available for the symbol table */
		u16 strtab_index = shdr_table[i].sh_link;
		if (unlikely(shdr_table[strtab_index].sh_type != ELF_SHT_STRTAB))
			return;

		kernel_symtab = (struct elf64_sym*)((u8*)ehdr + shdr_table[i].sh_offset);
		kernel_syms_strtab = (char*)((u8*)ehdr + shdr_table[strtab_index].sh_offset);
		kernel_sym_count = shdr_table[i].sh_size / sizeof(struct elf64_sym);
		break;
	}
}

INIT_TASK_DEFINE(stack_tracer_init_task, INIT_TASK_SCOPE_BSP, stack_tracer_init);
