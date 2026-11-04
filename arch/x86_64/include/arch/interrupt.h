#pragma once

#include <arch/context.h>
#include <arch-generic/interrupt.h>

struct arch_isr {
	int id, flags;
	void (*ehandler)(struct isr*, struct arch_context*);
};
