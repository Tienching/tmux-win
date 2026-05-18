/* $OpenBSD$ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#if defined(HAVE_CURSES_H)
#include <curses.h>
#elif defined(HAVE_NCURSES_H)
#include <ncurses.h>
#endif
#ifndef _WIN32
#include <fnmatch.h>
#endif
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <term.h>
#endif

#include "tmux.h"

static char	*tty_term_strip(const char *);

struct tty_terms tty_terms = LIST_HEAD_INITIALIZER(tty_terms);

enum tty_code_type {
	TTYCODE_NONE = 0,
	TTYCODE_STRING,
	TTYCODE_NUMBER,
	TTYCODE_FLAG,
};

struct tty_code {
	enum tty_code_type	type;
	union {
		char	       *string;
		int		number;
		int		flag;
	} value;
};

struct tty_term_code_entry {
	enum tty_code_type	type;
	const char	       *name;
};

static const struct tty_term_code_entry tty_term_codes[] = {
	[TTYC_ACSC] = { TTYCODE_STRING, "acsc" },
	[TTYC_AM] = { TTYCODE_FLAG, "am" },
	[TTYC_AX] = { TTYCODE_FLAG, "AX" },
	[TTYC_BCE] = { TTYCODE_FLAG, "bce" },
	[TTYC_BEL] = { TTYCODE_STRING, "bel" },
	[TTYC_BIDI] = { TTYCODE_STRING, "Bidi" },
	[TTYC_BLINK] = { TTYCODE_STRING, "blink" },
	[TTYC_BOLD] = { TTYCODE_STRING, "bold" },
	[TTYC_CIVIS] = { TTYCODE_STRING, "civis" },
	[TTYC_CLEAR] = { TTYCODE_STRING, "clear" },
	[TTYC_CLMG] = { TTYCODE_STRING, "Clmg" },
	[TTYC_CMG] = { TTYCODE_STRING, "Cmg" },
	[TTYC_CNORM] = { TTYCODE_STRING, "cnorm" },
	[TTYC_COLORS] = { TTYCODE_NUMBER, "colors" },
	[TTYC_CR] = { TTYCODE_STRING, "Cr" },
	[TTYC_CSR] = { TTYCODE_STRING, "csr" },
	[TTYC_CS] = { TTYCODE_STRING, "Cs" },
	[TTYC_CUB1] = { TTYCODE_STRING, "cub1" },
	[TTYC_CUB] = { TTYCODE_STRING, "cub" },
	[TTYC_CUD1] = { TTYCODE_STRING, "cud1" },
	[TTYC_CUD] = { TTYCODE_STRING, "cud" },
	[TTYC_CUF1] = { TTYCODE_STRING, "cuf1" },
	[TTYC_CUF] = { TTYCODE_STRING, "cuf" },
	[TTYC_CUP] = { TTYCODE_STRING, "cup" },
	[TTYC_CUU1] = { TTYCODE_STRING, "cuu1" },
	[TTYC_CUU] = { TTYCODE_STRING, "cuu" },
	[TTYC_CVVIS] = { TTYCODE_STRING, "cvvis" },
	[TTYC_DCH1] = { TTYCODE_STRING, "dch1" },
	[TTYC_DCH] = { TTYCODE_STRING, "dch" },
	[TTYC_DIM] = { TTYCODE_STRING, "dim" },
	[TTYC_DL1] = { TTYCODE_STRING, "dl1" },
	[TTYC_DL] = { TTYCODE_STRING, "dl" },
	[TTYC_DSEKS] = { TTYCODE_STRING, "Dseks" },
	[TTYC_DSFCS] = { TTYCODE_STRING, "Dsfcs" },
	[TTYC_DSBP] = { TTYCODE_STRING, "Dsbp" },
	[TTYC_DSMG] = { TTYCODE_STRING, "Dsmg" },
	[TTYC_E3] = { TTYCODE_STRING, "E3" },
	[TTYC_ECH] = { TTYCODE_STRING, "ech" },
	[TTYC_ED] = { TTYCODE_STRING, "ed" },
	[TTYC_EL1] = { TTYCODE_STRING, "el1" },
	[TTYC_EL] = { TTYCODE_STRING, "el" },
	[TTYC_ENACS] = { TTYCODE_STRING, "enacs" },
	[TTYC_ENBP] = { TTYCODE_STRING, "Enbp" },
	[TTYC_ENEKS] = { TTYCODE_STRING, "Eneks" },
	[TTYC_ENFCS] = { TTYCODE_STRING, "Enfcs" },
	[TTYC_ENMG] = { TTYCODE_STRING, "Enmg" },
	[TTYC_FSL] = { TTYCODE_STRING, "fsl" },
	[TTYC_HLS] = { TTYCODE_STRING, "Hls" },
	[TTYC_HOME] = { TTYCODE_STRING, "home" },
	[TTYC_HPA] = { TTYCODE_STRING, "hpa" },
	[TTYC_ICH1] = { TTYCODE_STRING, "ich1" },
	[TTYC_ICH] = { TTYCODE_STRING, "ich" },
	[TTYC_IL1] = { TTYCODE_STRING, "il1" },
	[TTYC_IL] = { TTYCODE_STRING, "il" },
	[TTYC_INDN] = { TTYCODE_STRING, "indn" },
	[TTYC_INVIS] = { TTYCODE_STRING, "invis" },
	[TTYC_KCBT] = { TTYCODE_STRING, "kcbt" },
	[TTYC_KCUB1] = { TTYCODE_STRING, "kcub1" },
	[TTYC_KCUD1] = { TTYCODE_STRING, "kcud1" },
	[TTYC_KCUF1] = { TTYCODE_STRING, "kcuf1" },
	[TTYC_KCUU1] = { TTYCODE_STRING, "kcuu1" },
	[TTYC_KDC2] = { TTYCODE_STRING, "kDC" },
	[TTYC_KDC3] = { TTYCODE_STRING, "kDC3" },
	[TTYC_KDC4] = { TTYCODE_STRING, "kDC4" },
	[TTYC_KDC5] = { TTYCODE_STRING, "kDC5" },
	[TTYC_KDC6] = { TTYCODE_STRING, "kDC6" },
	[TTYC_KDC7] = { TTYCODE_STRING, "kDC7" },
	[TTYC_KDCH1] = { TTYCODE_STRING, "kdch1" },
	[TTYC_KDN2] = { TTYCODE_STRING, "kDN" }, /* not kDN2 */
	[TTYC_KDN3] = { TTYCODE_STRING, "kDN3" },
	[TTYC_KDN4] = { TTYCODE_STRING, "kDN4" },
	[TTYC_KDN5] = { TTYCODE_STRING, "kDN5" },
	[TTYC_KDN6] = { TTYCODE_STRING, "kDN6" },
	[TTYC_KDN7] = { TTYCODE_STRING, "kDN7" },
	[TTYC_KEND2] = { TTYCODE_STRING, "kEND" },
	[TTYC_KEND3] = { TTYCODE_STRING, "kEND3" },
	[TTYC_KEND4] = { TTYCODE_STRING, "kEND4" },
	[TTYC_KEND5] = { TTYCODE_STRING, "kEND5" },
	[TTYC_KEND6] = { TTYCODE_STRING, "kEND6" },
	[TTYC_KEND7] = { TTYCODE_STRING, "kEND7" },
	[TTYC_KEND] = { TTYCODE_STRING, "kend" },
	[TTYC_KF10] = { TTYCODE_STRING, "kf10" },
	[TTYC_KF11] = { TTYCODE_STRING, "kf11" },
	[TTYC_KF12] = { TTYCODE_STRING, "kf12" },
	[TTYC_KF13] = { TTYCODE_STRING, "kf13" },
	[TTYC_KF14] = { TTYCODE_STRING, "kf14" },
	[TTYC_KF15] = { TTYCODE_STRING, "kf15" },
	[TTYC_KF16] = { TTYCODE_STRING, "kf16" },
	[TTYC_KF17] = { TTYCODE_STRING, "kf17" },
	[TTYC_KF18] = { TTYCODE_STRING, "kf18" },
	[TTYC_KF19] = { TTYCODE_STRING, "kf19" },
	[TTYC_KF1] = { TTYCODE_STRING, "kf1" },
	[TTYC_KF20] = { TTYCODE_STRING, "kf20" },
	[TTYC_KF21] = { TTYCODE_STRING, "kf21" },
	[TTYC_KF22] = { TTYCODE_STRING, "kf22" },
	[TTYC_KF23] = { TTYCODE_STRING, "kf23" },
	[TTYC_KF24] = { TTYCODE_STRING, "kf24" },
	[TTYC_KF25] = { TTYCODE_STRING, "kf25" },
	[TTYC_KF26] = { TTYCODE_STRING, "kf26" },
	[TTYC_KF27] = { TTYCODE_STRING, "kf27" },
	[TTYC_KF28] = { TTYCODE_STRING, "kf28" },
	[TTYC_KF29] = { TTYCODE_STRING, "kf29" },
	[TTYC_KF2] = { TTYCODE_STRING, "kf2" },
	[TTYC_KF30] = { TTYCODE_STRING, "kf30" },
	[TTYC_KF31] = { TTYCODE_STRING, "kf31" },
	[TTYC_KF32] = { TTYCODE_STRING, "kf32" },
	[TTYC_KF33] = { TTYCODE_STRING, "kf33" },
	[TTYC_KF34] = { TTYCODE_STRING, "kf34" },
	[TTYC_KF35] = { TTYCODE_STRING, "kf35" },
	[TTYC_KF36] = { TTYCODE_STRING, "kf36" },
	[TTYC_KF37] = { TTYCODE_STRING, "kf37" },
	[TTYC_KF38] = { TTYCODE_STRING, "kf38" },
	[TTYC_KF39] = { TTYCODE_STRING, "kf39" },
	[TTYC_KF3] = { TTYCODE_STRING, "kf3" },
	[TTYC_KF40] = { TTYCODE_STRING, "kf40" },
	[TTYC_KF41] = { TTYCODE_STRING, "kf41" },
	[TTYC_KF42] = { TTYCODE_STRING, "kf42" },
	[TTYC_KF43] = { TTYCODE_STRING, "kf43" },
	[TTYC_KF44] = { TTYCODE_STRING, "kf44" },
	[TTYC_KF45] = { TTYCODE_STRING, "kf45" },
	[TTYC_KF46] = { TTYCODE_STRING, "kf46" },
	[TTYC_KF47] = { TTYCODE_STRING, "kf47" },
	[TTYC_KF48] = { TTYCODE_STRING, "kf48" },
	[TTYC_KF49] = { TTYCODE_STRING, "kf49" },
	[TTYC_KF4] = { TTYCODE_STRING, "kf4" },
	[TTYC_KF50] = { TTYCODE_STRING, "kf50" },
	[TTYC_KF51] = { TTYCODE_STRING, "kf51" },
	[TTYC_KF52] = { TTYCODE_STRING, "kf52" },
	[TTYC_KF53] = { TTYCODE_STRING, "kf53" },
	[TTYC_KF54] = { TTYCODE_STRING, "kf54" },
	[TTYC_KF55] = { TTYCODE_STRING, "kf55" },
	[TTYC_KF56] = { TTYCODE_STRING, "kf56" },
	[TTYC_KF57] = { TTYCODE_STRING, "kf57" },
	[TTYC_KF58] = { TTYCODE_STRING, "kf58" },
	[TTYC_KF59] = { TTYCODE_STRING, "kf59" },
	[TTYC_KF5] = { TTYCODE_STRING, "kf5" },
	[TTYC_KF60] = { TTYCODE_STRING, "kf60" },
	[TTYC_KF61] = { TTYCODE_STRING, "kf61" },
	[TTYC_KF62] = { TTYCODE_STRING, "kf62" },
	[TTYC_KF63] = { TTYCODE_STRING, "kf63" },
	[TTYC_KF6] = { TTYCODE_STRING, "kf6" },
	[TTYC_KF7] = { TTYCODE_STRING, "kf7" },
	[TTYC_KF8] = { TTYCODE_STRING, "kf8" },
	[TTYC_KF9] = { TTYCODE_STRING, "kf9" },
	[TTYC_KHOM2] = { TTYCODE_STRING, "kHOM" },
	[TTYC_KHOM3] = { TTYCODE_STRING, "kHOM3" },
	[TTYC_KHOM4] = { TTYCODE_STRING, "kHOM4" },
	[TTYC_KHOM5] = { TTYCODE_STRING, "kHOM5" },
	[TTYC_KHOM6] = { TTYCODE_STRING, "kHOM6" },
	[TTYC_KHOM7] = { TTYCODE_STRING, "kHOM7" },
	[TTYC_KHOME] = { TTYCODE_STRING, "khome" },
	[TTYC_KIC2] = { TTYCODE_STRING, "kIC" },
	[TTYC_KIC3] = { TTYCODE_STRING, "kIC3" },
	[TTYC_KIC4] = { TTYCODE_STRING, "kIC4" },
	[TTYC_KIC5] = { TTYCODE_STRING, "kIC5" },
	[TTYC_KIC6] = { TTYCODE_STRING, "kIC6" },
	[TTYC_KIC7] = { TTYCODE_STRING, "kIC7" },
	[TTYC_KICH1] = { TTYCODE_STRING, "kich1" },
	[TTYC_KIND] = { TTYCODE_STRING, "kind" },
	[TTYC_KLFT2] = { TTYCODE_STRING, "kLFT" },
	[TTYC_KLFT3] = { TTYCODE_STRING, "kLFT3" },
	[TTYC_KLFT4] = { TTYCODE_STRING, "kLFT4" },
	[TTYC_KLFT5] = { TTYCODE_STRING, "kLFT5" },
	[TTYC_KLFT6] = { TTYCODE_STRING, "kLFT6" },
	[TTYC_KLFT7] = { TTYCODE_STRING, "kLFT7" },
	[TTYC_KMOUS] = { TTYCODE_STRING, "kmous" },
	[TTYC_KNP] = { TTYCODE_STRING, "knp" },
	[TTYC_KNXT2] = { TTYCODE_STRING, "kNXT" },
	[TTYC_KNXT3] = { TTYCODE_STRING, "kNXT3" },
	[TTYC_KNXT4] = { TTYCODE_STRING, "kNXT4" },
	[TTYC_KNXT5] = { TTYCODE_STRING, "kNXT5" },
	[TTYC_KNXT6] = { TTYCODE_STRING, "kNXT6" },
	[TTYC_KNXT7] = { TTYCODE_STRING, "kNXT7" },
	[TTYC_KPP] = { TTYCODE_STRING, "kpp" },
	[TTYC_KPRV2] = { TTYCODE_STRING, "kPRV" },
	[TTYC_KPRV3] = { TTYCODE_STRING, "kPRV3" },
	[TTYC_KPRV4] = { TTYCODE_STRING, "kPRV4" },
	[TTYC_KPRV5] = { TTYCODE_STRING, "kPRV5" },
	[TTYC_KPRV6] = { TTYCODE_STRING, "kPRV6" },
	[TTYC_KPRV7] = { TTYCODE_STRING, "kPRV7" },
	[TTYC_KRIT2] = { TTYCODE_STRING, "kRIT" },
	[TTYC_KRIT3] = { TTYCODE_STRING, "kRIT3" },
	[TTYC_KRIT4] = { TTYCODE_STRING, "kRIT4" },
	[TTYC_KRIT5] = { TTYCODE_STRING, "kRIT5" },
	[TTYC_KRIT6] = { TTYCODE_STRING, "kRIT6" },
	[TTYC_KRIT7] = { TTYCODE_STRING, "kRIT7" },
	[TTYC_KRI] = { TTYCODE_STRING, "kri" },
	[TTYC_KUP2] = { TTYCODE_STRING, "kUP" }, /* not kUP2 */
	[TTYC_KUP3] = { TTYCODE_STRING, "kUP3" },
	[TTYC_KUP4] = { TTYCODE_STRING, "kUP4" },
	[TTYC_KUP5] = { TTYCODE_STRING, "kUP5" },
	[TTYC_KUP6] = { TTYCODE_STRING, "kUP6" },
	[TTYC_KUP7] = { TTYCODE_STRING, "kUP7" },
	[TTYC_MS] = { TTYCODE_STRING, "Ms" },
	[TTYC_NOBR] = { TTYCODE_STRING, "Nobr" },
	[TTYC_OL] = { TTYCODE_STRING, "ol" },
	[TTYC_OP] = { TTYCODE_STRING, "op" },
	[TTYC_RECT] = { TTYCODE_STRING, "Rect" },
	[TTYC_REV] = { TTYCODE_STRING, "rev" },
	[TTYC_RGB] = { TTYCODE_FLAG, "RGB" },
	[TTYC_RIN] = { TTYCODE_STRING, "rin" },
	[TTYC_RI] = { TTYCODE_STRING, "ri" },
	[TTYC_RMACS] = { TTYCODE_STRING, "rmacs" },
	[TTYC_RMCUP] = { TTYCODE_STRING, "rmcup" },
	[TTYC_RMKX] = { TTYCODE_STRING, "rmkx" },
	[TTYC_SETAB] = { TTYCODE_STRING, "setab" },
	[TTYC_SETAF] = { TTYCODE_STRING, "setaf" },
	[TTYC_SETAL] = { TTYCODE_STRING, "setal" },
	[TTYC_SETRGBB] = { TTYCODE_STRING, "setrgbb" },
	[TTYC_SETRGBF] = { TTYCODE_STRING, "setrgbf" },
	[TTYC_SETULC] = { TTYCODE_STRING, "Setulc" },
	[TTYC_SETULC1] = { TTYCODE_STRING, "Setulc1" },
	[TTYC_SE] = { TTYCODE_STRING, "Se" },
	[TTYC_SXL] =  { TTYCODE_FLAG, "Sxl" },
	[TTYC_SGR0] = { TTYCODE_STRING, "sgr0" },
	[TTYC_SITM] = { TTYCODE_STRING, "sitm" },
	[TTYC_SMACS] = { TTYCODE_STRING, "smacs" },
	[TTYC_SMCUP] = { TTYCODE_STRING, "smcup" },
	[TTYC_SMKX] = { TTYCODE_STRING, "smkx" },
	[TTYC_SMOL] = { TTYCODE_STRING, "Smol" },
	[TTYC_SMSO] = { TTYCODE_STRING, "smso" },
	[TTYC_SMULX] = { TTYCODE_STRING, "Smulx" },
	[TTYC_SMUL] = { TTYCODE_STRING, "smul" },
	[TTYC_SMXX] =  { TTYCODE_STRING, "smxx" },
	[TTYC_SPB] = { TTYCODE_STRING, "Spb" },
	[TTYC_SS] = { TTYCODE_STRING, "Ss" },
	[TTYC_SWD] = { TTYCODE_STRING, "Swd" },
	[TTYC_SYNC] = { TTYCODE_STRING, "Sync" },
	[TTYC_TC] = { TTYCODE_FLAG, "Tc" },
	[TTYC_TSL] = { TTYCODE_STRING, "tsl" },
	[TTYC_U8] = { TTYCODE_NUMBER, "U8" },
	[TTYC_VPA] = { TTYCODE_STRING, "vpa" },
	[TTYC_XT] = { TTYCODE_FLAG, "XT" }
};

