#pragma once

#include <crater/errno.h>
#include <uacpi/status.h>
#include <uacpi/uacpi.h>

static inline uacpi_status acpi_errno_to_uacpi_status(int errno) {
	switch (errno) {
	case 0:
		return UACPI_STATUS_OK;
	case -EINVAL:
		return UACPI_STATUS_INVALID_ARGUMENT;
	case -ENOMEM:
		return UACPI_STATUS_OUT_OF_MEMORY;
	case -ENOENT:
		return UACPI_STATUS_NOT_FOUND;
	case -EEXIST:
		return UACPI_STATUS_ALREADY_EXISTS;
	case -ENOSYS:
	case -EOPNOTSUPP:
		return UACPI_STATUS_UNIMPLEMENTED;
	case -ETIMEDOUT:
		return UACPI_STATUS_HARDWARE_TIMEOUT;
	case -ETIME:
		return UACPI_STATUS_TIMEOUT;
	case -EPERM:
		return UACPI_STATUS_DENIED;
	}
	return UACPI_STATUS_INTERNAL_ERROR;
}

static inline int acpi_uacpi_status_to_errno(uacpi_status status) {
	switch (status) {
	case UACPI_STATUS_OK:
		return 0;
	case UACPI_STATUS_NOT_FOUND:
		return -ENOENT;
	case UACPI_STATUS_MAPPING_FAILED:
	case UACPI_STATUS_OUT_OF_MEMORY:
		return -ENOMEM;
	case UACPI_STATUS_TYPE_MISMATCH:
		return -EPROTO;
	case UACPI_STATUS_INVALID_ARGUMENT:
		return -EINVAL;
	case UACPI_STATUS_TIMEOUT:
		return -ETIME;
	case UACPI_STATUS_HARDWARE_TIMEOUT:
		return -ETIMEDOUT;
	case UACPI_STATUS_ALREADY_EXISTS:
		return -EEXIST;
	case UACPI_STATUS_UNIMPLEMENTED:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static inline uacpi_status acpi_eval_gpe(uacpi_namespace_node* node, u16* out) {
	uacpi_u64 value;
	uacpi_status status = uacpi_eval_simple_integer(node, "_GPE", &value);
	if (status == UACPI_STATUS_OK)
		*out = value;
	return status;
}
