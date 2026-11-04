#pragma once

#include <crater/types.h>

/**
 * @brief Write an 8 bit value to a hardware port
 * @param port The port to write to
 * @param val The value to write
 */
static inline void arch_x86_64_outb(u16 port, u8 val) {
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Write a 16 bit value to a hardware port
 * @param port The port to write to
 * @param val The value to write
 */
static inline void arch_x86_64_outw(u16 port, u16 val) {
	__asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Write a 32 bit value to a hardware port
 * @param port The port to write to
 * @param val The value to write
 */
static inline void arch_x86_64_outl(u16 port, u32 val) {
	__asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Read an 8 bit value from a hardware port
 * @param port The port to read from
 * @return The value at that port
 */
static inline u8 arch_x86_64_inb(u16 port) {
	u8 ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

/**
 * @brief Read a 16 bit value from a hardware port
 * @param port The port to read from
 * @return The value at that port
 */
static inline u16 arch_x86_64_inw(u16 port) {
	u16 ret;
	__asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

/**
 * @brief Read a 32 bit value from a hardware port
 * @param port The port to read from
 * @return The value at that port
 */
static inline u32 arch_x86_64_inl(u16 port) {
	u32 ret;
	__asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

/**
 * @brief Introduce an I/O delay
 *
 * Useful for giving time for slower hardware to process
 * commands.
 */
static inline void arch_x86_64_pmio_delay(void) {
	arch_x86_64_outb(0x80, 0);
}
