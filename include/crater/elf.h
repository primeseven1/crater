#pragma once

#include <crater/vfs.h>
#include <arch/elf.h>

/* Elf header e_type */
#define ELF_ET_NONE 0
#define ELF_ET_REL 1
#define ELF_ET_EXEC 2
#define ELF_ET_DYN 3
#define ELF_ET_CORE 4

/* Elf header e_version and e_ident.version */
#define ELF_EV_NONE 0
#define ELF_EV_CURRENT 1

/* Elf header e_ident._ident size */
#define ELF_EI_NIDENT 16

/* Elf header e_ident.mag */
#define ELF_MAG0 0x7f
#define ELF_MAG1 'E'
#define ELF_MAG2 'L'
#define ELF_MAG3 'F'
#define ELF_MAG "\177ELF"
#define ELF_SELFMAG 4

/* Elf header e_ident.class */
#define ELF_CLASS_NONE 0
#define ELF_CLASS_32 1
#define ELF_CLASS_64 2

/* Elf header e_ident.data */
#define ELF_DATA_NONE 0
#define ELF_DATA_2LSB 1
#define ELF_DATA_2MSB 2

/* Elf header e_ident.osabi */
#define ELF_OSABI_SYSTEMV 0

/* Section header sh_type */
#define ELF_SHT_NULL 0
#define ELF_SHT_PROGBITS 1
#define ELF_SHT_SYMTAB 2
#define ELF_SHT_STRTAB 3
#define ELF_SHT_RELA 4
#define ELF_SHT_HASH 5
#define ELF_SHT_DYNAMIC 6
#define ELF_SHT_NOTE 7
#define ELF_SHT_NOBITS 8
#define ELF_SHT_REL 9
#define ELF_SHT_SHLIB 10
#define ELF_SHT_DYNSYM 11
#define ELF_SHT_NUM 12

/* Section header sh_flags */
#define ELF_SHF_WRITE (1 << 0)
#define ELF_SHF_ALLOC (1 << 1)
#define ELF_SHF_EXECINSTR (1 << 2)
#define ELF_SHF_MERGE (1 << 4)
#define ELF_SHF_STRINGS (1 << 5)
#define ELF_SHF_INFO_LINK (1 << 6)
#define ELF_SHF_LINK_ORDER (1 << 7)
#define ELF_SHF_OS_NONCONFORMING (1 << 8)
#define ELF_SHF_GROUP (1 << 9)
#define ELF_SHF_TLS (1 << 10)

/* Program header p_type */
#define ELF_PT_NULL 0
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_PT_NOTE 4
#define ELF_PT_SHLIB 5
#define ELF_PT_PHDR 6
#define ELF_PT_TLS 7

/* Program header p_flags */
#define ELF_PF_X (1 << 0)
#define ELF_PF_W (1 << 1)
#define ELF_PF_R (1 << 2)

#define ELF_AT_NULL 0
#define ELF_AT_PHDR 3
#define ELF_AT_PHENT 4
#define ELF_AT_PHNUM 5
#define ELF_AT_PAGESZ 6
#define ELF_AT_ENTRY 9
#define ELF_AT_SECURE 23
#define ELF_AT_EXECFN 31

struct elf64_ehdr {
	union {
		unsigned char _ident[ELF_EI_NIDENT];
		struct {
			unsigned char mag[ELF_SELFMAG];
			u8 class, data, version, osabi, osabi_version;
		};
	} e_ident;
	u16 e_type;
	u16 e_machine;
	u32 e_version;
	u64 e_entry;
	u64 e_phoff;
	u64 e_shoff;
	u32 e_flags;
	u16 e_ehsize;
	u16 e_phentsize;
	u16 e_phnum;
	u16 e_shentsize;
	u16 e_shnum;
	u16 e_shstrndx;
};

struct elf64_shdr {
	u32 sh_name;
	u32 sh_type;
	u64 sh_flags;
	u64 sh_addr;
	u64 sh_offset;
	u64 sh_size;
	u32 sh_link;
	u32 sh_info;
	u64 sh_addralign;
	u64 sh_entsize;
};

struct elf64_sym {
	u32 st_name;
	u8 st_info;
	u8 st_other;
	u16 st_shndx;
	u64 st_value;
	u64 st_size;
};

struct elf64_phdr {
	u32 p_type;
	u32 p_flags;
	u64 p_offset;
	u64 p_vaddr;
	u64 p_paddr;
	u64 p_filesz;
	u64 p_memsz;
	u64 p_align;
};

struct elf64_auxv {
	u64 type;
	u64 value;
};

struct elf64_auxv_list {
	struct elf64_auxv phdr;
	struct elf64_auxv phnum;
	struct elf64_auxv phent;
	struct elf64_auxv entry;
	struct elf64_auxv execfn;
	struct elf64_auxv secure;
	struct elf64_auxv pagesz;
	struct elf64_auxv null;
};

/**
 * @brief Check if an elf header is valid
 * @param header The header to check
 * @return If the header is good, returns true, otherwise false
 */
bool elf64_header_ok(const struct elf64_ehdr* ehdr);

/**
 * @brief Load an ELF file into memory
 *
 * @param[in] vnode The vnode to load from
 * @param[out] auxv_list Populated while loading the binary
 *
 * @retval -EACCES @vnode is not a regular file
 * @retval -ENOMEM Out of memory
 * @retval -EEXIST Can mean overlapping headers
 * @retval -ESRCH Using the kernel mm struct
 * @return On success, returns 0, otherwise -errno
 */
int elf_load(struct vnode* vnode, struct elf64_auxv_list* auxv_list);
