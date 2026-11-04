#pragma once

#include <arch/interrupt.h>
#include <crater/slab.h>

#define ISR_FLAG_TYPE_MASK (0b11 << 0)
#define ISR_FLAG_TYPE_IRQ (0b00 << 0)
#define ISR_FLAG_TYPE_LIRQ (0b01 << 0)
#define ISR_FLAG_TYPE_SGI (0b10 << 0)

typedef void (*isrhandler_t)(struct isr*);

struct isr {
	isrhandler_t handler; /* The function to call on interrupt */
	int flags; /* ISR_FLAG_* flags */
	void* private; /* For use by whoever registers the interrupt */
	struct arch_isr arch_specific; /* Any architecture specific data */
};

/**
 * @brief Allocate an ISR structure
 *
 * The structure's refcount is initialized to zero, however everything else
 * can remain uninitialized.
 *
 * @return The ISR
 */
static inline struct isr* alloc_isr(void) {
	struct isr* ret = kmalloc(sizeof(*ret), MM_ZONE_NORMAL);
	return ret;
}

/**
 * @brief Free an ISR structure
 * @param isr The ISR to free
 */
static inline void free_isr(struct isr* isr) {
	kfree(isr);
}

/**
 * @brief Initialize the generic part of an ISR struct
 *
 * This is almost never to be called directly, but can be in some special cases
 *
 * @param isr The ISR to initialize
 * @param handler The handler to use
 * @param private ISR specific data
 * @param flags ISR_FLAG_* flags
 */
static inline void __init_isr(struct isr* isr, isrhandler_t handler, void* private, int flags) {
	isr->handler = handler;
	isr->private = private;
	isr->flags = flags;
}

/**
 * @brief Register an ISR handler
 *
 * @param isr The ISR to register
 * @param handler The ISR handler
 * @param private Pointer to put into isr->private, can be used for whatever purpose
 * @param flags Flags for how the ISR is used
 *
 * @return -errno on failure, 0 on success
 */
static inline int register_isr(struct isr* isr, isrhandler_t handler, void* private, int flags) {
	__init_isr(isr, handler, private, flags);
	return arch_register_isr(isr);
}

/**
 * @brief Unregister an ISR handler
 * @param isr The ISR to unregister
 * @return -errno on failure, 0 on success
 */
static inline int unregister_isr(struct isr* isr) {
	return arch_unregister_isr(isr);
}

typedef void (*softirqhandler_t)(void);

enum softirq {
	SOFTIRQ_TIMER,
	SOFTIRQ_COUNT
};

/**
 * @brief Register a softirq action
 *
 * @param softirq The softirq to register
 * @param action The action to call
 */
int softirq_register(enum softirq softirq, softirqhandler_t action);

/**
 * @brief Raise a softirq
 * @param softirq The softirq to raise
 */
void softirq_raise(enum softirq softirq);

/**
 * @brief Execute softirqs
 *
 * Only meant to be called at the end of an IRQ handler.
 */
void softirq_execute(void);

/**
 * @brief Enable softirqs on the current CPU
 */
void local_softirq_enable(void);

/**
 * @brief Disable softirqs on the current CPU
 */
void local_softirq_disable(void);
