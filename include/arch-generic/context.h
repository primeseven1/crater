#pragma once

#include <crater/types.h>

struct arch_context;
struct context;
struct thread;

/**
 * @brief Do a context switch
 *
 * IRQ's are off when this function is called.
 * 
 * @param current The current thread
 * @param next The thread to switch to
 */
void arch_context_switch(struct thread* current, struct thread* next);

/**
 * @brief Initialize a context structure
 * @return -errno on failure, 0 on success
 */
int arch_context_init(struct context* ctx);

/**
 * @brief Destroy a context
 * @param ctx The context to destroy
 */
void arch_context_destroy(struct context* ctx);

/**
 * @brief Prepare a thread for execution
 *
 * @param ctx The context to prepare
 * @param ip The instruction pointer
 * @param sp The stack pointer
 */
void arch_context_prepare_execution(struct arch_context* ctx, uintptr_t ip, uintptr_t sp);
