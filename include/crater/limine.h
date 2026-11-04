#pragma once

#include <crater/types.h>
#include <crater/compiler.h>
#include <crater/atomic.h>
#include <arch/limine.h>

#define LIMINE_KERNEL_BASE_REVISION 6

#define __limine_request __attribute__((section(".limine_requests"), aligned(8), used))

#define LIMINE_COMMON_MAGIC1 0xc7b1dd30df4c8b88
#define LIMINE_COMMON_MAGIC2 0x0a82e883a194f07b

#define LIMINE_BOOTLOADER_INFO_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0xf55038d8e2a1202f, 0x279426fcf5f59740 }
#define LIMINE_EXECUTABLE_CMDLINE_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x4b161536e598651e, 0xb390ad4a2f1f303a }
#define LIMINE_STACK_SIZE_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x224ef0460a8e8926, 0xe1cb0fc25f46ea3d }
#define LIMINE_FIRMWARE_TYPE_RESPONSE \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x8c2f75d90bef28a8, 0x7045a4688eac00c3 }
#define LIMINE_HHDM_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }
#define LIMINE_FRAMEBUFFER_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x9d5827dcd881dd75, 0xa3148604f6fab11b }
#define LIMINE_PAGING_MODE_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x95c1a0edab0944cb, 0xa4e5cb3842f7488a }
#define LIMINE_MP_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x95a67b819a1b857e, 0xa0b61b723b6a73e0 }
#define LIMINE_MMAP_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }
#define LIMINE_EXECUTABLE_FILE_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0xad97e90e83f1ed67, 0x31eb5d1c5ff23b69 }
#define LIMINE_MODULE_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x3e7e279702be32af, 0xca1c4f3bd1280cee }
#define LIMINE_RSDP_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0xc5e77b6b397e7b43, 0x27637845accdcf3c }
#define LIMINE_SMBIOS_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x9e9046f11e095391, 0xaa4a520fefbde5ee }
#define LIMINE_EXECUTABLE_ADDRESS_REQUEST \
	{ LIMINE_COMMON_MAGIC1, LIMINE_COMMON_MAGIC2, 0x71ba76863cc55f63, 0xb2644a48c516a487 }

struct limine_request {
	u64 id[4];
	u64 revision;
};

struct limine_booloader_info_response {
	u64 revision;
	const char* name;
	const char* version;
};

struct limine_bootloader_info_request {
	struct limine_request request;
	struct limine_bootloader_info_response* response;
};

struct limine_stack_size_response {
	u64 revision;
};

struct limine_stack_size_request {
	struct limine_request request;
	struct limine_stack_size_response* response;
	u64 stack_size;
};

struct limine_hhdm_response {
	u64 revision;
	u64 offset;
};

struct limine_hhdm_request {
	struct limine_request request;
	struct limine_hhdm_response* response;
};

enum limine_framebuffer_memory_model {
	LIMINE_FRAMEBUFFER_RGB = 1
};

struct limine_video_mode {
	u64 pitch;
	u64 width;
	u64 height;
	u16 bpp;
	u8 memory_model;
	u8 red_mask_size;
	u8 red_mask_shift;
	u8 green_mask_size;
	u8 green_mask_shift;
	u8 blue_mask_size;
	u8 blue_mask_shift;
};

struct limine_framebuffer {
	void __iomem* address;
	u64 width;
	u64 height;
	u64 pitch;
	u16 bpp;
	u8 memory_model;
	u8 red_mask_size;
	u8 red_mask_shift;
	u8 green_mask_size;
	u8 green_mask_shift;
	u8 blue_mask_size;
	u8 blue_mask_shift;
	u8 __padding[7];
	u64 edid_size;
	const void* edid;
	u64 mode_count;
	struct limine_video_mode** modes;
};

struct limine_framebuffer_response {
	u64 revision;
	u64 framebuffer_count;
	struct limine_framebuffer** framebuffers;
};

struct limine_framebuffer_request {
	struct limine_request request;
	struct limine_framebuffer_response* response;
};

struct limine_paging_mode_response {
	u64 revision;
	u64 mode;
};

