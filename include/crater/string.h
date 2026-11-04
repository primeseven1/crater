#pragma once

#include <crater/types.h>
#include <crater/mm.h>

/**
 * @brief Set a block of memory to a specified value
 *
 * @param dest The pointer to the memory block
 * @param val The value to set the memory block to
 * @param count The number of bytes to set
 *
 * @return The original destination pointer
 */
void* memset(void* dest, int val, size_t count);

/**
 * @brief Copy a block of memory from one location to another
 *
 * @param dest The block to copy the memory to
 * @param src The memory block to copy from
 * @param count The number of bytes to copy
 *
 * @return The original destination pointer
 */
void* memcpy(void* dest, const void* src, size_t count);

/**
 * @brief Copy a block of memory from one location to another
 *
 * Unlike memcpy, this function does handle overlapping memory regions.
 *
 * @param dest The block to copy the memory to
 * @param src The memory block to copy from
 * @param count The number of bytes to copy
 *
 * @return The original destination pointer
 */
void* memmove(void* dest, const void* src, size_t count);

/**
 * @brief Compare two blocks of memory
 *
 * @param b1 The first memory block
 * @param b2 The second memory block
 * @param count The number of bytes to compare
 *
 * @return The difference of the last compared byte
 */
int memcmp(const void* b1, const void* b2, size_t count);

/**
 * @brief Find a byte in a memory block
 *
 * @param ptr The pointer to the block
 * @param val The byte to search for
 * @param count The number of bytes in the memory block
 *
 * @return A pointer to the first occurrence of the value, or NULL
 */
void* memchr(const void* ptr, int val, size_t count);

/**
 * @brief Get the length of a null terminated string
 * @param str The string to get the length of
 * @return The length of the string excluding the null terminator
 */
size_t strlen(const char* str);

/**
 * @brief Copy a string from one location to another
 *
 * @param dest Where the string should be copied to
 * @param src The source string
 *
 * @return The original destination pointer
 */
char* strcpy(char* dest, const char* src);

/**
 * @brief Copy a string from one location to another
 *
 * @param dest Where the string should be copied to
 * @param src The source string
 * @param count The maximum number of characters to copy
 *
 * @return The original destination pointer
 */
char* strncpy(char* dest, const char* src, size_t count);

/**
 * @brief Copy a string from one location to another
 *
 * @param dest Where the string should be copied to
 * @param src The source string
 * @param dsize The size of the destination buffer
 *
 * @return The length of the source string
 */
size_t strlcpy(char* dest, const char* src, size_t dsize);

/**
 * @brief Concatenate two strings
 *
 * @param dest The string to concatenate
 * @param src The source string
 * 
 * @return The original destination pointer
 */
char* strcat(char* dest, const char* src);

/**
 * @brief Concatenate two strings
 *
 * @param dest The string to concatenate
 * @param src The source string
 * @param count The maximum number of characters to add
 *
 * @return The original destination pointer
 */
char* strncat(char* dest, const char* src, size_t count);

/**
 * @brief Concatenate two strings
 *
 * To check for error, check to see if length of the destination
 * string equals the return value
 *
 * @param dest The string to concatenate
 * @param src The source string
 * @param dsize The size of the destination
 *
 * @return The length of the destination plus the length of the source
 */
size_t strlcat(char* dest, const char* src, size_t dsize);

/**
 * @brief Compare two strings
 *
 * Compares one character at a time. When a character is not equal,
 * it returns the difference of the characters. Zero is returned if
 * the strings are equal.
 *
 * @param s1 The first string
 * @param s2 The second string
 *
 * @return The difference of the last compared character
 */
int strcmp(const char* s1, const char* s2);

/**
 * @brief Compare two strings
 *
 * @param s1 The first string
 * @param s2 The second string
 * @param count The maximum number of characters to compare
 *
 * @return The difference of the last compared character
 */
int strncmp(const char* s1, const char* s2, size_t count);

/**
 * @brief Look for a character in a string
 *
 * @param str The string
 * @param c The character to look for
 *
 * @return Pointer to the first occurrence of the character, or NULL
 */
char* strchr(const char* str, int c);

/**
 * @brief Look for the last occurrence of a character in a string
 *
 * @param str The string
 * @param c The character to look for
 *
 * @return Pointer to the last occurrence of the character, or NULL
 */
char* strrchr(const char* str, int c);

/**
 * @brief Tokenize a string into substrings using delimiters
 *
 * @param str The string to tokenize on first call, NULL to continue.
 * @param delim A string that has delimiter characters to separate tokens
 * @param saveptr A pointer to be used internally to maintain successive calls
 *
 * @return A pointer to the next token, or NULL if there are no more tokens
 */
char* strtok_r(char* str, const char* delim, char** saveptr);

/**
 * @brief Duplicate a string
 *
 * The string returned should be freed with kfree().
 *
 * @param str The string to duplicate
 * @param mm_flags Flags for the allocation
 *
 * @return A pointer to the string
 */
char* kstrdup(const char* str, mm_t mm_flags);
