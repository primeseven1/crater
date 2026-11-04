#include <crater/elf.h>
#include <crater/vmm.h>
#include <crater/string.h>
#include <crater/panic.h>
#include <crater/usercopy.h>

static int read_exact(struct vnode* vnode, void* buf, size_t count, off_t off) {
	if (off < 0)
		return -EINVAL;
	size_t readcnt;
	int err = vfs_read(vnode, buf, count, off, 0, &readcnt);
	if (err)
		return err;
	if (readcnt != count)
		return -ENOEXEC;
	return 0;
}

static int read_exact_user(struct vnode* vnode, void __user* buf, size_t count, off_t off) {
	void* kernel_buf = vmalloc(count);
	if (!kernel_buf)
		return -ENOMEM;

	int err = read_exact(vnode, kernel_buf, count, off);
	if (err == 0)
		err = usercopy_to_user(buf, kernel_buf, count);

	vfree(kernel_buf);
	return err;
}

bool elf64_header_ok(const struct elf64_ehdr* ehdr) {
	const char magic[ELF_SELFMAG] = { ELF_MAG0, ELF_MAG1, ELF_MAG2, ELF_MAG3 };
	if (__builtin_memcmp(ehdr->e_ident.mag, magic, sizeof(magic)) != 0)
		return false;

	if (ehdr->e_ident.class != ELF_CLASS_64 || ehdr->e_ident.data != ELF_DATA_2LSB ||
			ehdr->e_ident.version != ELF_EV_CURRENT || ehdr->e_version != ELF_EV_CURRENT ||
			ehdr->e_ident.osabi != ELF_OSABI_SYSTEMV || ehdr->e_ident.osabi_version != 0)
		return false;
	if (ehdr->e_machine != ARCH_ELF_EM_ARCHITECTURE)
		return false;
	if ((ehdr->e_type != ELF_ET_EXEC && ehdr->e_type != ELF_ET_DYN) || ehdr->e_phentsize != sizeof(struct elf64_phdr) || ehdr->e_phnum == 0)
		return false;

	return true;
}

static pgprot_t flags_to_pgprot(u32 flags) {
	pgprot_t prot = PGPROT_USER;
	if (flags & ELF_PF_R)
		prot |= PGPROT_READ;
	if (flags & ELF_PF_W)
		prot |= PGPROT_WRITE;
	if (flags & ELF_PF_X)
		prot |= PGPROT_EXEC;
	return prot;
}

static int load_phdr(struct vnode* vnode, struct elf64_phdr* phdr) {
	size_t filesz = phdr->p_filesz;
	size_t memsz = phdr->p_memsz;

	if (filesz > memsz)
		return -ENOEXEC;
	if (memsz == 0)
		return 0;

	u8 __user* mempos = (u8 __user*)phdr->p_vaddr;
	size_t fileoff = phdr->p_offset;
	const pgprot_t prot = flags_to_pgprot(phdr->p_flags);

	/* Handle misaligned position */
	size_t first_page_offset = (uintptr_t)mempos % PAGE_SIZE;
	if (first_page_offset) {
		u8 __user* page = (u8 __user*)((uintptr_t)mempos - first_page_offset);
		size_t first_page_memsz = PAGE_SIZE - first_page_offset;
		size_t first_page_filesz;
		if (first_page_memsz > memsz)
			first_page_memsz = memsz;
		first_page_filesz = (first_page_memsz < filesz) ? first_page_memsz : filesz;

		void __user* ptr = vm_map_user(page, PAGE_SIZE, PGPROT_READ | PGPROT_WRITE | PGPROT_USER, VMM_FIXED | VMM_NOREPLACE, NULL);
		if (IS_PTR_ERR(ptr))
			return PTR_ERR(ptr);

		if (first_page_filesz) {
			int err = read_exact_user(vnode, mempos, first_page_filesz, fileoff);
			if (err)
				return err;
		}

		int err = vm_protect_user(page, PAGE_SIZE, prot, 0);
		if (err)
			return err;

		mempos += first_page_memsz;
		memsz -= first_page_memsz;
		filesz -= first_page_filesz;
		fileoff += first_page_filesz;
	}

	/* Handle file backed pages */
	if (filesz) {
		size_t file_page_count = ROUND_UP(filesz, PAGE_SIZE) >> PAGE_SHIFT;
		size_t file_size_rounded = file_page_count << PAGE_SHIFT;

		void __user* ptr = vm_map_user(mempos, file_size_rounded, PGPROT_READ | PGPROT_WRITE | PGPROT_USER, VMM_FIXED | VMM_NOREPLACE, NULL);
		if (IS_PTR_ERR(ptr))
			return PTR_ERR(ptr);
		int err = read_exact_user(vnode, mempos, filesz, fileoff);
		if (err)
			return err;
		err = vm_protect_user(mempos, file_size_rounded, prot, 0);
		if (err)
			return err;

		if (memsz <= file_size_rounded)
			return 0;
		mempos += file_size_rounded;
		memsz -= file_size_rounded;
	}

	/* BSS section */
	if (memsz) {
		void __user* ptr = vm_map_user(mempos, memsz, prot, VMM_FIXED | VMM_NOREPLACE, NULL);
		if (IS_PTR_ERR(ptr))
			return PTR_ERR(ptr);
	}

	return 0;
}

int elf_load(struct vnode* vnode, struct elf64_auxv_list* auxv_list) {
	if (vnode->type != VTYPE_REGULAR)
		return -EACCES;

	struct elf64_ehdr ehdr;
	int err = read_exact(vnode, &ehdr, sizeof(ehdr), 0);
	if (err)
		return err;
	if (!elf64_header_ok(&ehdr))
		return -ENOEXEC;

	size_t phtable_size;
	if (__builtin_mul_overflow(ehdr.e_phentsize, ehdr.e_phnum, &phtable_size))
		return -ERANGE;
	struct elf64_phdr* phdr_table = vmalloc(phtable_size);
	if (!phdr_table)
		return -ENOMEM;
	err = read_exact(vnode, phdr_table, phtable_size, ehdr.e_phoff);
	if (err) {
		vfree(phdr_table);
		return err;
	}

	*auxv_list = (struct elf64_auxv_list){
		.phdr = { .type = ELF_AT_PHDR, .value = 0 }, .phnum = { .type = ELF_AT_PHNUM, .value = ehdr.e_phnum },
		.phent = { .type = ELF_AT_PHENT, .value = ehdr.e_phentsize }, .entry = { .type = ELF_AT_ENTRY, .value = ehdr.e_entry },
		.execfn = { .type = ELF_AT_EXECFN, .value = 0 }, .secure = { .type = ELF_AT_SECURE, .value = 0 },
		.pagesz = { .type = ELF_AT_PAGESZ, .value = PAGE_SIZE }, .null = { .type = ELF_AT_NULL, .value = 0 }
	};

	for (int i = 0; i < ehdr.e_phnum; i++) {
		struct elf64_phdr* phdr = &phdr_table[i];
		switch (phdr->p_type) {
		case ELF_PT_LOAD:
			err = load_phdr(vnode, phdr);
			if (err)
				goto out;
			break;
		case ELF_PT_PHDR:
			auxv_list->phdr.value = phdr->p_vaddr;
			break;
		}
	}

out:
	vfree(phdr_table);
	return err;
}
