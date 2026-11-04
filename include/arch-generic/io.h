#pragma once

#include <crater/types.h>
#include <crater/compiler.h>

static inline void writeb(u8 __iomem* ptr, u8 val) {
	*(u8 volatile __force*)ptr = val;
}

static inline void writew(u16 __iomem* ptr, u16 val) {
	*(u16 volatile __force*)ptr = val;
}

static inline void writel(u32 __iomem* ptr, u32 val) {
	*(u32 volatile __force*)ptr = val;
}

static inline void writeq(u64 __iomem* ptr, u64 val) {
	*(u64 volatile __force*)ptr = val;
}

static inline u8 readb(const u8 __iomem* ptr) {
	return *(u8 volatile __force*)ptr;
}

static inline u16 readw(const u16 __iomem* ptr) {
	return *(u16 volatile __force*)ptr;
}

static inline u32 readl(const u32 __iomem* ptr) {
	return *(u32 volatile __force*)ptr;
}

static inline u64 readq(const u64 __iomem* ptr) {
	return *(u64 volatile __force*)ptr;
}
