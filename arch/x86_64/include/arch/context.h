#pragma once

#include <crater/types.h>
#include <x86_64/asm/offsets.h>
#include <arch-generic/context.h>

#define ARCH_CODE_ADDRESS(t, fn) ((t)(fn))

struct arch_context {
	u64 ds, es;
	u64 cr2;
	u64 rax, rbx, rcx, rdx, rsi, rdi;
	u64 rbp;
	u64 r8, r9, r10, r11, r12, r13, r14, r15;
	u64 vector, err_code;
	u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed, aligned(8)));
static_assert(offsetof(struct arch_context, ds) == ARCH_X86_64_CTX_DS_OFFSET && offsetof(struct arch_context, es) == ARCH_X86_64_CTX_ES_OFFSET
		&& offsetof(struct arch_context, cr2) == ARCH_X86_64_CTX_CR2_OFFSET && offsetof(struct arch_context, rax) == ARCH_X86_64_CTX_RAX_OFFSET
		&& offsetof(struct arch_context, rbx) == ARCH_X86_64_CTX_RBX_OFFSET && offsetof(struct arch_context, rcx) == ARCH_X86_64_CTX_RCX_OFFSET
		&& offsetof(struct arch_context, rdx) == ARCH_X86_64_CTX_RDX_OFFSET && offsetof(struct arch_context, rsi) == ARCH_X86_64_CTX_RSI_OFFSET
		&& offsetof(struct arch_context, rdi) == ARCH_X86_64_CTX_RDI_OFFSET && offsetof(struct arch_context, rbp) == ARCH_X86_64_CTX_RBP_OFFSET
		&& offsetof(struct arch_context, r8) == ARCH_X86_64_CTX_R8_OFFSET && offsetof(struct arch_context, r9) == ARCH_X86_64_CTX_R9_OFFSET
		&& offsetof(struct arch_context, r10) == ARCH_X86_64_CTX_R10_OFFSET && offsetof(struct arch_context, r11) == ARCH_X86_64_CTX_R11_OFFSET
		&& offsetof(struct arch_context, r12) == ARCH_X86_64_CTX_R12_OFFSET && offsetof(struct arch_context, r13) == ARCH_X86_64_CTX_R13_OFFSET
		&& offsetof(struct arch_context, r14) == ARCH_X86_64_CTX_R14_OFFSET && offsetof(struct arch_context, r15) == ARCH_X86_64_CTX_R15_OFFSET
		&& offsetof(struct arch_context, vector) == ARCH_X86_64_CTX_VECTOR_OFFSET && offsetof(struct arch_context, err_code) == ARCH_X86_64_CTX_ERR_CODE_OFFSET
		&& offsetof(struct arch_context, rip) == ARCH_X86_64_CTX_RIP_OFFSET && offsetof(struct arch_context, cs) == ARCH_X86_64_CTX_CS_OFFSET
		&& offsetof(struct arch_context, rflags) == ARCH_X86_64_CTX_RFLAGS_OFFSET && offsetof(struct arch_context, rsp) == ARCH_X86_64_CTX_RSP_OFFSET
		&& offsetof(struct arch_context, ss) == ARCH_X86_64_CTX_SS_OFFSET, "struct arch_context offsets are wrong");

#define ARCH_CONTEXT_IP(c) ((c)->rip)
#define ARCH_CONTEXT_SP(c) ((c)->rsp)

struct arch_x86_64_fxsave_context {
	u16 fcw;
	u16 fsw;
	u8 ftw;
	u8 __reserved0;
	u16 fop;
	u64 fip;
	u64 fdp;
	u32 mxcsr;
	u32 mxcsr_mask;
	u8 st_mm[8][16];
	u8 xmm[16][16];
	u8 __reserved1[96];
} __attribute__((packed, aligned(16)));
static_assert(sizeof(struct arch_x86_64_fxsave_context) == 512, "struct arch_x86_64_fxsave_context must be exactly 512 bytes");

struct arch_context_extended {
	void* user_fsbase, *user_gsbase;
	union {
		struct arch_x86_64_fxsave_context* fxsave_region;
		void* xsave_region;
	} un;
};
