#pragma once

struct isr;

/**
 * @brief Register an ISR handler
 *
 * Once registered, the function associated with the ISR must be called when this interrupt happens.
 * The function expects IRQ's to be off.
 *
 * @param isr The ISR to register
 * @return 0 on success, -errno on failure
 */
int arch_register_isr(struct isr* isr);

/**
 * @brief Unregister an ISR handler
 *
 * @param isr The ISR to unregister
 *
 * @retval -ENOSYS Unimplemented
 * @return 0 on success, -errno on failure
 */
int arch_unregister_isr(struct isr* isr);