#ifdef _WIN32
static const struct {
	const char	*name;
	const char	*value;
} tty_term_win32_caps[] = {
	{ "am", "1" },
	{ "AX", "1" },
	{ "bce", "1" },
	{ "colors", "256" },
	{ "RGB", "1" },
	{ "Tc", "1" },
	{ "XT", "1" },
	{ "bel", "\007" },
	{ "blink", "\033[5m" },
	{ "bold", "\033[1m" },
	{ "civis", "\033[?25l" },
	{ "clear", "\033[H\033[J" },
	{ "cnorm", "\033[?25h" },
	{ "Cr", "\r" },
	{ "csr", "\033[%d;%dr" },
	{ "cub1", "\033[D" },
	{ "cub", "\033[%dD" },
	{ "cud1", "\033[B" },
	{ "cud", "\033[%dB" },
	{ "cuf1", "\033[C" },
	{ "cuf", "\033[%dC" },
	{ "cup", "\033[%d;%dH" },
	{ "cuu1", "\033[A" },
	{ "cuu", "\033[%dA" },
	{ "cvvis", "\033[?25h" },
	{ "dch1", "\033[P" },
	{ "dch", "\033[%dP" },
	{ "dim", "\033[2m" },
	{ "dl1", "\033[M" },
	{ "dl", "\033[%dM" },
	{ "ech", "\033[%dX" },
	{ "ed", "\033[J" },
	{ "el1", "\033[1K" },
	{ "el", "\033[K" },
	{ "home", "\033[H" },
	{ "hpa", "\033[%dG" },
	{ "ich1", "\033[@" },
	{ "ich", "\033[%d@" },
	{ "il1", "\033[L" },
	{ "il", "\033[%dL" },
	{ "indn", "\033[%dS" },
	{ "kmous", "\033[M" },
	{ "Ms", "\033]52;%s;%s\007" },
	{ "op", "\033[39;49m" },
	{ "rev", "\033[7m" },
	{ "rin", "\033[%dT" },
	{ "ri", "\033M" },
	{ "rmcup", "\033[?1049l" },
	{ "rmkx", "\033[?1l\033>" },
	{ "setab", "\033[48;5;%dm" },
	{ "setaf", "\033[38;5;%dm" },
	{ "setrgbb", "\033[48;2;%d;%d;%dm" },
	{ "setrgbf", "\033[38;2;%d;%d;%dm" },
	{ "sgr0", "\033[0m" },
	{ "sitm", "\033[3m" },
	{ "smcup", "\033[?1049h" },
	{ "smkx", "\033[?1h\033=" },
	{ "smso", "\033[7m" },
	{ "smul", "\033[4m" },
	{ "smxx", "\033[9m" },
	{ "vpa", "\033[%dd" },
	{ "kcuu1", "\033[A" },
	{ "kcud1", "\033[B" },
	{ "kcub1", "\033[D" },
	{ "kcuf1", "\033[C" },
	{ "khome", "\033[H" },
	{ "kend", "\033[F" },
	{ "kich1", "\033[2~" },
	{ "kdch1", "\033[3~" },
	{ "knp", "\033[6~" },
	{ "kpp", "\033[5~" },
	{ "kcbt", "\033[Z" },
	{ "kf1", "\033OP" },
	{ "kf2", "\033OQ" },
	{ "kf3", "\033OR" },
	{ "kf4", "\033OS" },
	{ "kf5", "\033[15~" },
	{ "kf6", "\033[17~" },
	{ "kf7", "\033[18~" },
	{ "kf8", "\033[19~" },
	{ "kf9", "\033[20~" },
	{ "kf10", "\033[21~" },
	{ "kf11", "\033[23~" },
	{ "kf12", "\033[24~" }
};

