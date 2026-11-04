#pragma once

#include <crater/ringbuffer.h>
#include <crater/spinlock.h>
#include <crater/list.h>
#include <crater/semaphore.h>

#define KB_PACKET_FLAG_RELEASED (1 << 0)
#define KB_PACKET_FLAG_LEFTCTRL (1 << 1)
#define KB_PACKET_FLAG_CAPSLOCK (1 << 2)
#define KB_PACKET_FLAG_LEFTALT (1 << 3)
#define KB_PACKET_FLAG_RIGHTALT (1 << 4)
#define KB_PACKET_FLAG_LEFTSHIFT (1 << 5)
#define KB_PACKET_FLAG_RIGHTSHIFT (1 << 6)
#define KB_PACKET_FLAG_RIGHTCTRL (1 << 7)

#define KEYCODE_RESERVED 0
#define KEYCODE_ESCAPE 1
#define KEYCODE_1 2
#define KEYCODE_2 3
#define KEYCODE_3 4
#define KEYCODE_4 5
#define KEYCODE_5 6
#define KEYCODE_6 7
#define KEYCODE_7 8
#define KEYCODE_8 9
#define KEYCODE_9 10
#define KEYCODE_0 11
#define KEYCODE_MINUS 12
#define KEYCODE_EQUAL 13
#define KEYCODE_BACKSPACE 14
#define KEYCODE_TAB 15
#define KEYCODE_Q 16
#define KEYCODE_W 17
#define KEYCODE_E 18
#define KEYCODE_R 19
#define KEYCODE_T 20
#define KEYCODE_Y 21
#define KEYCODE_U 22
#define KEYCODE_I 23
#define KEYCODE_O 24
#define KEYCODE_P 25
#define KEYCODE_LEFTBRACE 26
#define KEYCODE_RIGHTBRACE 27
#define KEYCODE_ENTER 28
#define KEYCODE_LEFTCTRL 29
#define KEYCODE_A 30
#define KEYCODE_S 31
#define KEYCODE_D 32
#define KEYCODE_F 33
#define KEYCODE_G 34
#define KEYCODE_H 35
#define KEYCODE_J 36
#define KEYCODE_K 37
#define KEYCODE_L 38
#define KEYCODE_SEMICOLON 39
#define KEYCODE_APOSTROPHE 40
#define KEYCODE_GRAVE 41
#define KEYCODE_LEFTSHIFT 42
#define KEYCODE_BACKSLASH 43
#define KEYCODE_Z 44
#define KEYCODE_X 45
#define KEYCODE_C 46
#define KEYCODE_V 47
#define KEYCODE_B 48
#define KEYCODE_N 49
#define KEYCODE_M 50
#define KEYCODE_COMMA 51
#define KEYCODE_DOT 52
#define KEYCODE_SLASH 53
#define KEYCODE_RIGHTSHIFT 54
#define KEYCODE_KEYPADASTERISK 55
#define KEYCODE_LEFTALT 56
#define KEYCODE_SPACE 57
#define KEYCODE_CAPSLOCK 58
#define KEYCODE_F1 59
#define KEYCODE_F2 60
#define KEYCODE_F3 61
#define KEYCODE_F4 62
#define KEYCODE_F5 63
#define KEYCODE_F6 64
#define KEYCODE_F7 65
#define KEYCODE_F8 66
#define KEYCODE_F9 67
#define KEYCODE_F10 68
#define KEYCODE_NUMLOCK 69
#define KEYCODE_SCROLLLOCK 70
#define KEYCODE_KEYPAD7 71
#define KEYCODE_KEYPAD8 72
#define KEYCODE_KEYPAD9 73
#define KEYCODE_KEYPADMINUS 74
#define KEYCODE_KEYPAD4 75
#define KEYCODE_KEYPAD5 76
#define KEYCODE_KEYPAD6 77
#define KEYCODE_KEYPADPLUS 78
#define KEYCODE_KEYPAD1 79
#define KEYCODE_KEYPAD2 80
#define KEYCODE_KEYPAD3 81
#define KEYCODE_KEYPAD0 82
#define KEYCODE_KEYPADDOT 83
#define KEYCODE_F11 84
#define KEYCODE_F12 85
#define KEYCODE_KEYPADENTER 86
#define KEYCODE_RIGHTCTRL 87
#define KEYCODE_KEYPADSLASH 88
#define KEYCODE_SYSREQ 89
#define KEYCODE_RIGHTALT 90
#define KEYCODE_LINEFEED 91
#define KEYCODE_HOME 92
#define KEYCODE_UP 93
#define KEYCODE_PAGEUP 94
#define KEYCODE_LEFT 95
#define KEYCODE_RIGHT 96
#define KEYCODE_END 97
#define KEYCODE_DOWN 98
#define KEYCODE_PAGEDOWN 99
#define KEYCODE_INSERT 100
#define KEYCODE_DELETE 101

struct kb_packet {
	char ascii;
	char _pad[7];
	unsigned int keycode;
	int flags;
} __attribute__((packed));
static_assert((sizeof(struct kb_packet) & (sizeof(struct kb_packet) - 1)) == 0, "");

struct keyboard {
	struct ringbuffer rb;
	spinlock_t rb_lock;
	struct semaphore sem;
	int flags;
	struct list_node link;
};

/**
 * @brief Create a keyboard device structure
 * @return A pointer to the new structure
 */
struct keyboard* keyboard_create(void);

/**
 * @brief Destroy a keyboard device structure
 * @param keyboard The keyboard to destroy
 */
void keyboard_destroy(struct keyboard* keyboard);

/**
 * @brief Send a keyboard event packet to a device
 * @param keyboard The keyboard to write to
 * @param packet The packet to send
 */
void keyboard_send_packet(struct keyboard* keyboard, struct kb_packet* packet);

/**
 * @brief Wait for a keyboard packet to arrive
 *
 * @param keyboard[in] The keyboard to read from
 * @param out_packet[out] Where the packet data will be stored
 *
 * @retval 0 Successful
 * @retval -EIO Failed to read packet
 */
int keyboard_wait(struct keyboard* keyboard, struct kb_packet* out_packet);

/**
 * @brief Will get removed
 */
void keyboard_reader_thread_init(void);
