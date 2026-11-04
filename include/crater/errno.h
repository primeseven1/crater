#pragma once

#include <crater/types.h>
#include <crater/compiler.h>
#include <arch/asm/errno.h>

#define ERR_PTR(e) ((void*)((intptr_t)(e)))
#define ERR_PTR_AS(t, e) ((t)((intptr_t)(e)))
#define PTR_ERR(p) ((int)((intptr_t __force)(p)))
#define IS_PTR_ERR(p) ((uintptr_t __force)(p) > (uintptr_t)(-ERRNO_MAX))