static void
tty_term_win32_add_cap(char ***caps, u_int *ncaps, const char *name,
    const char *value)
{
	*caps = xreallocarray(*caps, (*ncaps) + 1, sizeof **caps);
	xasprintf(&(*caps)[*ncaps], "%s=%s", name, value);
	(*ncaps)++;
}

static int
tty_term_win32_format_ok(const char *s)
{
	for (; *s != '\0'; s++) {
		if (*s != '%')
			continue;
		s++;
		if (*s == '\0')
			return (0);
		if (*s != '%' && *s != 'd' && *s != 's')
			return (0);
	}
	return (1);
}

static int
tty_term_win32_indexed(enum tty_code_code code)
{
	switch (code) {
	case TTYC_CSR:
	case TTYC_CUP:
	case TTYC_HPA:
	case TTYC_VPA:
		return (1);
	default:
		return (0);
	}
}

enum tty_term_win32_value_type {
	TTY_TERM_WIN32_NUMBER,
	TTY_TERM_WIN32_STRING
};

struct tty_term_win32_value {
	enum tty_term_win32_value_type	 type;
	long				 number;
	const char			*string;
};

struct tty_term_win32_condition {
	int	parent_active;
	int	condition_true;
};

static char	*tty_term_win32_out;
static size_t	 tty_term_win32_outlen;
static size_t	 tty_term_win32_outsize;

