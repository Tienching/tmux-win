#include <assert.h>
#include <stdio.h>

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

int
main(void)
{
	char buf[WIN32_KEY_SEQUENCE_MAX];

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
