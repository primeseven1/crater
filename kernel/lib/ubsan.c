#ifdef CONFIG_UBSAN

#include <crater/panic.h>
#include <crater/printk.h>
#include <crater/trace.h>

struct source {
	const char* file_name;
	u32 line;
	u32 column;
};

struct type_desc {
	u16 kind;
	u16 info;
	char name[];
};

struct type_mismatch_info {
	struct source source;
	struct type_desc* type;
	u8 log_alignment;
	u8 type_check_kind;
};

struct out_of_bounds_info {
	struct source source;
	const struct type_desc* array_type;
	const struct type_desc* index_type;
};

struct non_null_return_info {
	struct source attr;
};

struct pointer_overflow_info {
	struct source source;
};

struct invalid_value_info {
	struct source source;
	const struct type_desc* type;
};

struct unreachable_info {
	struct source source;
};

struct non_null_arg_info {
	struct source source;
	struct source attr_source;
	int arg_index;
};

struct shift_out_of_bounds_info {
	struct source source;
	const struct type_desc* lhs_type;
	const struct type_desc* rhs_type;
};

struct overflow_info {
	struct source source;
	const struct type_desc* type;
};

struct vla_bound_info {
	struct source source;
	const struct type_desc* type;
};

struct invalid_builtin_info {
	struct source source;
	u8 kind;
};

struct function_type_mismatch_info {
	struct source source;
	const struct type_desc* type;
};

enum type_check_kind {
	LOAD,
	STORE,
	REFERENCE_BINDING,
	MEMBER_ACCESS,
	MEMBER_CALL,
	CONSTRUCTOR_CALL,
	DOWNCAST_POINTER,
	DOWNCAST_REFERENCE,
	UPCAST,
	UPCAST_TO_VIRTUAL_BASE,
	NONNULL_ASSIGN,
	DYNAMIC_OPERATION,
};

enum error_type {
	NULL_POINTER_USE_WITH_NULLABILITY,
	NULL_POINTER_USE,
	MISALIGNED_POINTER_USE,
	INSUFFICIENT_OBJECT_SIZE,
};

static void ubsan_end(bool die) {
	if (die)
		panic("ubsan triggered crash");
	dump_stack();
	printk(PRINTK_CRIT "ubsan: Non-critical bug detected\n");
}

static const char* const type_check_kinds[] = {
	[LOAD] = "load of",
	[STORE] = "store to",
	[REFERENCE_BINDING] = "reference binding to",
	[MEMBER_ACCESS] = "member access within",
	[MEMBER_CALL] = "member call on",
	[CONSTRUCTOR_CALL] = "constructor call on",
	[DOWNCAST_POINTER] = "downcast of",
	[DOWNCAST_REFERENCE] = "downcast of",
	[UPCAST] = "upcast of",
	[UPCAST_TO_VIRTUAL_BASE] = "upcast to virtual base of",
	[NONNULL_ASSIGN] = "nonnull assignment to",
	[DYNAMIC_OPERATION] = "dynamic operation on"
};

__diag_push();
__diag_ignore("-Wmissing-prototypes");

static inline enum error_type get_err_type_from_mismatch(struct type_mismatch_info* mismatch, const void* ptr) {
	if (!ptr)
		return (mismatch->type_check_kind == NONNULL_ASSIGN) ? NULL_POINTER_USE_WITH_NULLABILITY : NULL_POINTER_USE;

	const size_t align = 1 << mismatch->log_alignment;
	return ((uintptr_t)ptr & (align - 1)) ? MISALIGNED_POINTER_USE : INSUFFICIENT_OBJECT_SIZE;
}

__visible void __ubsan_handle_type_mismatch_v1(struct type_mismatch_info* mismatch, const void* ptr) {
	enum error_type err_type = get_err_type_from_mismatch(mismatch, ptr);
	const char* file_name = mismatch->source.file_name;
	const char* type_name = mismatch->type->name;
	u32 line = mismatch->source.line;
	switch (err_type) {
	case NULL_POINTER_USE:
	case NULL_POINTER_USE_WITH_NULLABILITY:
		printk(PRINTK_EMERG "ubsan: %s:%u: %s NULL pointer of type %s\n", file_name, line, type_check_kinds[mismatch->type_check_kind], type_name);
		ubsan_end(true);
		break;
	case MISALIGNED_POINTER_USE:
		printk(PRINTK_CRIT "ubsan: %s:%u: misaligned address %p for type %s\n", file_name, line, ptr, type_name);
		ubsan_end(false);
		break;
	case INSUFFICIENT_OBJECT_SIZE:
		printk(PRINTK_EMERG "ubsan: %s:%u: %s address %p with insufficient space for an object of type %s\n",
				file_name, line, type_check_kinds[mismatch->type_check_kind], ptr, type_name);
		ubsan_end(true);
		break;
	default:
		printk(PRINTK_EMERG "ubsan: unknown error type: %d\n", err_type);
		ubsan_end(true);
		break;
	}
}