static void
tty_term_win32_out_reset(void)
{
	free(tty_term_win32_out);
	tty_term_win32_out = NULL;
	tty_term_win32_outlen = 0;
	tty_term_win32_outsize = 0;
}

static void
tty_term_win32_out_reserve(size_t size)
{
	size_t	newsize;

	if (size <= tty_term_win32_outsize)
		return;
	newsize = (tty_term_win32_outsize == 0) ? 128 : tty_term_win32_outsize;
	while (newsize < size)
		newsize *= 2;
	tty_term_win32_out = xrealloc(tty_term_win32_out, newsize);
	tty_term_win32_outsize = newsize;
}

static void
tty_term_win32_out_append(const char *s, size_t len)
{
	if (len == 0)
		return;
	tty_term_win32_out_reserve(tty_term_win32_outlen + len + 1);
	memcpy(tty_term_win32_out + tty_term_win32_outlen, s, len);
	tty_term_win32_outlen += len;
	tty_term_win32_out[tty_term_win32_outlen] = '\0';
}

static void
tty_term_win32_out_char(char ch)
{
	tty_term_win32_out_append(&ch, 1);
}

static void
tty_term_win32_out_number(long n)
{
	char	tmp[64];

	xsnprintf(tmp, sizeof tmp, "%ld", n);
	tty_term_win32_out_append(tmp, strlen(tmp));
}

static const char *
tty_term_win32_out_done(void)
{
	if (tty_term_win32_out == NULL) {
		tty_term_win32_out_reserve(1);
		tty_term_win32_out[0] = '\0';
	}
	return (tty_term_win32_out);
}

static struct tty_term_win32_value
tty_term_win32_number(long n)
{
	struct tty_term_win32_value	value;

	value.type = TTY_TERM_WIN32_NUMBER;
	value.number = n;
	value.string = NULL;
	return (value);
}

static struct tty_term_win32_value
tty_term_win32_string(const char *s)
{
	struct tty_term_win32_value	value;

	value.type = TTY_TERM_WIN32_STRING;
	value.number = 0;
	value.string = s;
	return (value);
}

static long
tty_term_win32_value_number(const struct tty_term_win32_value *value)
{
	char	*end;

	if (value->type == TTY_TERM_WIN32_NUMBER)
		return (value->number);
	if (value->string == NULL)
		return (0);
	return (strtol(value->string, &end, 10));
}

static size_t
tty_term_win32_value_length(const struct tty_term_win32_value *value)
{
	char	tmp[64];

	if (value->type == TTY_TERM_WIN32_STRING && value->string != NULL)
		return (strlen(value->string));
	xsnprintf(tmp, sizeof tmp, "%ld", tty_term_win32_value_number(value));
	return (strlen(tmp));
}

static int
tty_term_win32_push(struct tty_term_win32_value *stack, u_int *nstack,
    struct tty_term_win32_value value)
{
	if (*nstack == 64)
		return (0);
	stack[(*nstack)++] = value;
	return (1);
}

static int
tty_term_win32_pop(struct tty_term_win32_value *stack, u_int *nstack,
    struct tty_term_win32_value *value)
{
	if (*nstack == 0)
		return (0);
	*value = stack[--(*nstack)];
	return (1);
}

static int
tty_term_win32_variable_index(int ch)
{
	if (ch >= 'a' && ch <= 'z')
		return (ch - 'a');
	if (ch >= 'A' && ch <= 'Z')
		return (26 + ch - 'A');
	return (-1);
}

static int
tty_term_win32_is_terminfo(const char *s)
{
	for (; *s != '\0'; s++) {
		if (*s != '%')
			continue;
		s++;
		if (*s == '\0')
			return (0);
		if (strchr("pPig{}'l?te;+-*/m&|^=<>AO!~", *s) != NULL)
			return (1);
	}
	return (0);
}

static int
tty_term_win32_out_format(const char **pp,
    const struct tty_term_win32_value *value)
{
	const char	*p = *pp;
	char		 fmt[32], *tmp = NULL;
	size_t		 n = 0;
	int		 spec;

	fmt[n++] = '%';
	if (*p == ':')
		p++;
	while ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
	    *p == ' ' || *p == '#' || *p == '.') {
		if (n == (sizeof fmt) - 3)
			return (0);
		fmt[n++] = *p++;
	}
	spec = *p;
	if (spec == '\0')
		return (0);
	if (spec != 'd' && spec != 'o' && spec != 'x' && spec != 'X' &&
	    spec != 's' && spec != 'c')
		return (0);
	fmt[n++] = spec;
	fmt[n] = '\0';

	if (spec == 's') {
		if (value->type == TTY_TERM_WIN32_STRING)
			xasprintf(&tmp, fmt,
			    value->string != NULL ? value->string : "");
		else {
			char	number[64];

			xsnprintf(number, sizeof number, "%ld", value->number);
			xasprintf(&tmp, fmt, number);
		}
	} else if (spec == 'c') {
		tty_term_win32_out_char((char)tty_term_win32_value_number(value));
		*pp = p;
		return (1);
	} else
		xasprintf(&tmp, fmt, (int)tty_term_win32_value_number(value));
	tty_term_win32_out_append(tmp, strlen(tmp));
	free(tmp);

	*pp = p;
	return (1);
}

