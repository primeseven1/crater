#include <crater/printk.h>
#include <crater/timekeeper.h>
#include <crater/trace.h>

#include <x86_64/pmio.h>

#include "i8042.h"

static inline bool __i8042_outbuffer_empty(void) {
	return (arch_x86_64_inb(I8042_IO_STATUS) & (1 << 0)) == 0;
}

static inline bool __i8042_inbuffer_full(void) {
	return ((arch_x86_64_inb(I8042_IO_STATUS) & (1 << 1)));
}

void i8042_flush_outbuffer(void) {
	while (!__i8042_outbuffer_empty())
		arch_x86_64_inb(I8042_IO_DATA);
}

void i8042_command_write(u8 command) {
	while (__i8042_inbuffer_full())
		arch_cpu_relax();
	arch_x86_64_outb(I8042_IO_COMMAND, command);
}

void i8042_data_write(u8 command) {
	while (__i8042_inbuffer_full())
		arch_cpu_relax();
	arch_x86_64_outb(I8042_IO_DATA, command);
}

u8 i8042_data_read(void) {
	while (__i8042_outbuffer_empty())
		arch_cpu_relax();
	return arch_x86_64_inb(I8042_IO_DATA);
}

void i8042_port_command(int port, u8 command) {
	if (port == 2) {
		i8042_command_write(I8042_COMMAND_WRITE_P2);
	} else if (port != 1){
		dump_stack();
		printk(PRINTK_CRIT "i8042: BUG: Cannot write to port %i\n", port);
		return;
	}
	i8042_data_write(command);
}

int i8042_data_read_timeout(time_t ms, u8* out_value) {
	time_t initial = timespec_ms(time_fromboot());

	while (__i8042_outbuffer_empty()) {
		time_t now = timespec_ms(time_fromboot());
		if ((now - initial) >= ms)
			return -ETIMEDOUT;
	}

	*out_value = i8042_data_read();
	return 0;
}

int i8042_disable_scanning(int port) {
	i8042_port_command(port, I8042_PORT_COMMAND_DISABLE_SCANNING);
	int resend_attempts = 5;

	while (1) {
		u8 data;
		int err = i8042_data_read_timeout(100, &data);
		if (err)
			return -ETIMEDOUT;

		if (data == I8042_ACK) {
			break;
		} else if (data == I8042_RESEND) {
			if (resend_attempts-- == 0) {
				printk(PRINTK_ERR "i8042: %s(): too many resends (port %d)\n", __func__, port);
				return -ETIMEDOUT;
			}
			i8042_port_command(port, I8042_PORT_COMMAND_DISABLE_SCANNING);
		}
	}

	i8042_flush_outbuffer();
	return 0;
}

int i8042_enable_scanning(int port) {
	i8042_port_command(port, I8042_PORT_COMMAND_ENABLE_SCANNING);
	int resend_attempts = 5;

	while (1) {
		u8 data;
		int err = i8042_data_read_timeout(100, &data);
		if (err)
			return -ETIMEDOUT;
		if (data == I8042_ACK)
			break;
		if (data == I8042_RESEND) {
			if (resend_attempts-- == 0) {
				printk(PRINTK_ERR "i8042: %s(): too many resends (port %d)\n", __func__, port);
				return -ETIMEDOUT;
			}
			i8042_port_command(port, I8042_PORT_COMMAND_ENABLE_SCANNING);
		}
	}

	i8042_flush_outbuffer();
	return 0;
}

static int __i8042_port_reset_selftest(int port) {
	int err = i8042_disable_scanning(port);
	if (err)
		return -ETIMEDOUT;

	i8042_flush_outbuffer();
	i8042_port_command(port, I8042_PORT_COMMAND_RESET_SELF_TEST);
	int resend_attempts = 5;
	u8 data;

	/* Check the command status and resend if needed */
	while (1) {
		err = i8042_data_read_timeout(500, &data);
		if (err)
			return -ETIMEDOUT;

		if (data == I8042_ACK) {
			break;
		} else if (data == I8042_RESEND) {
			if (resend_attempts-- == 0) {
				printk(PRINTK_ERR "i8042: %s(): too many resends (port %d)\n", __func__, port);
				return -ETIMEDOUT;
			}

			i8042_port_command(port, I8042_PORT_COMMAND_RESET_SELF_TEST);
		} else {
			printk(PRINTK_ERR "i8042: %s(): unknown value checking status (got %d, expected %d, port %d)",
					__func__, I8042_ACK, data, port);
			return -ENODEV;
		}
	}

	/* Make sure the reset self test succeeds */
	while (1) {
		err = i8042_data_read_timeout(1000, &data);
		if (err)
			return -ETIMEDOUT;
		if (data == I8042_PORT_SELFTEST_OK) {
			break;
		} else {
			if (data == I8042_PORT_SELFTEST_FAIL)
				printk(PRINTK_ERR "i8042: %s(): selftest failed (port %d)", __func__, port);
			else
				printk(PRINTK_ERR "i8042: %s(): unknown self test response (got %d, expected %d, port %d)",
						__func__, I8042_PORT_SELFTEST_OK, data, port);
			return -ENODEV;
		}
	}

	/*
	 * flush any and all data, disable scanning again so that way something 
	 * like key spam doesn't just halt the boot process
	 */
	i8042_disable_scanning(port);
	while (1) {
		err = i8042_data_read_timeout(100, &data);
		if (err)
			break;
	}
	i8042_flush_outbuffer();

	return 0;
}

int i8042_port_reset_selftest(int port) {
	int ret = __i8042_port_reset_selftest(port);
	if (unlikely(i8042_disable_scanning(port) != 0))
		printk(PRINTK_ERR "i8042: %s(): disable scanning timed out\n", __func__);
	return ret;
}
