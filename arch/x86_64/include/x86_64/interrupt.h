#pragma once

#include <crater/interrupt.h>

/**
 * @brief Register an ISR at a specific vector
 *
 * @param isr The ISR to register
 * @param vector The IDT vector
 * @param handler The ISR handler
 * @param private ISR private data
 * @param flags ISR_FLAG_* flags
 * @param need_eoi Whether or not the ISR needs an EOI signal
 *
 * @retval -EEXIST Handler already exists at the vector
 * @return 0 on success, -errno on failure
 */
int arch_x86_64_register_isr_vector(struct isr* isr, int vector, isrhandler_t handler, void* private, int flags, bool need_eoi);