static const char *
tty_term_win32_expand_printf(enum tty_code_code code, const char *s,
    const struct tty_term_win32_value *args, u_int nargs)
{
	struct tty_term_win32_value	 values[3];
	u_int				 i, next = 0;
	long				 n;

	if (!tty_term_win32_format_ok(s))
		return ("");

	for (i = 0; i < nargs && i < nitems(values); i++)
		values[i] = args[i];
	if (tty_term_win32_indexed(code)) {
		for (i = 0; i < nargs && i < 2; i++) {
			if (values[i].type == TTY_TERM_WIN32_NUMBER)
				values[i].number++;
		}
	}

	tty_term_win32_out_reset();
	for (; *s != '\0'; s++) {
		if (*s != '%') {
			tty_term_win32_out_char(*s);
			continue;
		}
		s++;
		if (*s == '\0') {
			tty_term_win32_out_reset();
			return ("");
		}
		if (*s == '%') {
			tty_term_win32_out_char('%');
			continue;
		}
		if (next == nargs) {
			tty_term_win32_out_reset();
			return ("");
		}
		if (*s == 's') {
			if (values[next].type == TTY_TERM_WIN32_STRING &&
			    values[next].string != NULL)
				tty_term_win32_out_append(values[next].string,
				    strlen(values[next].string));
			else
				tty_term_win32_out_number(
				    tty_term_win32_value_number(&values[next]));
		} else if (*s == 'd') {
			n = tty_term_win32_value_number(&values[next]);
			tty_term_win32_out_number(n);
		} else {
			tty_term_win32_out_reset();
			return ("");
		}
		next++;
	}
	return (tty_term_win32_out_done());
}

static const char *
tty_term_win32_expand_terminfo(const char *s,
    const struct tty_term_win32_value *args, u_int nargs)
{
	struct tty_term_win32_value	 params[9], stack[64], vars[52];
	struct tty_term_win32_value	 lhs, rhs, value;
	struct tty_term_win32_condition	 conds[16];
	const char			*p = s, *cp;
	u_int				 i, nstack = 0, nconds = 0;
	long				 n, number;
	int				 active = 1, idx, ch, neg;

	for (i = 0; i < nitems(params); i++)
		params[i] = tty_term_win32_number(0);
	for (i = 0; i < nargs && i < nitems(params); i++)
		params[i] = args[i];
	for (i = 0; i < nitems(vars); i++)
		vars[i] = tty_term_win32_number(0);

	tty_term_win32_out_reset();
	while (*p != '\0') {
		if (*p != '%') {
			if (active)
				tty_term_win32_out_char(*p);
			p++;
			continue;
		}
		p++;
		if (*p == '\0')
			goto error;

		switch (*p) {
		case '%':
			if (active)
				tty_term_win32_out_char('%');
			break;
		case 'i':
			if (active) {
				for (i = 0; i < 2; i++) {
					if (params[i].type == TTY_TERM_WIN32_NUMBER)
						params[i].number++;
				}
			}
			break;
		case 'p':
			p++;
			if (*p < '1' || *p > '9')
				goto error;
			if (active && !tty_term_win32_push(stack, &nstack,
			    params[*p - '1']))
				goto error;
			break;
		case 'P':
			p++;
			idx = tty_term_win32_variable_index(*p);
			if (idx == -1)
				goto error;
			if (active &&
			    !tty_term_win32_pop(stack, &nstack, &vars[idx]))
				goto error;
			break;
		case 'g':
			p++;
			idx = tty_term_win32_variable_index(*p);
			if (idx == -1)
				goto error;
			if (active &&
			    !tty_term_win32_push(stack, &nstack, vars[idx]))
				goto error;
			break;
		case '\'':
			p++;
			if (*p == '\0')
				goto error;
			ch = (u_char)*p++;
			if (*p != '\'')
				goto error;
			if (active &&
			    !tty_term_win32_push(stack, &nstack,
			    tty_term_win32_number(ch)))
				goto error;
			break;
		case '{':
			p++;
			neg = 0;
			if (*p == '-') {
				neg = 1;
				p++;
			}
			if (*p < '0' || *p > '9')
				goto error;
			number = 0;
			do {
				number = (number * 10) + (*p - '0');
				p++;
			} while (*p >= '0' && *p <= '9');
			if (*p != '}')
				goto error;
			if (neg)
				number = -number;
			if (active &&
			    !tty_term_win32_push(stack, &nstack,
			    tty_term_win32_number(number)))
				goto error;
			break;
		case 'l':
			if (active) {
				if (!tty_term_win32_pop(stack, &nstack, &value))
					goto error;
				if (!tty_term_win32_push(stack, &nstack,
				    tty_term_win32_number(
				    tty_term_win32_value_length(&value))))
					goto error;
			}
			break;
		case '+':
		case '-':
		case '*':
		case '/':
		case 'm':
		case '&':
		case '|':
		case '^':
		case '=':
		case '<':
		case '>':
		case 'A':
		case 'O':
			if (active) {
				if (!tty_term_win32_pop(stack, &nstack, &rhs) ||
				    !tty_term_win32_pop(stack, &nstack, &lhs))
					goto error;
				n = tty_term_win32_value_number(&lhs);
				number = tty_term_win32_value_number(&rhs);
				switch (*p) {
				case '+':
					n += number;
					break;
				case '-':
					n -= number;
					break;
				case '*':
					n *= number;
					break;
				case '/':
					n = (number == 0) ? 0 : n / number;
					break;
				case 'm':
					n = (number == 0) ? 0 : n % number;
					break;
				case '&':
					n &= number;
					break;
				case '|':
					n |= number;
					break;
				case '^':
					n ^= number;
					break;
				case '=':
					n = (n == number);
					break;
				case '<':
					n = (n < number);
					break;
				case '>':
					n = (n > number);
					break;
				case 'A':
					n = (n != 0 && number != 0);
					break;
				case 'O':
					n = (n != 0 || number != 0);
					break;
				}
				if (!tty_term_win32_push(stack, &nstack,
				    tty_term_win32_number(n)))
					goto error;
			}
			break;
		case '!':
		case '~':
			if (active) {
				if (!tty_term_win32_pop(stack, &nstack, &value))
					goto error;
				n = tty_term_win32_value_number(&value);
				if (*p == '!')
					n = (n == 0);
				else
					n = ~n;
				if (!tty_term_win32_push(stack, &nstack,
				    tty_term_win32_number(n)))
					goto error;
			}
			break;
		case '?':
			if (nconds == nitems(conds))
				goto error;
			conds[nconds].parent_active = active;
			conds[nconds].condition_true = 0;
			nconds++;
			break;
		case 't':
			if (nconds == 0)
				goto error;
			if (active) {
				if (!tty_term_win32_pop(stack, &nstack, &value))
					goto error;
				conds[nconds - 1].condition_true =
				    (tty_term_win32_value_number(&value) != 0);
				active = conds[nconds - 1].parent_active &&
				    conds[nconds - 1].condition_true;
			} else {
				active = 0;
			}
			break;
		case 'e':
			if (nconds == 0)
				goto error;
			active = conds[nconds - 1].parent_active &&
			    !conds[nconds - 1].condition_true;
			break;
		case ';':
			if (nconds == 0)
				goto error;
			active = conds[nconds - 1].parent_active;
			nconds--;
			break;
		default:
			if (active) {
				cp = p;
				if (!tty_term_win32_pop(stack, &nstack, &value))
					goto error;
				if (!tty_term_win32_out_format(&cp, &value))
					goto error;
				p = cp;
			} else {
				cp = p;
				if (*cp == ':')
					cp++;
				while ((*cp >= '0' && *cp <= '9') ||
				    *cp == '-' || *cp == '+' || *cp == ' ' ||
				    *cp == '#' || *cp == '.')
					cp++;
				if (*cp == '\0')
					goto error;
				p = cp;
			}
			break;
		}
		p++;
	}
	if (nconds != 0)
		goto error;
	return (tty_term_win32_out_done());

error:
	tty_term_win32_out_reset();
	return ("");
}

