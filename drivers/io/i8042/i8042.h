#pragma once

#include <crater/irq.h>

struct i8042_keyboard {
	unsigned int irq;
	struct keyboard* kbd;
	bool in_extended;
};

#define I8042_IO_DATA 0x60
#define I8042_IO_COMMAND 0x64
#define I8042_IO_STATUS I8042_IO_COMMAND

#define I8042_COMMAND_GET_CFG 0x20
#define I8042_COMMAND_SET_CFG 0x60
#define I8042_COMMAND_DISABLE_P2 0xa7
#define I8042_COMMAND_ENABLE_P2 0xa8
#define I8042_COMMAND_SELFTEST_P2 0xa9
#define I8042_COMMAND_SELFTEST 0xaa
#define I8042_COMMAND_SELFTEST_P1 0xab
#define I8042_COMMAND_DISABLE_P1 0xad
#define I8042_COMMAND_ENABLE_P1 0xae
#define I8042_COMMAND_WRITE_P2 0xd4

#define I8042_PORT_COMMAND_ENABLE_SCANNING 0xf4
#define I8042_PORT_COMMAND_DISABLE_SCANNING 0xf5
#define I8042_PORT_COMMAND_RESET_SELF_TEST 0xff

#define I8042_PORT_SELFTEST_OK 0xaa
#define I8042_PORT_SELFTEST_FAIL 0xfc

#define I8042_CFG_P1_IRQ_ENABLED (1 << 0)
#define I8042_CFG_P2_IRQ_ENABLED (1 << 1)
#define I8042_CFG_P1_CLOCK (1 << 4)
#define I8042_CFG_P2_CLOCK (1 << 5)
#define I8042_CFG_TRANSLATION (1 << 6)

#define I8042_RESEND 0xfe
#define I8042_SELFTEST_OK 0x55
#define I8042_ACK 0xfa

int i8042_setup_keyboard_irq(struct i8042_keyboard* kbd);
void i8042_command_write(u8 command);
void i8042_data_write(u8 command);
u8 i8042_data_read(void);
int i8042_data_read_timeout(time_t ms, u8* out_value);
void i8042_flush_outbuffer(void);
void i8042_port_command(int port, u8 command);
int i8042_port_reset_selftest(int port);
int i8042_disable_scanning(int port);
int i8042_enable_scanning(int port);
