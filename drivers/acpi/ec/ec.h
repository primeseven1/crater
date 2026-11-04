#pragma once

#include <crater/mutex.h>
#include <crater/panic.h>
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/io.h>

#define EC_FLAG_OBF (1 << 0)
#define EC_FLAG_IBF (1 << 1)
#define EC_FLAG_BURST (1 << 4)
#define EC_FLAG_SCI_EVT (1 << 5)
#define EC_FLAG_SMI_EVT (1 << 7)

#define EC_COMMAND_READ 0x80
#define EC_COMMAND_WRITE 0x81
#define EC_COMMAND_BURST_ENABLE 0x82
#define EC_COMMAND_BURST_DISABLE 0x83
#define EC_COMMAND_QUERY 0x84

struct ec_device {
	uacpi_namespace_node* node;
	u16 gpe_index;
	bool __atomic handling_events;
	struct acpi_gas ctrl, data;
	bool global_lock;
	u32 global_lock_seq;
	mutex_t mtx;
};

static inline void ec_lock(struct ec_device* device) {
	mutex_acquire(&device->mtx);
	if (device->global_lock)
		bug(uacpi_acquire_global_lock(0xFFFF, &device->global_lock_seq) != 0);
}

static inline void ec_unlock(struct ec_device* device) {
	if (device->global_lock)
		bug(uacpi_release_global_lock(device->global_lock_seq) != UACPI_STATUS_OK);
	mutex_release(&device->mtx);
}

int ec_read(struct ec_device* device, u8 addr, u8* out);
int ec_write(struct ec_device* device, u8 addr, u8 value);
int ec_burst_enable(struct ec_device* device);
int ec_burst_disable(struct ec_device* device);
int ec_get_event(struct ec_device* device, u8* index);
bool ec_ok(struct ec_device* device);

void ec_install_handlers(struct ec_device* device);
void ec_install_events(struct ec_device* device);
