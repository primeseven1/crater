#pragma once

#include <crater/errno.h>

#define ARCH_IS_USER_ADDRESS(p) ((uintptr_t)(p) <= 0x7FFFFFFFFFFFFFFF)

#define __ARCH_X86_64_ASM_EXTABLE(fault, fixup) \
	".section .extable, \"a\"\n" \
	".balign 8\n\t" \
	".long (" #fault ")-.,(" #fixup ")-.\n\t" \
	".previous\n"
#define __arch_x86_64_user_read(ptr, instr, val) \
	({ \
		int __err; \
		typeof(*(val)) __val; \
		__asm__ volatile("1: " instr " (%2), %1\n\t" \
				"mov $0, %0\n\t" \
				"jmp 4f\n" \
				".rept 13\n\tnop\n\t.endr\n\t" \
				".section .fixup, \"ax\"\n\t" \
				"3: mov %3, %0\n\t" \
				"mov $0, %1\n\t" \
				"jmp 4f\n" \
				".previous\n" \
				__ARCH_X86_64_ASM_EXTABLE(1b, 3b) \
				"4:" \
				: "=r"(__err), "=&r"(__val) \
				: "r"(ptr), "i"(-EFAULT) \
				: "memory"); \
		*(val) = __val; \
		__err; \
	})
#define __arch_x86_64_user_write(ptr, instr, val) \
	({ \
		int __err; \
		__asm__ volatile("1: " instr " %1, (%2)\n" \
				"mov $0, %0\n" \
				"jmp 4f\n" \
				".rept 13\n\tnop\n\t.endr\n\t" \
				".section .fixup, \"ax\"\n" \
				"3: mov %3, %0\n" \
				"jmp 4f\n" \
				".previous\n" \
				__ARCH_X86_64_ASM_EXTABLE(1b, 3b) \
				"4:" \
				: "=r"(__err) \
				: "r"(val), "r"(ptr), "i"(-EFAULT) \
				: "memory"); \
		__err; \
	})

#define arch_user_read_byte(ptr, val) __arch_x86_64_user_read(ptr, "movb", val)
#define arch_user_read_word(ptr, val) __arch_x86_64_user_read(ptr, "movw", val)
#define arch_user_read_dword(ptr, val) __arch_x86_64_user_read(ptr, "movl", val)
#define arch_user_read_qword(ptr, val) __arch_x86_64_user_read(ptr, "movq", val)

#define arch_user_write_byte(ptr, val) __arch_x86_64_user_write(ptr, "movb", (u8)(val))
#define arch_user_write_word(ptr, val) __arch_x86_64_user_write(ptr, "movw", (u16)(val))
#define arch_user_write_dword(ptr, val) __arch_x86_64_user_write(ptr, "movl", (u32)(val))
#define arch_user_write_qword(ptr, val) __arch_x86_64_user_write(ptr, "movq", (u64)(val))

void arch_usercopy_enter(void);
void arch_usercopy_exit(void);
