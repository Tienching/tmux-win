#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tmux.h"
#include "compat/win32-key.h"

static void
expect_key(key_code key, unsigned int vk, unsigned int ch, unsigned int state)
{
	char buf[WIN32_KEY_SEQUENCE_MAX];
	unsigned int actual_vk, scan, actual_ch, down, actual_state, repeat;
	int length, offset;
	const char *next = buf;

	length = win32_key_encode(key, buf, sizeof buf);
	assert(length > 0);
	for (int transition = 1; transition >= 0; --transition) {
		assert(sscanf(next, "\033[%u;%u;%u;%u;%u;%u_%n", &actual_vk,
		    &scan, &actual_ch, &down, &actual_state, &repeat, &offset) == 6);
		assert(actual_vk == vk && actual_ch == ch && actual_state == state);
		assert(down == (unsigned int)transition && repeat == 1);
		next += offset;
	}
	assert(next == buf + length && *next == '\0');
}

static void
expect_key_code(WORD vk, WCHAR ch, DWORD state, key_code expected)
{
	KEY_EVENT_RECORD record = { .bKeyDown = TRUE, .wRepeatCount = 1,
		.wVirtualKeyCode = vk, .uChar.UnicodeChar = ch,
		.dwControlKeyState = state };
	assert(win32_key_code(&record) == expected);
}

int
main(void)
{
	char buf[WIN32_KEY_SEQUENCE_MAX];
	const char *native_enter = "\033[13;28;13;1;260;3_";
	KEY_EVENT_RECORD record;
	struct utf8_data ud;
	utf8_char uc;
	size_t used;

	for (size_t i = 2; i < strlen(native_enter); ++i)
		assert(win32_key_decode(native_enter, i, &record, &used) == 1);
	assert(win32_key_decode(native_enter, strlen(native_enter), &record,
	    &used) == 0);
	assert(used == strlen(native_enter));
	assert(record.wVirtualKeyCode == VK_RETURN && record.wVirtualScanCode == 28);
	assert(record.uChar.UnicodeChar == '\r' && record.bKeyDown);
	assert(record.dwControlKeyState == (RIGHT_CTRL_PRESSED | ENHANCED_KEY));
	assert(record.wRepeatCount == 3);
	assert(win32_key_code(&record) == (KEYC_KP_ENTER | KEYC_CTRL | KEYC_KEYPAD));
	assert(win32_key_decode("\033[13;5u", 7, &record, &used) == -1);
	assert(win32_key_decode("\033[70000;1;0;1;0;1_", 18, &record,
	    &used) == -1);
	expect_key_code('2', 0, LEFT_CTRL_PRESSED, '2' | KEYC_CTRL);
	expect_key_code(VK_F13, 0, LEFT_CTRL_PRESSED, KEYC_ANY);
	expect_key_code(VK_SHIFT, 0, SHIFT_PRESSED, KEYC_UNKNOWN);
	expect_key_code(VK_RETURN, '\r', LEFT_CTRL_PRESSED, '\r' | KEYC_CTRL);
	expect_key_code(VK_TAB, '\t', SHIFT_PRESSED | RIGHT_CTRL_PRESSED,
	    KEYC_BTAB | KEYC_CTRL);
	expect_key_code('J', '\n', RIGHT_CTRL_PRESSED, 'j' | KEYC_CTRL);
	expect_key_code('A', 'A', SHIFT_PRESSED, 'A');
	expect_key_code(VK_LEFT, 0, LEFT_ALT_PRESSED | SHIFT_PRESSED,
	    KEYC_LEFT | KEYC_CURSOR | KEYC_META | KEYC_SHIFT);
	expect_key_code(VK_F12, 0, LEFT_CTRL_PRESSED, KEYC_F12 | KEYC_CTRL);
	expect_key_code(VK_CANCEL, 0, LEFT_CTRL_PRESSED, KEYC_BREAK | KEYC_CTRL);
	expect_key_code(0, '\003', 0, 'c' | KEYC_CTRL);
	expect_key_code(VK_PACKET, 0xd83d, 0, KEYC_ANY);
	assert(utf8_fromwc(0x4e2d, &ud) == UTF8_DONE);
	assert(utf8_from_data(&ud, &uc) == UTF8_DONE);
	expect_key_code(VK_PACKET, 0x4e2d, LEFT_ALT_PRESSED, uc | KEYC_META);
	SHORT mapped = VkKeyScanW(0x4e2d);
	expect_key(uc | KEYC_META, mapped == -1 ? VK_PACKET : LOBYTE(mapped),
	    0x4e2d, LEFT_ALT_PRESSED);

	for (key_code letter = 'a'; letter <= 'z'; ++letter)
		expect_key(letter | KEYC_CTRL, (unsigned int)letter - 'a' + 'A',
		    (unsigned int)letter & 0x1f, LEFT_CTRL_PRESSED);
	for (key_code key = KEYC_F1; key <= KEYC_F12; ++key)
		expect_key(key | KEYC_META | KEYC_SHIFT, VK_F1 + key - KEYC_F1,
		    0, LEFT_ALT_PRESSED | SHIFT_PRESSED);
	expect_key('\r' | KEYC_CTRL | KEYC_META | KEYC_SHIFT, VK_RETURN,
	    '\n', LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED | SHIFT_PRESSED);
	expect_key(KEYC_BTAB | KEYC_CTRL, VK_TAB, '\t',
	    LEFT_CTRL_PRESSED | SHIFT_PRESSED);
	expect_key(KEYC_KP_ENTER | KEYC_KEYPAD | KEYC_META, VK_RETURN, '\r',
	    ENHANCED_KEY | LEFT_ALT_PRESSED);
	expect_key(KEYC_LEFT | KEYC_CURSOR | KEYC_SHIFT, VK_LEFT, 0,
	    ENHANCED_KEY | SHIFT_PRESSED);
	expect_key(' ' | KEYC_CTRL, VK_SPACE, 0, LEFT_CTRL_PRESSED);
	expect_key('2' | KEYC_CTRL, '2', 0, LEFT_CTRL_PRESSED);
	expect_key('\033' | KEYC_CTRL, VK_ESCAPE, '\033', LEFT_CTRL_PRESSED);
	expect_key('a' | KEYC_SHIFT, 'A', 'A', SHIFT_PRESSED);
	assert(win32_key_encode('x', buf, sizeof buf) == 0);
	assert(win32_key_encode('\n' | KEYC_LITERAL, buf, sizeof buf) == 0);
	assert(win32_key_encode(KEYC_PASTE_START, buf, sizeof buf) == 0);
	assert(win32_key_encode(KEYC_FOCUS_IN, buf, sizeof buf) == 0);
	assert(win32_key_encode(KEYC_UNKNOWN | KEYC_CTRL, buf, sizeof buf) == 0);
	assert(win32_key_encode('j' | KEYC_CTRL, buf, 4) == 0);
	puts("Verified Win32 key sequences");
	return (0);
}
