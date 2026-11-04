#pragma once

#include <crater/types.h>
#include <crater/errno.h>

/**
 * @brief Convert an unsigned long long integer to a string
 *
 * Guaranteed to be null terminated unless dsize is zero.
 * The string is garbage if an error code is returned.
 *
 * @param dest The destination for the string to go
 * @param x The value to convert
 * @param base The base to convert to
 * @param size The destination size
 *
 * @retval -EOVERFLOW Cannot fit the whole string in the buffer
 * @retval -EINVAL Invalid base
 */
int kulltostr(char* dest, unsigned long long x, unsigned int base, size_t dsize);

/**
 * @brief Convert a long long integer to a string
 *
 * Guaranteed to be null terminated unless dsize is zero.
 * The string is garbage if an error code is returned.
 *
 * @param dest The destination for the string to go
 * @param x The value to convert
 * @param base The base to convert to
 * @param dsize The destination size
 *
 * @retval -EOVERFLOW Cannot fit the whole string in the buffer
 * @retval -EINVAL Invalid base
 */
int klltostr(char* dest, long long x, unsigned int base, size_t dsize);

/**
 * @brief Convert a string to an unsigned long long
 *
 * @param str The string to convert
 * @param base The base of the integer
 * @param res The pointer to the result
 *
 * @retval 0 Successful
 * @retval -ERANGE Overflow
 * @retval -EINVAL str is NULL, invalid string, or invalid base
 */
int kstrtoull(const char* str, unsigned int base, unsigned long long* res);

/**
 * @brief Convert a string to a long long
 *
 * @param str The string to convert
 * @param base The base of the integer
 * @param res The pointer to the result
 *
 * @retval 0 Successful
 * @retval -ERANGE Overflow
 * @retval -EINVAL str is NULL, invalid string, or invalid base
 */
int kstrtoll(const char* str, unsigned int base, long long* res);