static const char *
tty_term_win32_expand(enum tty_code_code code, const char *s,
    const struct tty_term_win32_value *args, u_int nargs)
{
	if (tty_term_win32_is_terminfo(s))
		return (tty_term_win32_expand_terminfo(s, args, nargs));
	return (tty_term_win32_expand_printf(code, s, args, nargs));
}
#endif

#ifndef TMUX_TERMINAL_FORMAT_PROBE

u_int
tty_term_ncodes(void)
{
	return (nitems(tty_term_codes));
}

static char *
tty_term_strip(const char *s)
{
	const char     *ptr;
	static char	buf[8192];
	size_t		len;

	/* Ignore strings with no padding. */
	if (strchr(s, '$') == NULL)
		return (xstrdup(s));

	len = 0;
	for (ptr = s; *ptr != '\0'; ptr++) {
		if (*ptr == '$' && *(ptr + 1) == '<') {
			while (*ptr != '\0' && *ptr != '>')
				ptr++;
			if (*ptr == '>')
				ptr++;
			if (*ptr == '\0')
				break;
		}

		buf[len++] = *ptr;
		if (len == (sizeof buf) - 1)
			break;
	}
	buf[len] = '\0';

	return (xstrdup(buf));
}

static char *
tty_term_override_next(const char *s, size_t *offset)
{
	static char	value[8192];
	size_t		n = 0, at = *offset;

	if (s[at] == '\0')
		return (NULL);

	while (s[at] != '\0') {
		if (s[at] == ':') {
			if (s[at + 1] == ':') {
				value[n++] = ':';
				at += 2;
			} else
				break;
		} else {
			value[n++] = s[at];
			at++;
		}
		if (n == (sizeof value) - 1)
			return (NULL);
	}
	if (s[at] != '\0')
		*offset = at + 1;
	else
		*offset = at;
	value[n] = '\0';
	return (value);
}

void
tty_term_apply(struct tty_term *term, const char *capabilities, int quiet)
{
	const struct tty_term_code_entry	*ent;
	struct tty_code				*code;
	size_t                                   offset = 0;
	char					*cp, *value, *s;
	const char				*errstr, *name = term->name;
	u_int					 i;
	int					 n, remove;

	while ((s = tty_term_override_next(capabilities, &offset)) != NULL) {
		if (*s == '\0')
			continue;
		value = NULL;

		remove = 0;
		if ((cp = strchr(s, '=')) != NULL) {
			*cp++ = '\0';
			value = xstrdup(cp);
			if (strunvis(value, cp) == -1) {
				free(value);
				value = xstrdup(cp);
			}
		} else if (s[strlen(s) - 1] == '@') {
			s[strlen(s) - 1] = '\0';
			remove = 1;
		} else
			value = xstrdup("");

		if (!quiet) {
			if (remove)
				log_debug("%s override: %s@", name, s);
			else if (*value == '\0')
				log_debug("%s override: %s", name, s);
			else
				log_debug("%s override: %s=%s", name, s, value);
		}

		for (i = 0; i < tty_term_ncodes(); i++) {
			ent = &tty_term_codes[i];
			if (strcmp(s, ent->name) != 0)
				continue;
			code = &term->codes[i];

			if (remove) {
				code->type = TTYCODE_NONE;
				continue;
			}
			switch (ent->type) {
			case TTYCODE_NONE:
				break;
			case TTYCODE_STRING:
				if (code->type == TTYCODE_STRING)
					free(code->value.string);
				code->value.string = xstrdup(value);
				code->type = ent->type;
				break;
			case TTYCODE_NUMBER:
				n = strtonum(value, 0, INT_MAX, &errstr);
				if (errstr != NULL)
					break;
				code->value.number = n;
				code->type = ent->type;
				break;
			case TTYCODE_FLAG:
				code->value.flag = 1;
				code->type = ent->type;
				break;
			}
		}

		free(value);
	}
}

void
tty_term_apply_overrides(struct tty_term *term)
{
	struct options_entry		*o;
	struct options_array_item	*a;
	union options_value		*ov;
	const char			*s, *acs;
	size_t				 offset;
	char				*first;

	/* Update capabilities from the option. */
	o = options_get_only(global_options, "terminal-overrides");
	a = options_array_first(o);
	while (a != NULL) {
		ov = options_array_item_value(a);
		s = ov->string;

		offset = 0;
		first = tty_term_override_next(s, &offset);
		if (first != NULL && fnmatch(first, term->name, 0) == 0)
			tty_term_apply(term, s + offset, 0);
		a = options_array_next(a);
	}

	/* Log the SIXEL flag. */
	log_debug("SIXEL flag is %d", !!(term->flags & TERM_SIXEL));

	/* Update the RGB flag if the terminal has RGB colours. */
	if (tty_term_has(term, TTYC_SETRGBF) &&
	    tty_term_has(term, TTYC_SETRGBB))
		term->flags |= TERM_RGBCOLOURS;
	else
		term->flags &= ~TERM_RGBCOLOURS;
	log_debug("RGBCOLOURS flag is %d", !!(term->flags & TERM_RGBCOLOURS));

	/*
	 * Set or clear the DECSLRM flag if the terminal has the margin
	 * capabilities.
	 */
	if (tty_term_has(term, TTYC_CMG) && tty_term_has(term, TTYC_CLMG))
		term->flags |= TERM_DECSLRM;
	else
		term->flags &= ~TERM_DECSLRM;
	log_debug("DECSLRM flag is %d", !!(term->flags & TERM_DECSLRM));

	/*
	 * Set or clear the DECFRA flag if the terminal has the rectangle
	 * capability.
	 */
	if (tty_term_has(term, TTYC_RECT))
		term->flags |= TERM_DECFRA;
	else
		term->flags &= ~TERM_DECFRA;
	log_debug("DECFRA flag is %d", !!(term->flags & TERM_DECFRA));

	/*
	 * Terminals without am (auto right margin) wrap at at $COLUMNS - 1
	 * rather than $COLUMNS (the cursor can never be beyond $COLUMNS - 1).
	 *
	 * Terminals without xenl (eat newline glitch) ignore a newline beyond
	 * the right edge of the terminal, but tmux doesn't care about this -
	 * it always uses absolute only moves the cursor with a newline when
	 * also sending a linefeed.
	 *
	 * This is irritating, most notably because it is painful to write to
	 * the very bottom-right of the screen without scrolling.
	 *
	 * Flag the terminal here and apply some workarounds in other places to
	 * do the best possible.
	 */
	if (!tty_term_flag(term, TTYC_AM))
		term->flags |= TERM_NOAM;
	else
		term->flags &= ~TERM_NOAM;
	log_debug("NOAM flag is %d", !!(term->flags & TERM_NOAM));

	/* Generate ACS table. If none is present, use nearest ASCII. */
	memset(term->acs, 0, sizeof term->acs);
	if (tty_term_has(term, TTYC_ACSC))
		acs = tty_term_string(term, TTYC_ACSC);
	else
		acs = "a#j+k+l+m+n+o-p-q-r-s-t+u+v+w+x|y<z>~.";
	for (; acs[0] != '\0' && acs[1] != '\0'; acs += 2)
		term->acs[(u_char) acs[0]][0] = acs[1];
}

