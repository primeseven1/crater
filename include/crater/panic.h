#pragma once

#include <crater/compiler.h>

__attribute__((format(printf, 1, 2)))
_Noreturn void panic(const char* fmt, ...);

#define bug(c) \
	do { \
		if (unlikely(!!(c))) \
			panic("%s:%d: %s bug!", __FILE__, __LINE__, #c); \
	} while (0)
