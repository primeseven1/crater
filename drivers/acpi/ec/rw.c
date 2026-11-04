#include <crater/printk.h>
#include <crater/timekeeper.h>
#include <crater/sched.h>
#include <crater/panic.h>
#include <crater/trace.h>
#include "ec.h"

static inline u8 raw_read(struct acpi_gas* gas) {
	u64 value;
	bug(uacpi_gas_read(gas, &value) != UACPI_STATUS_OK);
	return (u8)value;
}

static inline void raw_write(struct acpi_gas* gas, u8 value) {
	bug(uacpi_gas_write(gas, value) != UACPI_STATUS_OK);
}

static int wait(struct acpi_gas* gas, u8 bit, bool value) {
	const int timeout_us = 1000 * 1000;
	const int delay_time = 5;

	/* Most EC transactions finish in a few microseconds, so spin for ~1ms before sleeping */
	for (int i = 0; i < 1000 / delay_time; i++) {
		if (!!(raw_read(gas) & bit) == value)
			return 0;
		udelay(delay_time);
	}

	/* Fall back to sleeping */
	const time_t start = timespec_us(time_fromboot());
	while (1) {
		u8 reg = raw_read(gas);
		if (!!(reg & bit) == value)
			break;
		if (timespec_us(time_fromboot()) - start >= timeout_us) {
			printk(PRINTK_ERR "ec: Timed out after %d us\n", timeout_us);
			return -ETIMEDOUT;
		}
		msleep(1);	
	}
	return 0;
}

static inline int wait_ibf(struct ec_device* device) {
	return wait(&device->ctrl, EC_FLAG_IBF, false);
}

static inline int wait_obf(struct ec_device* device) {
	return wait(&device->ctrl, EC_FLAG_OBF, true);
}

int ec_read(struct ec_device* device, u8 addr, u8* out) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->ctrl, EC_COMMAND_READ);
	timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->data, addr);

	timeout = wait_obf(device);
	if (timeout == 0)
		*out = raw_read(&device->data);
	return timeout;
}

int ec_write(struct ec_device* device, u8 addr, u8 value) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->ctrl, EC_COMMAND_WRITE);
	timeout = wait_ibf(device);
	if (timeout)
		return timeout;
	raw_write(&device->data, addr);

	timeout = wait_ibf(device);
	if (timeout == 0)
		raw_write(&device->data, value);
	return timeout;
}

int ec_burst_enable(struct ec_device* device) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;

	raw_write(&device->ctrl, EC_COMMAND_BURST_ENABLE);
	timeout = wait_obf(device);
	if (timeout == 0) /* discard ACK */
		raw_read(&device->data);
	return timeout;
}

int ec_burst_disable(struct ec_device* device) {
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;

	raw_write(&device->ctrl, EC_COMMAND_BURST_DISABLE);
	return wait_ibf(device);
}

int ec_get_event(struct ec_device* device, u8* index) {
	*index = 0;
	if (!(raw_read(&device->ctrl) & EC_FLAG_SCI_EVT))
		return 0;
	int timeout = wait_ibf(device);
	if (timeout)
		return timeout;

	raw_write(&device->ctrl, EC_COMMAND_QUERY);
	timeout = wait_obf(device);
	*index = timeout == 0 ? raw_read(&device->data) : 0;
	return timeout;
}

bool ec_ok(struct ec_device* device) {
	int timeout = wait_ibf(device);
	if (timeout)
		return false;

	raw_write(&device->ctrl, EC_COMMAND_READ);
	timeout = wait_ibf(device);
	if (timeout)
		return false;

	raw_write(&device->data, 0); /* read from 0 */
	timeout = wait_obf(device);
	if (timeout)
		return false;
	raw_read(&device->data); /* discard data */
	return true;
}
