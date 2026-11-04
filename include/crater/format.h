#pragma once

#include <crater/types.h>

/**
 * @brief Write a formatted string to a buffer
 *
 * @param dest The destination buffer
 * @param dsize The size of the destination
 * @param fmt The format string
 * @param va Variable arguments
 *
 * @return The number of characters that would be written assuming a sufficient buffer size, or -1 on error
 */
int vsnprintf(char* dest, size_t dsize, const char* fmt, va_list va);

/**
 * @brief Write a formatted string to a buffer
 *
 * This function initializes a variable argument list and then calls
 * vsnprintf.
 *
 * @param dest The destination buffer
 * @param dsize The size of the destination
 * @param fmt The format string
 * @param ... Variable arguments
 *
 * @return The number of characters that would be written assuming a sufficient buffer size, or -1 on error
 */
__attribute__((format(printf, 3, 4)))
int snprintf(char* dest, size_t dsize, const char* fmt, ...);