struct tty_term *
tty_term_create(struct tty *tty, char *name, char **caps, u_int ncaps,
    int *feat, char **cause)
{
	struct tty_term				*term;
	const struct tty_term_code_entry	*ent;
	struct tty_code				*code;
	struct options_entry			*o;
	struct options_array_item		*a;
	union options_value			*ov;
	u_int					 i, j;
	const char				*s, *value, *errstr;
	size_t					 offset, namelen;
	char					*first;
	int					 n;
	struct environ_entry			*envent;

	log_debug("adding term %s", name);

	term = xcalloc(1, sizeof *term);
	term->tty = tty;
	term->name = xstrdup(name);
	term->codes = xcalloc(tty_term_ncodes(), sizeof *term->codes);
	LIST_INSERT_HEAD(&tty_terms, term, entry);

	/* Fill in codes. */
	for (i = 0; i < ncaps; i++) {
		namelen = strcspn(caps[i], "=");
		if (namelen == 0)
			continue;
		value = caps[i] + namelen + 1;

		for (j = 0; j < tty_term_ncodes(); j++) {
			ent = &tty_term_codes[j];
			if (strncmp(ent->name, caps[i], namelen) != 0)
				continue;
			if (ent->name[namelen] != '\0')
				continue;

			code = &term->codes[j];
			code->type = TTYCODE_NONE;
			switch (ent->type) {
			case TTYCODE_NONE:
				break;
			case TTYCODE_STRING:
				code->type = TTYCODE_STRING;
				code->value.string = tty_term_strip(value);
				break;
			case TTYCODE_NUMBER:
				n = strtonum(value, 0, INT_MAX, &errstr);
				if (errstr != NULL)
					log_debug("%s: %s", ent->name, errstr);
				else {
					code->type = TTYCODE_NUMBER;
					code->value.number = n;
				}
				break;
			case TTYCODE_FLAG:
				code->type = TTYCODE_FLAG;
				code->value.flag = (*value == '1');
				break;
			}
		}
	}

	/* Apply terminal features. */
	o = options_get_only(global_options, "terminal-features");
	a = options_array_first(o);
	while (a != NULL) {
		ov = options_array_item_value(a);
		s = ov->string;

		offset = 0;
		first = tty_term_override_next(s, &offset);
		if (first != NULL && fnmatch(first, term->name, 0) == 0)
			tty_add_features(feat, s + offset, ":");
		a = options_array_next(a);
	}

	/* Delete curses data. */
#ifndef _WIN32
#if !defined(NCURSES_VERSION_MAJOR) || NCURSES_VERSION_MAJOR > 5 || \
    (NCURSES_VERSION_MAJOR == 5 && NCURSES_VERSION_MINOR > 6)
	del_curterm(cur_term);
#endif
#endif
	/* Check for COLORTERM. */
	envent = environ_find(tty->client->environ, "COLORTERM");
	if (envent != NULL) {
		log_debug("%s COLORTERM=%s", tty->client->name, envent->value);
		if (strcasecmp(envent->value, "truecolor") == 0 ||
		    strcasecmp(envent->value, "24bit") == 0)
			tty_add_features(feat, "RGB", ",");
 		else if (strstr(envent->value, "256") != NULL)
			tty_add_features(feat, "256", ",");
	}

	/* Apply overrides so any capabilities used for features are changed. */
	tty_term_apply_overrides(term);

	/* These are always required. */
	if (!tty_term_has(term, TTYC_CLEAR)) {
		xasprintf(cause, "terminal does not support clear");
		goto error;
	}
	if (!tty_term_has(term, TTYC_CUP)) {
		xasprintf(cause, "terminal does not support cup");
		goto error;
	}

	/*
	 * If TERM has XT or clear starts with CSI then it is safe to assume
	 * the terminal is derived from the VT100. This controls whether device
	 * attributes requests are sent to get more information.
	 *
	 * This is a bit of a hack but there aren't that many alternatives.
	 * Worst case tmux will just fall back to using whatever terminfo(5)
	 * says without trying to correct anything that is missing.
	 *
	 * Also add few features that VT100-like terminals should either
	 * support or safely ignore.
	 */
	s = tty_term_string(term, TTYC_CLEAR);
	if (tty_term_flag(term, TTYC_XT) || strncmp(s, "\033[", 2) == 0) {
		term->flags |= TERM_VT100LIKE;
		tty_add_features(feat, "bpaste,focus,title", ",");
	}

	/* Add RGB feature if terminal has RGB colours. */
	if ((tty_term_flag(term, TTYC_TC) || tty_term_has(term, TTYC_RGB)) &&
	    (!tty_term_has(term, TTYC_SETRGBF) ||
	    !tty_term_has(term, TTYC_SETRGBB)))
		tty_add_features(feat, "RGB", ",");

	/* Apply the features and overrides again. */
	if (tty_apply_features(term, *feat))
		tty_term_apply_overrides(term);

	/* Log the capabilities. */
	for (i = 0; i < tty_term_ncodes(); i++)
		log_debug("%s%s", name, tty_term_describe(term, i));

	return (term);

error:
	tty_term_free(term);
	return (NULL);
}

void
tty_term_free(struct tty_term *term)
{
	u_int	i;

	log_debug("removing term %s", term->name);

	for (i = 0; i < tty_term_ncodes(); i++) {
		if (term->codes[i].type == TTYCODE_STRING)
			free(term->codes[i].value.string);
	}
	free(term->codes);

	LIST_REMOVE(term, entry);
	free(term->name);
	free(term);
}

int
tty_term_read_list(const char *name, int fd, char ***caps, u_int *ncaps,
    char **cause)
{
#ifdef _WIN32
	u_int	i;

	(void)name;
	(void)fd;
	(void)cause;

	*ncaps = 0;
	*caps = NULL;
	for (i = 0; i < nitems(tty_term_win32_caps); i++) {
		tty_term_win32_add_cap(caps, ncaps,
		    tty_term_win32_caps[i].name,
		    tty_term_win32_caps[i].value);
	}
	return (0);
#else
	const struct tty_term_code_entry	*ent;
	int					 error, n;
	u_int					 i;
	const char				*s;
	char					 tmp[11];

	if (setupterm((char *)name, fd, &error) != OK) {
		switch (error) {
		case 1:
			xasprintf(cause, "can't use hardcopy terminal: %s",
			    name);
			break;
		case 0:
			xasprintf(cause, "missing or unsuitable terminal: %s",
			    name);
			break;
		case -1:
			xasprintf(cause, "can't find terminfo database");
			break;
		default:
			xasprintf(cause, "unknown error");
			break;
		}
		return (-1);
	}

	*ncaps = 0;
	*caps = NULL;

	for (i = 0; i < tty_term_ncodes(); i++) {
		ent = &tty_term_codes[i];
		switch (ent->type) {
		case TTYCODE_NONE:
			continue;
		case TTYCODE_STRING:
			s = tigetstr((char *)ent->name);
			if (s == NULL || s == (char *)-1)
				continue;
			break;
		case TTYCODE_NUMBER:
			n = tigetnum((char *)ent->name);
			if (n == -1 || n == -2)
				continue;
			xsnprintf(tmp, sizeof tmp, "%d", n);
			s = tmp;
			break;
		case TTYCODE_FLAG:
			n = tigetflag((char *)ent->name);
			if (n == -1)
				continue;
			if (n)
				s = "1";
			else
				s = "0";
			break;
		default:
			fatalx("unknown capability type");
		}
		*caps = xreallocarray(*caps, (*ncaps) + 1, sizeof **caps);
		xasprintf(&(*caps)[*ncaps], "%s=%s", ent->name, s);
		(*ncaps)++;
	}

#if !defined(NCURSES_VERSION_MAJOR) || NCURSES_VERSION_MAJOR > 5 || \
    (NCURSES_VERSION_MAJOR == 5 && NCURSES_VERSION_MINOR > 6)
	del_curterm(cur_term);
#endif
	return (0);
#endif
}