struct limine_paging_mode_request {
	struct limine_request request;
	struct limine_paging_mode_response* response;
	u64 mode;
	u64 max_mode;
	u64 min_mode;
};

struct limine_mp_request {
	struct limine_request request;
	struct arch_limine_mp_response* arch_specific_response;
	u64 flags;
};

enum limine_mmap_type {
	LIMINE_MMAP_USABLE,
	LIMINE_MMAP_RESERVED,
	LIMINE_MMAP_ACPI_RECLAIMABLE,
	LIMINE_MMAP_ACPI_NVS,
	LIMINE_MMAP_BAD_MEMORY,
	LIMINE_MMAP_BOOTLOADER_RECLAIMABLE,
	LIMINE_MMAP_EXECUTABLE_AND_MODULES,
	LIMINE_MMAP_FRAMEBUFFER,
	LIMINE_MMAP_RESERVED_MAPPED /* Base revision 4+ */
};

struct limine_mmap_entry {
	physaddr_t base;
	u64 length;
	u64 type;
};

struct limine_mmap_response {
	u64 revision;
	u64 entry_count;
	struct limine_mmap_entry** entries;
};

struct limine_mmap_request {
	struct limine_request request;
	struct limine_mmap_response* response;
};

struct limine_uuid {
	u32 a;
	u16 b;
	u16 c;
	u8 d[8];
};

enum limine_media_type {
	LIMINE_MEDIA_TYPE_GENERIC,
	LIMINE_MEDIA_TYPE_OPTICAL,
	LIMINE_MEDIA_TYPE_TFTP
};

struct limine_file {
	u64 revision;
	void* address;
	u64 size;
	const char* path;
	const char* string;
	u32 media_type;
	u32 __padding;
	u32 tftp_ip;
	u32 tftp_port;
	u32 partition_index;
	u32 mbr_disk_id;
	struct limine_uuid gpt_disk_uuid;
	struct limine_uuid gpt_part_uuid;
	struct limine_uuid part_uuid;
};

struct limine_executable_file_response {
	u64 revision;
	struct limine_file* executable_file;
};

struct limine_executable_file_request {
	struct limine_request request;
	struct limine_executable_file_response* response;
};

enum limine_internal_module_flags {
	LIMINE_INTERNAL_MODULE_REQUIRED = (1 << 0),
	LIMINE_INTERNAL_MODULE_COMPRESSED = (1 << 1)
};

struct limine_internal_module {
	const char* path;
	const char* string;
	u64 flags;
};

struct limine_module_response {
	u64 revision;
	u64 module_count;
	struct limine_file** modules;
};

struct limine_module_request {
	struct limine_request request;
	struct limine_module_response* response;
	u64 internal_module_count;
	struct limine_internal_module** internal_modules;
};

struct limine_rsdp_response {
	u64 revision;
	union {
		void* virtual;
		physaddr_t physical; /* Base revision 3 */
	};
};

struct limine_rsdp_request {
	struct limine_request request;
	struct limine_rsdp_response* response;
};

struct limine_smbios_response {
	u64 revision;
	union {
		physaddr_t physical; /* Base revision 3 and 4 */
		void* virtual;
	} entry_32;
	union {
		physaddr_t physical; /* Base revision 3 and 4 */
		void* virtual;
	} entry_64;
};

struct limine_smbios_request {
	struct limine_request request;
	struct limine_smbios_response* response;
};

struct limine_executable_address_response {
	u64 revision;
	physaddr_t physical;
	const void* virtual;
};

struct limine_executable_address_request {
	struct limine_request request;
	struct limine_executable_address_response* response;
};

extern struct limine_executable_file_request g_limine_executable_file_request;

/**
 * @brief Given a list of revisions, return the current base revision if it's in the list
 *
 * Useful for drivers or subsystems that need a specific base revision, or support multiple
 * base revisions.
 *
 * @param revisions The revisions to check
 * @param revision_count The number of revisions
 *
 * @return The current base revision if it's in the list, otherwise -1 is returned
 */
int limine_match_base_revision(int* revisions, size_t revision_count);

/**
 * @brief Get the base revision the bootloader used
 * @return The base revision
 */
int limine_base_revision(void);