__visible void __ubsan_handle_out_of_bounds(struct out_of_bounds_info* oob, size_t index) {
	printk(PRINTK_EMERG "ubsan: %s:%u: index %zu out of bounds for type %s\n", oob->source.file_name, oob->source.line, index, oob->array_type->name);
	ubsan_end(true);
}

__visible void __ubsan_handle_nonnull_return_v1(struct non_null_return_info* nonnull, struct source* source) {
	(void)nonnull;
	printk(PRINTK_EMERG "ubsan: %s:%u: NULL pointer returned from function that should never return NULL\n", source->file_name, source->line);
	ubsan_end(true);
}

__visible void __ubsan_handle_pointer_overflow(struct pointer_overflow_info* overflow, const void* base, const void* result) {
	printk(PRINTK_EMERG "ubsan: %s:%u: Pointer %p overflowed to %p\n", overflow->source.file_name, overflow->source.line, base, result);
	ubsan_end(true);
}

__visible void __ubsan_handle_load_invalid_value(struct invalid_value_info* invalid, void* from) {
	(void)from;
	printk(PRINTK_CRIT "ubsan: %s:%u: Loaded invalid value for type %s\n", invalid->source.file_name, invalid->source.line, invalid->type->name);
	ubsan_end(false);
}

__visible void __ubsan_handle_builtin_unreachable(struct unreachable_info* unreachable) {
	printk(PRINTK_EMERG "ubsan: %s:%u: Reached an unreachable block\n", unreachable->source.file_name, unreachable->source.line);
	ubsan_end(true);
}

__visible void __ubsan_handle_nonnull_arg(struct non_null_arg_info* nonnull) {
	printk(PRINTK_EMERG "ubsan: %s:%u: nonull argument at arg index %u is NULL\n", nonnull->source.file_name, nonnull->source.line, nonnull->arg_index);
	ubsan_end(true);
}

__visible void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_info* oob, const void* lhs, const void* rhs) {
	(void)lhs;
	(void)rhs;
	printk(PRINTK_CRIT "ubsan: %s:%u: shift index >= type width\n", oob->source.file_name, oob->source.line);
	ubsan_end(false);
}

__visible void __ubsan_handle_add_overflow(struct overflow_info* overflow, size_t lhs, size_t rhs) {
	(void)lhs;
	(void)rhs;
	printk(PRINTK_CRIT "ubsan: %s:%u Add overflow", overflow->source.file_name, overflow->source.line);
	ubsan_end(false);
}

__visible void __ubsan_handle_sub_overflow(struct overflow_info* overflow, size_t lhs, size_t rhs) {
	(void)lhs;
	(void)rhs;
	printk(PRINTK_CRIT "ubsan: %s:%u Subtract overflow", overflow->source.file_name, overflow->source.line);
	ubsan_end(false);
}

__visible void __ubsan_handle_mul_overflow(struct overflow_info* overflow, size_t lhs, size_t rhs) {
	(void)lhs;
	(void)rhs;
	printk(PRINTK_CRIT "ubsan: %s:%u Multipy overflow", overflow->source.file_name, overflow->source.line);
	ubsan_end(false);
}

__visible void __ubsan_handle_negate_overflow(struct overflow_info* overflow, size_t old) {
	(void)old;
	printk(PRINTK_CRIT "ubsan: %s:%u Negate overflow",
			overflow->source.file_name, overflow->source.line);
	ubsan_end(false);
}

__visible void __ubsan_handle_divrem_overflow(struct overflow_info* overflow, size_t lhs, size_t rhs) {
	(void)lhs;
	const char* msg = rhs == 0 ? "division by zero" : "divrem overflow";
	const char* msg_lvl = rhs == 0 ? PRINTK_EMERG : PRINTK_CRIT;
	printk("%s ubsan: %s:%u: had %s\n", msg_lvl, overflow->source.file_name, overflow->source.line, msg);
	ubsan_end(rhs == 0);
}

__visible void __ubsan_handle_vla_bound_not_positive(struct vla_bound_info* info, size_t bound) {
	(void)bound;
	printk(PRINTK_EMERG "ubsan: %s:%u: vla size <= 0\n", info->source.file_name, info->source.line);
	ubsan_end(true);
}

__visible void __ubsan_handle_invalid_builtin(struct invalid_builtin_info* info) {
	(void)info;
	if (info->kind == 2) {
		printk(PRINTK_EMERG "ubsan: %s:%u: assumption violated during execution\n", info->source.file_name, info->source.line);
	} else {
		printk(PRINTK_CRIT "ubsan: %s:%u: Passing zero to __builtin_%s() which is not valid\n",
				info->source.file_name, info->source.line, info->kind == 1 ? "clz" : "ctz");
	}
	ubsan_end(info->kind == 2);
}

__visible void __ubsan_handle_function_type_mismatch(struct function_type_mismatch_info* info, const void* ptr) {
	printk(PRINTK_EMERG "ubsan: %s:%u: call to function %p through pointer to incorrect function type %s\n",
			info->source.file_name, info->source.line, ptr, info->type->name);
	ubsan_end(true);
}

__diag_pop();

#endif /* CONFIG_UBSAN */
