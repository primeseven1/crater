#pragma once

#include <crater/types.h>
#include <crater/errno.h>

#define PRINTK_DBG_N '\006'
#define PRINTK_INFO_N '\005'
#define PRINTK_WARN_N '\004'
#define PRINTK_ERR_N '\003'
#define PRINTK_CRIT_N '\002'
#define PRINTK_EMERG_N '\001'
#define PRINTK_MIN_N PRINTK_EMERG_N
#define PRINTK_MAX_N PRINTK_DBG_N

#define PRINTK_DBG "\001\006"
#define PRINTK_INFO "\001\005"
#define PRINTK_WARN "\001\004"
#define PRINTK_ERR "\001\003"
#define PRINTK_CRIT "\001\002"
#define PRINTK_EMERG "\001\001"

/**
 * @brief Set a printk hook
 *
 * This function is called regardless of the current printk level.
 *
 * The implementation of this function should be very fast, as the function
 * can be called during an interrupt context.
 *
 * @param hook The hook to use (parameters: message level, global level, message)
 *
 * @retval 0 Successful
 * @retval -EEXIST Hook already exists
 */
int printk_set_hook(void (*hook)(int, int, const char*));

/**
 * @brief Set the current global printk level
 *
 * This function ignores invalid levels.
 *
 * @param level The level to set
 */
void printk_set_level(int level);

/**
 * @brief Like vprintf
 *
 * @param fmt The format string
 * @param va The variable argument list
 *
 * @return The length of the string when formatted
 */
int vprintk(const char* fmt, va_list va);

/**
 * @brief Like printf
 *
 * @param fmt The format string
 * @param ... Variable arguments for the format string
 *
 * @return The length of the string when formatted
 */
__attribute__((format(printf, 1, 2)))
int printk(const char* fmt, ...);

/**
 * @brief Disable a ringbuffer and flush the ringbuffer
 * 
 * Only used when in a panic. Calling this function at any other time
 * is unsafe.
 */
void printk_disable_ringbuffer_and_flush(void);
