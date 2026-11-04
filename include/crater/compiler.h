#pragma once

#define likely(c) __builtin_expect(!!(c), 1)
#define unlikely(c) __builtin_expect(!!(c), 0)
#define compiler_barrier() __asm__ volatile("" : : : "memory")

#if defined(__CHECKER__)

#define __user __attribute__((noderef, address_space(1)))
#define __iomem __attribute__((noderef, address_space(2)))
#define __force __attribute__((force))
#define __visible __attribute__((externally_visible))

#else /* __CHECKER__ */

#define __user
#define __iomem
#define __force
#define __visible

#endif /* __CHECKER__ */

#if defined(__clang__)

#define __do_pragma(x) _Pragma(#x)
#define __diag_push() __do_pragma(clang diagnostic push)
#define __diag_ignore(w) __do_pragma(clang diagnostic ignored w)
#define __diag_pop() __do_pragma(clang diagnostic pop)

#elif defined(__GNUC__)

#define __do_pragma(x) _Pragma(#x)
#define __diag_push() __do_pragma(GCC diagnostic push)
#define __diag_ignore(w) __do_pragma(GCC diagnostic ignored w)
#define __diag_pop() __do_pragma(GCC diagnostic pop)

#else

#error "Unsupported C compiler"

#endif /* __clang__/__GNUC__ */
