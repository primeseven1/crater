#pragma once

#include <crater/percpu.h>
#include <crater/interrupt.h>
#include <crater/init.h>

struct irqctl_ops {
	int (*init_bsp)(void);
	int (*init_ap)(void);
	int (*install)(unsigned int, const struct cpu*, const struct isr*, int);
	int (*uninstall)(unsigned int);
	int (*disable)(unsigned int);
	int (*enable)(unsigned int);
	int (*send_ipi)(const struct cpu*, const struct isr*, int);
	void (*eoi)(const struct isr*);
	bool (*is_pending)(unsigned int);
};

struct irqctl {
	const char* name;
	unsigned int rating;
	const struct irqctl_ops* ops;
	struct init_task** dependencies;
};

#define __irqctl __attribute__((section(".irqctl"), aligned(8), used))

typedef enum {
	IRQ_HANDLED,
	IRQ_NONE
} irqreturn_t;

#define IRQ_FLAG_TRIGGER_MASK (0b11 << 1)

#define IRQ_FLAG_SHARED (0b1 << 0)
#define IRQ_FLAG_TRIGGER_HIGH (0b00 << 1)
#define IRQ_FLAG_TRIGGER_LOW (0b01 << 1)
#define IRQ_FLAG_TRIGGER_RISING (0b10 << 1)
#define IRQ_FLAG_TRIGGER_FALLING (0b11 << 1)
#define IRQ_FLAG_LOCAL (1 << 3)

typedef irqreturn_t (*irqhandler_t)(unsigned int, void*);

/**
 * @brief Send an EOI signal
 *
 * The ISR must have an IRQ structure as it's private data in order
 * for this to function correctly.
 *
 * @param isr The ISR to send
 */
void irqctl_eoi(const struct isr* isr);

/**
 * @brief Send an inter-processor interrupt
 *
 * @param cpu The CPU to send the IPI to
 * @param isr The ISR for that CPU to run
 * @param flags Unimplemented
 *
 * @return -errno on failure, 0 on success
 */
int irqctl_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags);

/**
 * @brief Disable an IRQ
 *
 * Not safe to call from an atomic context.
 *
 * @param irqnum The IRQ number
 */
void disable_irq(unsigned int irqnum);

/**
 * @brief Enable an IRQ
 *
 * Not safe to call from an atomic context
 *
 * @param irqnum The IRQ number
 */
void enable_irq(unsigned int irqnum);

/**
 * @brief Synchronize an IRQ handler
 * @param irqnum The IRQ number
 */
void synchronize_irq(unsigned int irqnum);

/**
 * @brief Request an IRQ number
 *
 * @param irqnum The IRQ number
 * @param handler The IRQ handler
 * @param flags IRQ flags
 * @param devname Name of the device
 * @param devid Device ID
 *
 * @retval -EEXIST IRQ handler already exists
 * @retval -ENOMEM Out of memory
 * @retval -EBUSY IRQ already in use, this can also be returned if there is a flag mismatch
 * @retval 0 Successful
 */
int request_irq(unsigned int irqnum, irqhandler_t handler, int flags, const char* devname, void* devid);

/**
 * @brief Free an IRQ handler
 *
 * If devid is NULL, this function is a no-op.
 *
 * Unimplemented right now.
 *
 * @param irqnum The IRQ number associated with devid
 * @param devid The device ID
 */
void free_irq(unsigned int irqnum, void* devid);

/**
 * @brief Execute pending IRQ's
 *
 * Should only ever be called from an interrupt context
 * to avoid deadlocks
 */
void do_pending_irqs(void);

static inline unsigned long local_irq_save(void) {
	return arch_local_irq_save();
}

static inline void local_irq_restore(unsigned long flags) {
	arch_local_irq_restore(flags);
}

static inline void local_irq_disable(void) {
	arch_local_irq_disable();
}

static inline void local_irq_enable(void) {
	arch_local_irq_enable();
}

static inline bool local_irq_disabled(unsigned long flags) {
	return arch_local_irq_disabled(flags);
}

static inline unsigned long local_irq_read(void) {
	return arch_local_irq_read();
}