void
tty_term_free_list(char **caps, u_int ncaps)
{
	u_int	i;

	for (i = 0; i < ncaps; i++)
		free(caps[i]);
	free(caps);
}

int
tty_term_has(struct tty_term *term, enum tty_code_code code)
{
	return (term->codes[code].type != TTYCODE_NONE);
}

const char *
tty_term_string(struct tty_term *term, enum tty_code_code code)
{
	if (!tty_term_has(term, code))
		return ("");
	if (term->codes[code].type != TTYCODE_STRING)
		fatalx("not a string: %d", code);
	return (term->codes[code].value.string);
}

const char *
tty_term_string_i(struct tty_term *term, enum tty_code_code code, int a)
{
	const char	*x = tty_term_string(term, code), *s;

#ifdef _WIN32
	struct tty_term_win32_value	args[1];

	args[0] = tty_term_win32_number(a);
	return (tty_term_win32_expand(code, x, args, nitems(args)));
#else
#if defined(HAVE_TIPARM_S)
	s = tiparm_s(1, 0, x, a);
#elif defined(HAVE_TIPARM)
	s = tiparm(x, a);
#else
	s = tparm((char *)x, a, 0, 0, 0, 0, 0, 0, 0, 0);
#endif
	if (s == NULL) {
		log_debug("could not expand %s", tty_term_codes[code].name);
		return ("");
	}
	return (s);
#endif
}

const char *
tty_term_string_ii(struct tty_term *term, enum tty_code_code code, int a, int b)
{
	const char	*x = tty_term_string(term, code), *s;

#ifdef _WIN32
	struct tty_term_win32_value	args[2];

	args[0] = tty_term_win32_number(a);
	args[1] = tty_term_win32_number(b);
	return (tty_term_win32_expand(code, x, args, nitems(args)));
#else
#if defined(HAVE_TIPARM_S)
	s = tiparm_s(2, 0, x, a, b);
#elif defined(HAVE_TIPARM)
	s = tiparm(x, a, b);
#else
	s = tparm((char *)x, a, b, 0, 0, 0, 0, 0, 0, 0);
#endif
	if (s == NULL) {
		log_debug("could not expand %s", tty_term_codes[code].name);
		return ("");
	}
	return (s);
#endif
}

const char *
tty_term_string_iii(struct tty_term *term, enum tty_code_code code, int a,
    int b, int c)
{
	const char	*x = tty_term_string(term, code), *s;

#ifdef _WIN32
	struct tty_term_win32_value	args[3];

	args[0] = tty_term_win32_number(a);
	args[1] = tty_term_win32_number(b);
	args[2] = tty_term_win32_number(c);
	return (tty_term_win32_expand(code, x, args, nitems(args)));
#else
#if defined(HAVE_TIPARM_S)
	s = tiparm_s(3, 0, x, a, b, c);
#elif defined(HAVE_TIPARM)
	s = tiparm(x, a, b, c);
#else
	s = tparm((char *)x, a, b, c, 0, 0, 0, 0, 0, 0);
#endif
	if (s == NULL) {
		log_debug("could not expand %s", tty_term_codes[code].name);
		return ("");
	}
	return (s);
#endif
}

const char *
tty_term_string_s(struct tty_term *term, enum tty_code_code code, const char *a)
{
	const char	*x = tty_term_string(term, code), *s;

#ifdef _WIN32
	struct tty_term_win32_value	args[1];

	args[0] = tty_term_win32_string(a);
	return (tty_term_win32_expand(code, x, args, nitems(args)));
#else
#if defined(HAVE_TIPARM_S)
	s = tiparm_s(1, 1, x, a);
#elif defined(HAVE_TIPARM)
	s = tiparm(x, a);
#else
	s = tparm((char *)x, (long)a, 0, 0, 0, 0, 0, 0, 0, 0);
#endif
	if (s == NULL) {
		log_debug("could not expand %s", tty_term_codes[code].name);
		return ("");
	}
	return (s);
#endif
}

const char *
tty_term_string_ss(struct tty_term *term, enum tty_code_code code,
    const char *a, const char *b)
{
	const char	*x = tty_term_string(term, code), *s;

#ifdef _WIN32
	struct tty_term_win32_value	args[2];

	args[0] = tty_term_win32_string(a);
	args[1] = tty_term_win32_string(b);
	return (tty_term_win32_expand(code, x, args, nitems(args)));
#else
#if defined(HAVE_TIPARM_S)
	s = tiparm_s(2, 3, x, a, b);
#elif defined(HAVE_TIPARM)
	s = tiparm(x, a, b);
#else
	s = tparm((char *)x, (long)a, (long)b, 0, 0, 0, 0, 0, 0, 0);
#endif
	if (s == NULL) {
		log_debug("could not expand %s", tty_term_codes[code].name);
		return ("");
	}
	return (s);
#endif
}

int
tty_term_number(struct tty_term *term, enum tty_code_code code)
{
	if (!tty_term_has(term, code))
		return (0);
	if (term->codes[code].type != TTYCODE_NUMBER)
		fatalx("not a number: %d", code);
	return (term->codes[code].value.number);
}

int
tty_term_flag(struct tty_term *term, enum tty_code_code code)
{
	if (!tty_term_has(term, code))
		return (0);
	if (term->codes[code].type != TTYCODE_FLAG)
		fatalx("not a flag: %d", code);
	return (term->codes[code].value.flag);
}

const char *
tty_term_describe(struct tty_term *term, enum tty_code_code code)
{
	static char	 s[256];
	char		 out[128];

	switch (term->codes[code].type) {
	case TTYCODE_NONE:
		xsnprintf(s, sizeof s, "%4u: %s: [missing]",
		    code, tty_term_codes[code].name);
		break;
	case TTYCODE_STRING:
		strnvis(out, term->codes[code].value.string, sizeof out,
		    VIS_OCTAL|VIS_CSTYLE|VIS_TAB|VIS_NL);
		xsnprintf(s, sizeof s, "%4u: %s: (string) %s",
		    code, tty_term_codes[code].name,
		    out);
		break;
	case TTYCODE_NUMBER:
		xsnprintf(s, sizeof s, "%4u: %s: (number) %d",
		    code, tty_term_codes[code].name,
		    term->codes[code].value.number);
		break;
	case TTYCODE_FLAG:
		xsnprintf(s, sizeof s, "%4u: %s: (flag) %s",
		    code, tty_term_codes[code].name,
		    term->codes[code].value.flag ? "true" : "false");
		break;
	}
	return (s);
}

#endif /* !TMUX_TERMINAL_FORMAT_PROBE */
