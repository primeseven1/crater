#pragma once

#include <arch-generic/processor.h>

#define ARCH_SMP_MAX_CPUS 255

#define arch_cpu_relax() __asm__ volatile("pause" : : : "memory")
#define arch_cpu_idle() __asm__ volatile("hlt" : : : "memory")
