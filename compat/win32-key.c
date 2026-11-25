#ifdef _WIN32

#include "tmux.h"
#include "win32-key.h"

struct win32_special_key {
	WORD	vk;
	WCHAR	ch;
	DWORD	state;
};

static const struct win32_special_key win32_special_keys[] = {
	[KEYC_BSPACE - KEYC_BSPACE] = { VK_BACK, '\b', 0 },
	[KEYC_IC - KEYC_BSPACE] = { VK_INSERT, 0, ENHANCED_KEY },
	[KEYC_DC - KEYC_BSPACE] = { VK_DELETE, 0, ENHANCED_KEY },
	[KEYC_HOME - KEYC_BSPACE] = { VK_HOME, 0, ENHANCED_KEY },
	[KEYC_END - KEYC_BSPACE] = { VK_END, 0, ENHANCED_KEY },
	[KEYC_NPAGE - KEYC_BSPACE] = { VK_NEXT, 0, ENHANCED_KEY },
	[KEYC_PPAGE - KEYC_BSPACE] = { VK_PRIOR, 0, ENHANCED_KEY },
	[KEYC_BTAB - KEYC_BSPACE] = { VK_TAB, '\t', SHIFT_PRESSED },
	[KEYC_UP - KEYC_BSPACE] = { VK_UP, 0, ENHANCED_KEY },
	[KEYC_DOWN - KEYC_BSPACE] = { VK_DOWN, 0, ENHANCED_KEY },
	[KEYC_LEFT - KEYC_BSPACE] = { VK_LEFT, 0, ENHANCED_KEY },
	[KEYC_RIGHT - KEYC_BSPACE] = { VK_RIGHT, 0, ENHANCED_KEY },
	[KEYC_KP_SLASH - KEYC_BSPACE] = { VK_DIVIDE, '/', ENHANCED_KEY },
	[KEYC_KP_STAR - KEYC_BSPACE] = { VK_MULTIPLY, '*', 0 },
	[KEYC_KP_MINUS - KEYC_BSPACE] = { VK_SUBTRACT, '-', 0 },
	[KEYC_KP_SEVEN - KEYC_BSPACE] = { VK_NUMPAD7, '7', 0 },
	[KEYC_KP_EIGHT - KEYC_BSPACE] = { VK_NUMPAD8, '8', 0 },
	[KEYC_KP_NINE - KEYC_BSPACE] = { VK_NUMPAD9, '9', 0 },
	[KEYC_KP_PLUS - KEYC_BSPACE] = { VK_ADD, '+', 0 },
	[KEYC_KP_FOUR - KEYC_BSPACE] = { VK_NUMPAD4, '4', 0 },
	[KEYC_KP_FIVE - KEYC_BSPACE] = { VK_NUMPAD5, '5', 0 },
	[KEYC_KP_SIX - KEYC_BSPACE] = { VK_NUMPAD6, '6', 0 },
	[KEYC_KP_ONE - KEYC_BSPACE] = { VK_NUMPAD1, '1', 0 },
	[KEYC_KP_TWO - KEYC_BSPACE] = { VK_NUMPAD2, '2', 0 },
	[KEYC_KP_THREE - KEYC_BSPACE] = { VK_NUMPAD3, '3', 0 },
	[KEYC_KP_ENTER - KEYC_BSPACE] = { VK_RETURN, '\r', ENHANCED_KEY },
	[KEYC_KP_ZERO - KEYC_BSPACE] = { VK_NUMPAD0, '0', 0 },
	[KEYC_KP_PERIOD - KEYC_BSPACE] = { VK_DECIMAL, '.', 0 },
};

static WCHAR
win32_key_control_char(WORD vk, WCHAR ch)
{
	static const WCHAR digits[] = { 0, 27, 28, 29, 30, 31, 127 };

	if (vk == VK_RETURN)
		return ('\n');
	if (vk == VK_BACK || ch == '?')
		return (0x7f);
	if (ch >= '@' && ch <= '~')
		return (ch & 0x1f);
	if (ch == ' ')
		return (0);
	if (ch == '/')
		return (0x1f);
	if (ch >= '2' && ch <= '8' && vk == ch)
		return (digits[ch - '2']);
	return (ch);
}

int
win32_key_encode(key_code key, char *buf, size_t size)
{
	key_code		 base = key & KEYC_MASK_KEY;
	struct win32_special_key special;
	struct utf8_data	 ud;
	WCHAR			 chars[2] = { 0 };
	DWORD			 state = 0;
	WORD			 vk, scan;
	SHORT			 mapped;
	int			 count = 1, i, down, length, total = 0;

	/* Keep ordinary text and terminal protocol bytes on the VT path. */
	if (key & KEYC_LITERAL)
		return (0);
	if (!(key & KEYC_MASK_MODIFIERS) && base != KEYC_BTAB)
		return (0);
	if (key & KEYC_CTRL)
		state |= LEFT_CTRL_PRESSED;
	if (key & KEYC_META)
		state |= LEFT_ALT_PRESSED;
	if (key & KEYC_SHIFT)
		state |= SHIFT_PRESSED;

	if (base >= KEYC_F1 && base <= KEYC_F12)
		vk = VK_F1 + (base - KEYC_F1);
	else if (base >= KEYC_BSPACE && base <= KEYC_KP_PERIOD) {
		special = win32_special_keys[base - KEYC_BSPACE];
		vk = special.vk;
		chars[0] = special.ch;
		state |= special.state;
	} else if (base == '\r' || base == '\t' || base == '\033') {
		chars[0] = base;
		vk = base; /* VK_RETURN, VK_TAB and VK_ESCAPE. */
	} else if (base >= 'a' && base <= 'z') {
		vk = base - 'a' + 'A';
		chars[0] = (state & SHIFT_PRESSED) ? vk : base;
	} else if (base >= ' ' && base <= '~') {
		chars[0] = base;
		mapped = VkKeyScanW(chars[0]);
		vk = (mapped == -1) ? VK_PACKET : LOBYTE(mapped);
	} else if (KEYC_IS_UNICODE(base)) {
		utf8_to_data(base, &ud);
		count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		    ud.data, ud.size, chars, nitems(chars));
		if (count == 0)
			return (0);
		mapped = (count == 1) ? VkKeyScanW(chars[0]) : -1;
		vk = (mapped == -1) ? VK_PACKET : LOBYTE(mapped);
	} else
		return (0);

	if (state & LEFT_CTRL_PRESSED)
		chars[0] = win32_key_control_char(vk, chars[0]);
	scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
	for (i = 0; i < count; i++) {
		for (down = 1; down >= 0; down--) {
			/* CSI Vk;Sc;Uc;Kd;Cs;Rc_ (Microsoft Win32 input mode). */
			length = snprintf(buf + total, size - total,
			    "\033[%u;%u;%u;%u;%lu;1_", (unsigned)vk,
			    (unsigned)scan, (unsigned)chars[i], (unsigned)down,
			    (unsigned long)state);
			if (length < 0 || (size_t)length >= size - total)
				return (0);
			total += length;
		}
	}
	return (total);
}

#endif
