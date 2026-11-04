#pragma once

#include <arch/interrupt.h>

void arch_x86_64_general_protection_fault(struct isr* isr, struct arch_context* ctx);
void arch_x86_64_page_fault(struct isr* isr, struct arch_context* ctx);
