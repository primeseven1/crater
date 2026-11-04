#pragma once

#include <crater/types.h>

#if !defined(ARCH_IRQ_ENABLED) && !defined(ARCH_IRQ_DISABLED)
#define ARCH_IRQ_ENABLED 1
#define ARCH_IRQ_DISABLED 0
#endif /* !defined(ARCH_IRQ_ENABLED) && !defined(ARCH_IRQ_DISABLED) */

/**
 * @brief Read IRQ flags without disabling
 *
 * If the target architecture uses this header file, then
 * this function must return either ARCH_IRQ_ENABLED or ARCH_IRQ_DISABLED with
 * no other flags involved.
 *
 * @return The IRQ state
 */
unsigned long arch_local_irq_read(void);

/**
 * @brief Restore IRQ state from flags
 * @param flags The IRQ flags
 */
void arch_local_irq_restore(unsigned long flags);

static inline void arch_local_irq_enable(void) {
	arch_local_irq_restore(ARCH_IRQ_ENABLED);
}

static inline void arch_local_irq_disable(void) {
	arch_local_irq_restore(ARCH_IRQ_DISABLED);
}

static inline unsigned long arch_local_irq_save(void) {
	unsigned long flags = arch_local_irq_read();
	arch_local_irq_restore(ARCH_IRQ_DISABLED);
	return flags;
}

static inline bool arch_local_irq_disabled(unsigned long flags) {
	return flags == ARCH_IRQ_DISABLED;
}
