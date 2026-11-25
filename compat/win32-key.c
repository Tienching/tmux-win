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
win32_key_encode_record(const KEY_EVENT_RECORD *record, char *buf, size_t size)
{
	int down, length, total = 0;

	for (down = 1; down >= 0; down--) {
		/* CSI Vk;Sc;Uc;Kd;Cs;Rc_ (Microsoft Win32 input mode). */
		length = snprintf(buf + total, size - total,
		    "\033[%u;%u;%u;%u;%lu;%u_", record->wVirtualKeyCode,
		    record->wVirtualScanCode, (unsigned)record->uChar.UnicodeChar,
		    (unsigned)down, (unsigned long)record->dwControlKeyState,
		    down ? record->wRepeatCount : 1);
		if (length < 0 || (size_t)length >= size - total)
			return (0);
		total += length;
	}
	return (total);
}

int
win32_key_encode(key_code key, char *buf, size_t size)
{
	key_code		 base = key & KEYC_MASK_KEY;
	struct win32_special_key special;
	struct utf8_data	 ud;
	WCHAR			 chars[2] = { 0 };
	DWORD			 state = 0;
	KEY_EVENT_RECORD	 record = { 0 };
	WORD			 vk;
	SHORT			 mapped;
	int			 count = 1, i, length, total = 0;

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
	record.wVirtualKeyCode = vk;
	record.wVirtualScanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
	record.dwControlKeyState = state;
	record.wRepeatCount = 1;
	for (i = 0; i < count; i++) {
		record.uChar.UnicodeChar = chars[i];
		length = win32_key_encode_record(&record, buf + total, size - total);
		if (length == 0)
			return (0);
		total += length;
	}
	return (total);
}

/* Return 0 for a record, 1 for a prefix and -1 for other input. */
int
win32_key_decode(const char *buf, size_t len, KEY_EVENT_RECORD *record,
    size_t *used)
{
	unsigned int fields[6] = { 0, 0, 0, 0, 0, 1 };
	unsigned int value = 0, field = 0;
	size_t i;
	int digits = 0;

	if (len == 0 || buf[0] != '\033')
		return (-1);
	if (len == 1)
		return (1);
	if (buf[1] != '[')
		return (-1);
	for (i = 2; i < len && i < 40; i++) {
		if (buf[i] >= '0' && buf[i] <= '9') {
			value = value * 10 + buf[i] - '0';
			if (value > 65535)
				return (-1);
			digits = 1;
			continue;
		}
		if (buf[i] != ';' && buf[i] != '_')
			return (-1);
		if (digits)
			fields[field] = value;
		if (buf[i] == '_')
			break;
		if (++field == nitems(fields))
			return (-1);
		value = digits = 0;
	}
	if (i == 40)
		return (-1);
	if (i == len)
		return (1);
	if (fields[3] > 1 || fields[5] == 0)
		return (-1);
	record->wVirtualKeyCode = fields[0];
	record->wVirtualScanCode = fields[1];
	record->uChar.UnicodeChar = fields[2];
	record->bKeyDown = fields[3];
	record->dwControlKeyState = fields[4];
	record->wRepeatCount = fields[5];
	*used = i + 1;
	return (0);
}

key_code
win32_key_code(const KEY_EVENT_RECORD *record)
{
	WORD		 vk = record->wVirtualKeyCode;
	WCHAR		 ch = record->uChar.UnicodeChar;
	DWORD		 state = record->dwControlKeyState;
	key_code	 key = KEYC_UNKNOWN, modifiers = 0;
	struct utf8_data ud;
	utf8_char	 uc;
	size_t		 i;

	if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
		modifiers |= KEYC_CTRL;
	if (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
		modifiers |= KEYC_META;
	if (state & SHIFT_PRESSED)
		modifiers |= KEYC_SHIFT;

	if (vk >= VK_F1 && vk <= VK_F12)
		return ((KEYC_F1 + vk - VK_F1) | modifiers);
	if (vk == VK_CANCEL)
		return (KEYC_BREAK | modifiers);
	if (vk == VK_TAB && (modifiers & KEYC_SHIFT))
		return (KEYC_BTAB | (modifiers & ~KEYC_SHIFT));
	for (i = 0; i < nitems(win32_special_keys); i++) {
		if (vk == 0 || vk != win32_special_keys[i].vk)
			continue;
		key = KEYC_BSPACE + i;
		if (key == KEYC_BTAB ||
		    (key == KEYC_KP_ENTER && !(state & ENHANCED_KEY)))
			break;
		if (key >= KEYC_KP_SLASH)
			key |= KEYC_KEYPAD;
		else if (key >= KEYC_UP)
			key |= KEYC_CURSOR;
		return (key | modifiers);
	}
	if (vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE || vk == VK_SPACE)
		return (vk | modifiers);
	if ((modifiers & KEYC_CTRL) && vk >= 'A' && vk <= 'Z')
		return ((vk - 'A' + 'a') | modifiers);
	if (ch == 0 && vk >= '0' && vk <= '9')
		return (vk | modifiers);
	if (ch >= ' ' && ch <= '~') {
		if ((modifiers & (KEYC_CTRL | KEYC_META)) == 0)
			modifiers &= ~KEYC_SHIFT;
		return (ch | modifiers);
	}
	if (ch > 0x7f && utf8_fromwc(ch, &ud) == UTF8_DONE &&
	    utf8_from_data(&ud, &uc) == UTF8_DONE)
		return (uc | modifiers);
	if (ch != 0 && ch < 0x20) {
		key = ch | 0x40;
		if (key >= 'A' && key <= 'Z')
			key |= 0x20;
		return (key | modifiers | KEYC_CTRL);
	}
	if ((vk == 0 && ch == 0) || (vk >= VK_SHIFT && vk <= VK_MENU) ||
	    (vk >= VK_LSHIFT && vk <= VK_RMENU))
		return (KEYC_UNKNOWN);
	/* Keep native keys that have no equivalent in the tmux key table. */
	return (KEYC_ANY);
}

#endif
