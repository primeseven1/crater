#include <x86_64/pmio.h>
#include <uacpi/kernel_api.h>

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out) {
	(void)len;
	*out = (uacpi_handle)base;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
	(void)handle;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out) {
	*out = arch_x86_64_inb((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out) {
	*out = arch_x86_64_inw((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out) {
	*out = arch_x86_64_inl((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value) {
	arch_x86_64_outb((uacpi_io_addr)handle + offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value) {
	arch_x86_64_outw((uacpi_io_addr)handle + offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
	arch_x86_64_outl((uacpi_io_addr)handle + offset, value);
	return UACPI_STATUS_OK;
}
