#pragma once

#include <crater/errno.h>
#include <crater/compiler.h>
#include <arch/usercopy.h>

#define IS_USER_ADDRESS(p) ARCH_IS_USER_ADDRESS(p)

/**
 * @brief Check if a user address is a user address
 *
 * @param ptr The pointer to check
 * @param size The number of bytes being accessed
 *
 * @retval true Address is a user address
 * @retval false Address is not a user address
 */
static inline bool usercopy_access_ok(const void __user* ptr, size_t size) {
	uintptr_t end;
	if (__builtin_add_overflow((uintptr_t)ptr, size ? size - 1 : 0, &end))
		return false;
	return IS_USER_ADDRESS(ptr) && IS_USER_ADDRESS((const void __user*)end);
}

#define __usercopy_read(ptr, val, type, accessor) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(type) && sizeof(*(val)) == sizeof(type), "__usercopy_read() mismatched size types"); \
		typeof(ptr) const ____ucp = (ptr); \
		typeof(val) const ____ucv = (val); \
		usercopy_access_ok(____ucp, sizeof(type)) ? accessor(____ucp, ____ucv) : -EFAULT; \
	})
#define __usercopy_write(ptr, val, type, accessor) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(type), "__usercopy_write() mismatched size types"); \
		typeof(ptr) const ____ucp = (ptr); \
		type const ____ucv = (type)(val); \
		usercopy_access_ok(____ucp, sizeof(type)) ? accessor(____ucp, ____ucv) : -EFAULT; \
	})

#define user_read_byte(ptr, val) __usercopy_read(ptr, val, u8, arch_user_read_byte)
#define user_read_word(ptr, val) __usercopy_read(ptr, val, u16, arch_user_read_word)
#define user_read_dword(ptr, val) __usercopy_read(ptr, val, u32, arch_user_read_dword)
#define user_read_qword(ptr, val) __usercopy_read(ptr, val, u64, arch_user_read_qword)
#define user_write_byte(ptr, val) __usercopy_write(ptr, val, u8, arch_user_write_byte)
#define user_write_word(ptr, val) __usercopy_write(ptr, val, u16, arch_user_write_word)
#define user_write_dword(ptr, val) __usercopy_write(ptr, val, u32, arch_user_write_dword)
#define user_write_qword(ptr, val) __usercopy_write(ptr, val, u64, arch_user_write_qword)

/**
 * @brief Enable user copies
 */
void usercopy_enable(void);

/**
 * @brief Disable user copies
 *
 * Acts like a preempt count, 2 enables require 2 disables.
 */
void usercopy_disable(void);

/**
 * @brief Set bytes in user space to a value
 *
 * @param dest Destination pointer
 * @param val The value to set, should be an 8 bit value
 * @param count Number of bytes to set
 *
 * @retval -EFAULT Failed to access memory
 * @retval 0 Successful
 */
int usercopy_memset(void __user* dest, int val, size_t count);

/**
 * @brief Copy bytes from user space to kernel space
 *
 * @param dest Destination pointer
 * @param src The source
 * @param count The number of bytes to copy
 *
 * @retval -EFAULT Failed to access memory
 * @retval 0 Successful
 */
int usercopy_from_user(void* dest, const void __user* src, size_t count);

/**
 * @brief Copy bytes from kernel space to user space
 *
 * @param dest The destination pointer
 * @param src The source pointer
 * @param count The number of bytes to copy
 *
 * @retval -EFAULT Failed to access memory
 * @retval 0 Successful
 */
int usercopy_to_user(void __user* dest, const void* src, size_t count);

/**
 * @brief Get the length of a string in user space
 * @param str The string
 * @return -EFAULT on failure, or the length of the string on success
 */
ssize_t usercopy_strlen(const char __user* str);
