#pragma once

#include <crater/types.h>
#include <crater/atomic.h>

struct arch_limine_mp_info;

enum arch_limine_mp_flags {
	ARCH_X86_64_LIMINE_MP_REQUEST_X2APIC = (1 << 0),
	ARCH_X86_64_LIMINE_MP_RESPONSE_X2APIC = (1 << 0)
};

struct arch_limine_mp_info {
	u32 processor_id;
	u32 lapic_id;
	u64 __reserved;
	void (* __atomic goto_address)(struct arch_limine_mp_info*);
	u64 extra_argument;
};

struct arch_limine_mp_response {
	u64 revision;
	u32 flags;
	u32 bsp_lapic_id;
	u64 cpu_count;
	struct arch_limine_mp_info** cpus;
};

enum arch_limine_paging_mode {
	ARCH_X86_64_LIMINE_PAGING_MODE_4LVL = 0,
	ARCH_X86_64_LIMINE_PAGING_MODE_5LVL = 1
};
