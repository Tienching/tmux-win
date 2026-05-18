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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "compat.h"
#include "tmux.h"

struct utf8_width_item {
	utf8_wchar			wc;
	u_int				width;
	int				allocated;

	RB_ENTRY(utf8_width_item)	entry;
};

static int
utf8_width_cache_cmp(struct utf8_width_item *uw1, struct utf8_width_item *uw2)
{
	if (uw1->wc < uw2->wc)
		return (-1);
	if (uw1->wc > uw2->wc)
		return (1);
	return (0);
}
RB_HEAD(utf8_width_cache, utf8_width_item);
RB_GENERATE_STATIC(utf8_width_cache, utf8_width_item, entry,
    utf8_width_cache_cmp);
static struct utf8_width_cache utf8_width_cache =
    RB_INITIALIZER(utf8_width_cache);

static struct utf8_width_item utf8_default_width_cache[] = {
	{ .wc = 0x0261D, .width = 2 },
	{ .wc = 0x026F9, .width = 2 },
	{ .wc = 0x0270A, .width = 2 },
	{ .wc = 0x0270B, .width = 2 },
	{ .wc = 0x0270C, .width = 2 },
	{ .wc = 0x0270D, .width = 2 },
	{ .wc = 0x1F1E6, .width = 1 },
	{ .wc = 0x1F1E7, .width = 1 },
	{ .wc = 0x1F1E8, .width = 1 },
	{ .wc = 0x1F1E9, .width = 1 },
	{ .wc = 0x1F1EA, .width = 1 },
	{ .wc = 0x1F1EB, .width = 1 },
	{ .wc = 0x1F1EC, .width = 1 },
	{ .wc = 0x1F1ED, .width = 1 },
	{ .wc = 0x1F1EE, .width = 1 },
	{ .wc = 0x1F1EF, .width = 1 },
	{ .wc = 0x1F1F0, .width = 1 },
	{ .wc = 0x1F1F1, .width = 1 },
	{ .wc = 0x1F1F2, .width = 1 },
	{ .wc = 0x1F1F3, .width = 1 },
	{ .wc = 0x1F1F4, .width = 1 },
	{ .wc = 0x1F1F5, .width = 1 },
	{ .wc = 0x1F1F6, .width = 1 },
	{ .wc = 0x1F1F7, .width = 1 },
	{ .wc = 0x1F1F8, .width = 1 },
	{ .wc = 0x1F1F9, .width = 1 },
	{ .wc = 0x1F1FA, .width = 1 },
	{ .wc = 0x1F1FB, .width = 1 },
	{ .wc = 0x1F1FC, .width = 1 },
	{ .wc = 0x1F1FD, .width = 1 },
	{ .wc = 0x1F1FE, .width = 1 },
	{ .wc = 0x1F1FF, .width = 1 },
	{ .wc = 0x1F385, .width = 2 },
	{ .wc = 0x1F3C2, .width = 2 },
	{ .wc = 0x1F3C3, .width = 2 },
	{ .wc = 0x1F3C4, .width = 2 },
	{ .wc = 0x1F3C7, .width = 2 },
	{ .wc = 0x1F3CA, .width = 2 },
	{ .wc = 0x1F3CB, .width = 2 },
	{ .wc = 0x1F3CC, .width = 2 },
	{ .wc = 0x1F3FB, .width = 2 },
	{ .wc = 0x1F3FC, .width = 2 },
	{ .wc = 0x1F3FD, .width = 2 },
	{ .wc = 0x1F3FE, .width = 2 },
	{ .wc = 0x1F3FF, .width = 2 },
	{ .wc = 0x1F442, .width = 2 },
	{ .wc = 0x1F443, .width = 2 },
	{ .wc = 0x1F446, .width = 2 },
	{ .wc = 0x1F447, .width = 2 },
	{ .wc = 0x1F448, .width = 2 },
	{ .wc = 0x1F449, .width = 2 },
	{ .wc = 0x1F44A, .width = 2 },
	{ .wc = 0x1F44B, .width = 2 },
	{ .wc = 0x1F44C, .width = 2 },
	{ .wc = 0x1F44D, .width = 2 },
	{ .wc = 0x1F44E, .width = 2 },
	{ .wc = 0x1F44F, .width = 2 },
	{ .wc = 0x1F450, .width = 2 },
	{ .wc = 0x1F466, .width = 2 },
	{ .wc = 0x1F467, .width = 2 },
	{ .wc = 0x1F468, .width = 2 },
	{ .wc = 0x1F469, .width = 2 },
	{ .wc = 0x1F46B, .width = 2 },
	{ .wc = 0x1F46C, .width = 2 },
	{ .wc = 0x1F46D, .width = 2 },
	{ .wc = 0x1F46E, .width = 2 },
	{ .wc = 0x1F470, .width = 2 },
	{ .wc = 0x1F471, .width = 2 },
	{ .wc = 0x1F472, .width = 2 },
	{ .wc = 0x1F473, .width = 2 },
	{ .wc = 0x1F474, .width = 2 },
	{ .wc = 0x1F475, .width = 2 },
	{ .wc = 0x1F476, .width = 2 },
	{ .wc = 0x1F477, .width = 2 },
	{ .wc = 0x1F478, .width = 2 },
	{ .wc = 0x1F47C, .width = 2 },
	{ .wc = 0x1F481, .width = 2 },
	{ .wc = 0x1F482, .width = 2 },
	{ .wc = 0x1F483, .width = 2 },
	{ .wc = 0x1F485, .width = 2 },
	{ .wc = 0x1F486, .width = 2 },
	{ .wc = 0x1F487, .width = 2 },
	{ .wc = 0x1F48F, .width = 2 },
	{ .wc = 0x1F491, .width = 2 },
	{ .wc = 0x1F4AA, .width = 2 },
	{ .wc = 0x1F574, .width = 2 },
	{ .wc = 0x1F575, .width = 2 },
	{ .wc = 0x1F57A, .width = 2 },
	{ .wc = 0x1F590, .width = 2 },
	{ .wc = 0x1F595, .width = 2 },
	{ .wc = 0x1F596, .width = 2 },
	{ .wc = 0x1F645, .width = 2 },
	{ .wc = 0x1F646, .width = 2 },
	{ .wc = 0x1F647, .width = 2 },
	{ .wc = 0x1F64B, .width = 2 },
	{ .wc = 0x1F64C, .width = 2 },
	{ .wc = 0x1F64D, .width = 2 },
	{ .wc = 0x1F64E, .width = 2 },
	{ .wc = 0x1F64F, .width = 2 },
	{ .wc = 0x1F6A3, .width = 2 },
	{ .wc = 0x1F6B4, .width = 2 },
	{ .wc = 0x1F6B5, .width = 2 },
	{ .wc = 0x1F6B6, .width = 2 },
	{ .wc = 0x1F6C0, .width = 2 },
	{ .wc = 0x1F6CC, .width = 2 },
	{ .wc = 0x1F90C, .width = 2 },
	{ .wc = 0x1F90F, .width = 2 },
	{ .wc = 0x1F918, .width = 2 },
	{ .wc = 0x1F919, .width = 2 },
	{ .wc = 0x1F91A, .width = 2 },
	{ .wc = 0x1F91B, .width = 2 },
	{ .wc = 0x1F91C, .width = 2 },
	{ .wc = 0x1F91D, .width = 2 },
	{ .wc = 0x1F91E, .width = 2 },
	{ .wc = 0x1F91F, .width = 2 },
	{ .wc = 0x1F926, .width = 2 },
	{ .wc = 0x1F930, .width = 2 },
	{ .wc = 0x1F931, .width = 2 },
	{ .wc = 0x1F932, .width = 2 },
	{ .wc = 0x1F933, .width = 2 },
	{ .wc = 0x1F934, .width = 2 },
	{ .wc = 0x1F935, .width = 2 },
	{ .wc = 0x1F936, .width = 2 },
	{ .wc = 0x1F937, .width = 2 },
	{ .wc = 0x1F938, .width = 2 },
	{ .wc = 0x1F939, .width = 2 },
	{ .wc = 0x1F93D, .width = 2 },
	{ .wc = 0x1F93E, .width = 2 },
	{ .wc = 0x1F977, .width = 2 },
	{ .wc = 0x1F9B5, .width = 2 },
	{ .wc = 0x1F9B6, .width = 2 },
	{ .wc = 0x1F9B8, .width = 2 },
	{ .wc = 0x1F9B9, .width = 2 },
	{ .wc = 0x1F9BB, .width = 2 },
	{ .wc = 0x1F9CD, .width = 2 },
	{ .wc = 0x1F9CE, .width = 2 },
	{ .wc = 0x1F9CF, .width = 2 },
	{ .wc = 0x1F9D1, .width = 2 },
	{ .wc = 0x1F9D2, .width = 2 },
	{ .wc = 0x1F9D3, .width = 2 },
	{ .wc = 0x1F9D4, .width = 2 },
	{ .wc = 0x1F9D5, .width = 2 },
	{ .wc = 0x1F9D6, .width = 2 },
	{ .wc = 0x1F9D7, .width = 2 },
	{ .wc = 0x1F9D8, .width = 2 },
	{ .wc = 0x1F9D9, .width = 2 },
	{ .wc = 0x1F9DA, .width = 2 },
	{ .wc = 0x1F9DB, .width = 2 },
	{ .wc = 0x1F9DC, .width = 2 },
	{ .wc = 0x1F9DD, .width = 2 },
	{ .wc = 0x1FAC3, .width = 2 },
	{ .wc = 0x1FAC4, .width = 2 },
	{ .wc = 0x1FAC5, .width = 2 },
	{ .wc = 0x1FAF0, .width = 2 },
	{ .wc = 0x1FAF1, .width = 2 },
	{ .wc = 0x1FAF2, .width = 2 },
	{ .wc = 0x1FAF3, .width = 2 },
	{ .wc = 0x1FAF4, .width = 2 },
	{ .wc = 0x1FAF5, .width = 2 },
	{ .wc = 0x1FAF6, .width = 2 },
	{ .wc = 0x1FAF7, .width = 2 },
	{ .wc = 0x1FAF8, .width = 2 }
};

struct utf8_item {
	RB_ENTRY(utf8_item)	index_entry;
	u_int			index;

	RB_ENTRY(utf8_item)	data_entry;
	char			data[UTF8_SIZE];
	u_char			size;
};

static int
utf8_data_cmp(struct utf8_item *ui1, struct utf8_item *ui2)
{
	if (ui1->size < ui2->size)
		return (-1);
	if (ui1->size > ui2->size)
		return (1);
	return (memcmp(ui1->data, ui2->data, ui1->size));
}
RB_HEAD(utf8_data_tree, utf8_item);
RB_GENERATE_STATIC(utf8_data_tree, utf8_item, data_entry, utf8_data_cmp);
static struct utf8_data_tree utf8_data_tree = RB_INITIALIZER(utf8_data_tree);

static int
utf8_index_cmp(struct utf8_item *ui1, struct utf8_item *ui2)
{
	if (ui1->index < ui2->index)
		return (-1);
	if (ui1->index > ui2->index)
		return (1);
	return (0);
}
RB_HEAD(utf8_index_tree, utf8_item);
RB_GENERATE_STATIC(utf8_index_tree, utf8_item, index_entry, utf8_index_cmp);
static struct utf8_index_tree utf8_index_tree = RB_INITIALIZER(utf8_index_tree);

static int	utf8_no_width;
static u_int	utf8_next_index;

#if defined(_WIN32) && !defined(HAVE_UTF8PROC)
struct utf8_width_range {
	utf8_wchar	first;
	utf8_wchar	last;
};

static const struct utf8_width_range utf8_win32_zero_width[] = {
	{ 0x0300, 0x036f },
	{ 0x0483, 0x0489 },
	{ 0x0591, 0x05bd },
	{ 0x05bf, 0x05bf },
	{ 0x05c1, 0x05c2 },
	{ 0x05c4, 0x05c5 },
	{ 0x05c7, 0x05c7 },
	{ 0x0610, 0x061a },
	{ 0x064b, 0x065f },
	{ 0x0670, 0x0670 },
	{ 0x06d6, 0x06dc },
	{ 0x06df, 0x06e4 },
	{ 0x06e7, 0x06e8 },
	{ 0x06ea, 0x06ed },
	{ 0x0711, 0x0711 },
	{ 0x0730, 0x074a },
	{ 0x07a6, 0x07b0 },
	{ 0x07eb, 0x07f3 },
	{ 0x0816, 0x0819 },
	{ 0x081b, 0x0823 },
	{ 0x0825, 0x0827 },
	{ 0x0829, 0x082d },
	{ 0x0859, 0x085b },
	{ 0x08d3, 0x08e1 },
	{ 0x08e3, 0x0902 },
	{ 0x093a, 0x093a },
	{ 0x093c, 0x093c },
	{ 0x0941, 0x0948 },
	{ 0x094d, 0x094d },
	{ 0x0951, 0x0957 },
	{ 0x0962, 0x0963 },
	{ 0x0981, 0x0981 },
	{ 0x09bc, 0x09bc },
	{ 0x09c1, 0x09c4 },
	{ 0x09cd, 0x09cd },
	{ 0x09e2, 0x09e3 },
	{ 0x0a01, 0x0a02 },
	{ 0x0a3c, 0x0a3c },
	{ 0x0a41, 0x0a42 },
	{ 0x0a47, 0x0a48 },
	{ 0x0a4b, 0x0a4d },
	{ 0x0a51, 0x0a51 },
	{ 0x0a70, 0x0a71 },
	{ 0x0a75, 0x0a75 },
	{ 0x0a81, 0x0a82 },
	{ 0x0abc, 0x0abc },
	{ 0x0ac1, 0x0ac5 },
	{ 0x0ac7, 0x0ac8 },
	{ 0x0acd, 0x0acd },
	{ 0x0ae2, 0x0ae3 },
	{ 0x0b01, 0x0b01 },
	{ 0x0b3c, 0x0b3c },
	{ 0x0b3f, 0x0b3f },
	{ 0x0b41, 0x0b44 },
	{ 0x0b4d, 0x0b4d },
	{ 0x0b56, 0x0b56 },
	{ 0x0b62, 0x0b63 },
	{ 0x0b82, 0x0b82 },
	{ 0x0bc0, 0x0bc0 },
	{ 0x0bcd, 0x0bcd },
	{ 0x0c00, 0x0c00 },
	{ 0x0c04, 0x0c04 },
	{ 0x0c3e, 0x0c40 },
	{ 0x0c46, 0x0c48 },
	{ 0x0c4a, 0x0c4d },
	{ 0x0c55, 0x0c56 },
	{ 0x0c62, 0x0c63 },
	{ 0x0c81, 0x0c81 },
	{ 0x0cbc, 0x0cbc },
	{ 0x0cbf, 0x0cbf },
	{ 0x0cc6, 0x0cc6 },
	{ 0x0ccc, 0x0ccd },
	{ 0x0ce2, 0x0ce3 },
	{ 0x0d00, 0x0d01 },
	{ 0x0d3b, 0x0d3c },
	{ 0x0d41, 0x0d44 },
	{ 0x0d4d, 0x0d4d },
	{ 0x0d62, 0x0d63 },
	{ 0x0dca, 0x0dca },
	{ 0x0dd2, 0x0dd4 },
	{ 0x0dd6, 0x0dd6 },
	{ 0x0e31, 0x0e31 },
	{ 0x0e34, 0x0e3a },
	{ 0x0e47, 0x0e4e },
	{ 0x0eb1, 0x0eb1 },
	{ 0x0eb4, 0x0ebc },
	{ 0x0ec8, 0x0ecd },
	{ 0x0f18, 0x0f19 },
	{ 0x0f35, 0x0f35 },
	{ 0x0f37, 0x0f37 },
	{ 0x0f39, 0x0f39 },
	{ 0x0f71, 0x0f7e },
	{ 0x0f80, 0x0f84 },
	{ 0x0f86, 0x0f87 },
	{ 0x0f8d, 0x0f97 },
	{ 0x0f99, 0x0fbc },
	{ 0x0fc6, 0x0fc6 },
	{ 0x102d, 0x1030 },
	{ 0x1032, 0x1037 },
	{ 0x1039, 0x103a },
	{ 0x103d, 0x103e },
	{ 0x1058, 0x1059 },
	{ 0x105e, 0x1060 },
	{ 0x1071, 0x1074 },
	{ 0x1082, 0x1082 },
	{ 0x1085, 0x1086 },
	{ 0x108d, 0x108d },
	{ 0x109d, 0x109d },
	{ 0x135d, 0x135f },
	{ 0x1712, 0x1714 },
	{ 0x1732, 0x1734 },
	{ 0x1752, 0x1753 },
	{ 0x1772, 0x1773 },
	{ 0x17b4, 0x17b5 },
	{ 0x17b7, 0x17bd },
	{ 0x17c6, 0x17c6 },
	{ 0x17c9, 0x17d3 },
	{ 0x17dd, 0x17dd },
	{ 0x180b, 0x180d },
	{ 0x1885, 0x1886 },
	{ 0x18a9, 0x18a9 },
	{ 0x1920, 0x1922 },
	{ 0x1927, 0x1928 },
	{ 0x1932, 0x1932 },
	{ 0x1939, 0x193b },
	{ 0x1a17, 0x1a18 },
	{ 0x1a1b, 0x1a1b },
	{ 0x1a56, 0x1a56 },
	{ 0x1a58, 0x1a5e },
	{ 0x1a60, 0x1a60 },
	{ 0x1a62, 0x1a62 },
	{ 0x1a65, 0x1a6c },
	{ 0x1a73, 0x1a7c },
	{ 0x1a7f, 0x1a7f },
	{ 0x1ab0, 0x1ace },
	{ 0x1b00, 0x1b03 },
	{ 0x1b34, 0x1b34 },
	{ 0x1b36, 0x1b3a },
	{ 0x1b3c, 0x1b3c },
	{ 0x1b42, 0x1b42 },
	{ 0x1b6b, 0x1b73 },
	{ 0x1b80, 0x1b81 },
	{ 0x1ba2, 0x1ba5 },
	{ 0x1ba8, 0x1ba9 },
	{ 0x1bab, 0x1bad },
	{ 0x1be6, 0x1be6 },
	{ 0x1be8, 0x1be9 },
	{ 0x1bed, 0x1bed },
	{ 0x1bef, 0x1bf1 },
	{ 0x1c2c, 0x1c33 },
	{ 0x1c36, 0x1c37 },
	{ 0x1cd0, 0x1cd2 },
	{ 0x1cd4, 0x1ce0 },
	{ 0x1ce2, 0x1ce8 },
	{ 0x1ced, 0x1ced },
	{ 0x1cf4, 0x1cf4 },
	{ 0x1cf8, 0x1cf9 },
	{ 0x1dc0, 0x1dff },
	{ 0x200b, 0x200f },
	{ 0x202a, 0x202e },
	{ 0x2060, 0x2064 },
	{ 0x2066, 0x206f },
	{ 0x20d0, 0x20ff },
	{ 0x2cef, 0x2cf1 },
	{ 0x2d7f, 0x2d7f },
	{ 0x2de0, 0x2dff },
	{ 0x302a, 0x302f },
	{ 0x3099, 0x309a },
	{ 0xa66f, 0xa672 },
	{ 0xa674, 0xa67d },
	{ 0xa69e, 0xa69f },
	{ 0xa6f0, 0xa6f1 },
	{ 0xa802, 0xa802 },
	{ 0xa806, 0xa806 },
	{ 0xa80b, 0xa80b },
	{ 0xa825, 0xa826 },
	{ 0xa8c4, 0xa8c5 },
	{ 0xa8e0, 0xa8f1 },
	{ 0xa926, 0xa92d },
	{ 0xa947, 0xa951 },
	{ 0xa980, 0xa982 },
	{ 0xa9b3, 0xa9b3 },
	{ 0xa9b6, 0xa9b9 },
	{ 0xa9bc, 0xa9bd },
	{ 0xa9e5, 0xa9e5 },
	{ 0xaa29, 0xaa2e },
	{ 0xaa31, 0xaa32 },
	{ 0xaa35, 0xaa36 },
	{ 0xaa43, 0xaa43 },
	{ 0xaa4c, 0xaa4c },
	{ 0xaa7c, 0xaa7c },
	{ 0xaab0, 0xaab0 },
	{ 0xaab2, 0xaab4 },
	{ 0xaab7, 0xaab8 },
	{ 0xaabe, 0xaabf },
	{ 0xaac1, 0xaac1 },
	{ 0xaaec, 0xaaed },
	{ 0xaaf6, 0xaaf6 },
	{ 0xabe5, 0xabe5 },
	{ 0xabe8, 0xabe8 },
	{ 0xabed, 0xabed },
	{ 0xfb1e, 0xfb1e },
	{ 0xfe00, 0xfe0f },
	{ 0xfe20, 0xfe2f },
	{ 0xfeff, 0xfeff },
	{ 0xfff9, 0xfffb },
	{ 0x101fd, 0x101fd },
	{ 0x102e0, 0x102e0 },
	{ 0x10376, 0x1037a },
	{ 0x10a01, 0x10a03 },
	{ 0x10a05, 0x10a06 },
	{ 0x10a0c, 0x10a0f },
	{ 0x10a38, 0x10a3a },
	{ 0x10a3f, 0x10a3f },
	{ 0x10ae5, 0x10ae6 },
	{ 0x11001, 0x11001 },
	{ 0x11038, 0x11046 },
	{ 0x11070, 0x11070 },
	{ 0x11073, 0x11074 },
	{ 0x1107f, 0x11081 },
	{ 0x110b3, 0x110b6 },
	{ 0x110b9, 0x110ba },
	{ 0x110c2, 0x110c2 },
	{ 0x11100, 0x11102 },
	{ 0x11127, 0x1112b },
	{ 0x1112d, 0x11134 },
	{ 0x11173, 0x11173 },
	{ 0x11180, 0x11181 },
	{ 0x111b6, 0x111be },
	{ 0x111c9, 0x111cc },
	{ 0x1122f, 0x11231 },
	{ 0x11234, 0x11234 },
	{ 0x11236, 0x11237 },
	{ 0x1123e, 0x1123e },
	{ 0x112df, 0x112df },
	{ 0x112e3, 0x112ea },
	{ 0x11300, 0x11301 },
	{ 0x1133b, 0x1133c },
	{ 0x11340, 0x11340 },
	{ 0x11366, 0x1136c },
	{ 0x11370, 0x11374 },
	{ 0x11438, 0x1143f },
	{ 0x11442, 0x11444 },
	{ 0x11446, 0x11446 },
	{ 0x1145e, 0x1145e },
	{ 0x114b3, 0x114b8 },
	{ 0x114ba, 0x114ba },
	{ 0x114bf, 0x114c0 },
	{ 0x114c2, 0x114c3 },
	{ 0x115b2, 0x115b5 },
	{ 0x115bc, 0x115bd },
	{ 0x115bf, 0x115c0 },
	{ 0x115dc, 0x115dd },
	{ 0x11633, 0x1163a },
	{ 0x1163d, 0x1163d },
	{ 0x1163f, 0x11640 },
	{ 0x116ab, 0x116ab },
	{ 0x116ad, 0x116ad },
	{ 0x116b0, 0x116b5 },
	{ 0x116b7, 0x116b7 },
	{ 0x1171d, 0x1171f },
	{ 0x11722, 0x11725 },
	{ 0x11727, 0x1172b },
	{ 0x1182f, 0x11837 },
	{ 0x11839, 0x1183a },
	{ 0x1193b, 0x1193c },
	{ 0x1193e, 0x1193e },
	{ 0x11943, 0x11943 },
	{ 0x119d4, 0x119d7 },
	{ 0x119da, 0x119db },
	{ 0x119e0, 0x119e0 },
	{ 0x11a01, 0x11a0a },
	{ 0x11a33, 0x11a38 },
	{ 0x11a3b, 0x11a3e },
	{ 0x11a47, 0x11a47 },
	{ 0x11a51, 0x11a56 },
	{ 0x11a59, 0x11a5b },
	{ 0x11a8a, 0x11a96 },
	{ 0x11a98, 0x11a99 },
	{ 0x11c30, 0x11c36 },
	{ 0x11c38, 0x11c3d },
	{ 0x11c3f, 0x11c3f },
	{ 0x11c92, 0x11ca7 },
	{ 0x11caa, 0x11cb0 },
	{ 0x11cb2, 0x11cb3 },
	{ 0x11cb5, 0x11cb6 },
	{ 0x11d31, 0x11d36 },
	{ 0x11d3a, 0x11d3a },
	{ 0x11d3c, 0x11d3d },
	{ 0x11d3f, 0x11d45 },
	{ 0x11d47, 0x11d47 },
	{ 0x11d90, 0x11d91 },
	{ 0x11d95, 0x11d95 },
	{ 0x11d97, 0x11d97 },
	{ 0x11ef3, 0x11ef4 },
	{ 0x16af0, 0x16af4 },
	{ 0x16b30, 0x16b36 },
	{ 0x16f4f, 0x16f4f },
	{ 0x16f8f, 0x16f92 },
	{ 0x16fe4, 0x16fe4 },
	{ 0x1bc9d, 0x1bc9e },
	{ 0x1bca0, 0x1bca3 },
	{ 0x1cf00, 0x1cf2d },
	{ 0x1cf30, 0x1cf46 },
	{ 0x1d167, 0x1d169 },
	{ 0x1d173, 0x1d182 },
	{ 0x1d185, 0x1d18b },
	{ 0x1d1aa, 0x1d1ad },
	{ 0x1d242, 0x1d244 },
	{ 0x1da00, 0x1da36 },
	{ 0x1da3b, 0x1da6c },
	{ 0x1da75, 0x1da75 },
	{ 0x1da84, 0x1da84 },
	{ 0x1da9b, 0x1da9f },
	{ 0x1daa1, 0x1daaf },
	{ 0x1e000, 0x1e006 },
	{ 0x1e008, 0x1e018 },
	{ 0x1e01b, 0x1e021 },
	{ 0x1e023, 0x1e024 },
	{ 0x1e026, 0x1e02a },
	{ 0x1e08f, 0x1e08f },
	{ 0x1e130, 0x1e136 },
	{ 0x1e2ae, 0x1e2ae },
	{ 0x1e2ec, 0x1e2ef },
	{ 0x1e4ec, 0x1e4ef },
	{ 0x1e8d0, 0x1e8d6 },
	{ 0x1e944, 0x1e94a },
	{ 0xe0100, 0xe01ef }
};

static const struct utf8_width_range utf8_win32_wide[] = {
	{ 0x1100, 0x115f },
	{ 0x231a, 0x231b },
	{ 0x2329, 0x232a },
	{ 0x23e9, 0x23ec },
	{ 0x23f0, 0x23f0 },
	{ 0x23f3, 0x23f3 },
	{ 0x25fd, 0x25fe },
	{ 0x2614, 0x2615 },
	{ 0x2648, 0x2653 },
	{ 0x267f, 0x267f },
	{ 0x2693, 0x2693 },
	{ 0x26a1, 0x26a1 },
	{ 0x26aa, 0x26ab },
	{ 0x26bd, 0x26be },
	{ 0x26c4, 0x26c5 },
	{ 0x26ce, 0x26ce },
	{ 0x26d4, 0x26d4 },
	{ 0x26ea, 0x26ea },
	{ 0x26f2, 0x26f3 },
	{ 0x26f5, 0x26f5 },
	{ 0x26fa, 0x26fa },
	{ 0x26fd, 0x26fd },
	{ 0x2705, 0x2705 },
	{ 0x270a, 0x270b },
	{ 0x2728, 0x2728 },
	{ 0x274c, 0x274c },
	{ 0x274e, 0x274e },
	{ 0x2753, 0x2755 },
	{ 0x2757, 0x2757 },
	{ 0x2795, 0x2797 },
	{ 0x27b0, 0x27b0 },
	{ 0x27bf, 0x27bf },
	{ 0x2b1b, 0x2b1c },
	{ 0x2b50, 0x2b50 },
	{ 0x2b55, 0x2b55 },
	{ 0x2e80, 0x303e },
	{ 0x3040, 0xa4cf },
	{ 0xac00, 0xd7a3 },
	{ 0xf900, 0xfaff },
	{ 0xfe10, 0xfe19 },
	{ 0xfe30, 0xfe6f },
	{ 0xff00, 0xff60 },
	{ 0xffe0, 0xffe6 },
	{ 0x16fe0, 0x16fe4 },
	{ 0x17000, 0x187f7 },
	{ 0x18800, 0x18cd5 },
	{ 0x18d00, 0x18d08 },
	{ 0x1f004, 0x1f004 },
	{ 0x1f0cf, 0x1f0cf },
	{ 0x1f18e, 0x1f18e },
	{ 0x1f191, 0x1f19a },
	{ 0x1f200, 0x1f320 },
	{ 0x1f32d, 0x1f335 },
	{ 0x1f337, 0x1f37c },
	{ 0x1f37e, 0x1f393 },
	{ 0x1f3a0, 0x1f3ca },
	{ 0x1f3cf, 0x1f3d3 },
	{ 0x1f3e0, 0x1f3f0 },
	{ 0x1f3f4, 0x1f3f4 },
	{ 0x1f3f8, 0x1f43e },
	{ 0x1f440, 0x1f440 },
	{ 0x1f442, 0x1f4fc },
	{ 0x1f4ff, 0x1f53d },
	{ 0x1f54b, 0x1f54e },
	{ 0x1f550, 0x1f567 },
	{ 0x1f57a, 0x1f57a },
	{ 0x1f595, 0x1f596 },
	{ 0x1f5a4, 0x1f5a4 },
	{ 0x1f5fb, 0x1f64f },
	{ 0x1f680, 0x1f6c5 },
	{ 0x1f6cc, 0x1f6cc },
	{ 0x1f6d0, 0x1f6d2 },
	{ 0x1f6d5, 0x1f6d7 },
	{ 0x1f6dc, 0x1f6df },
	{ 0x1f6eb, 0x1f6ec },
	{ 0x1f6f4, 0x1f6fc },
	{ 0x1f7e0, 0x1f7eb },
	{ 0x1f7f0, 0x1f7f0 },
	{ 0x1f90c, 0x1f93a },
	{ 0x1f93c, 0x1f945 },
	{ 0x1f947, 0x1fa7c },
	{ 0x1fa80, 0x1fa89 },
	{ 0x1fa8f, 0x1fac6 },
	{ 0x1face, 0x1fadc },
	{ 0x1fadf, 0x1fae9 },
	{ 0x1faf0, 0x1faf8 },
	{ 0x20000, 0x2fffd },
	{ 0x30000, 0x3fffd }
};

static int
utf8_win32_in_ranges(utf8_wchar wc, const struct utf8_width_range *ranges,
    u_int nranges)
{
	u_int	left = 0, right = nranges;
	u_int	mid;

	while (left < right) {
		mid = left + (right - left) / 2;
		if (wc < ranges[mid].first)
			right = mid;
		else if (wc > ranges[mid].last)
			left = mid + 1;
		else
			return (1);
	}
	return (0);
}

static int
utf8_win32_wcwidth(utf8_wchar wc)
{
	if (wc == 0)
		return (0);
	if (wc < 0x20 || (wc >= 0x7f && wc < 0xa0))
		return (-1);
	if (utf8_win32_in_ranges(wc, utf8_win32_zero_width,
	    nitems(utf8_win32_zero_width)))
		return (0);
	if (utf8_win32_in_ranges(wc, utf8_win32_wide,
	    nitems(utf8_win32_wide)))
		return (2);
	return (1);
}
#endif

#define UTF8_GET_SIZE(uc) (((uc) >> 24) & 0x1f)
#define UTF8_GET_WIDTH(uc) (((uc) >> 29) - 1)

#define UTF8_SET_SIZE(size) (((utf8_char)(size)) << 24)
#define UTF8_SET_WIDTH(width) ((((utf8_char)(width)) + 1) << 29)

/* Get a UTF-8 item from data. */
static struct utf8_item *
utf8_item_by_data(const u_char *data, size_t size)
{
	struct utf8_item	ui;

	memcpy(ui.data, data, size);
	ui.size = size;

	return (RB_FIND(utf8_data_tree, &utf8_data_tree, &ui));
}

/* Get a UTF-8 item from data. */
static struct utf8_item *
utf8_item_by_index(u_int index)
{
	struct utf8_item	ui;

	ui.index = index;

	return (RB_FIND(utf8_index_tree, &utf8_index_tree, &ui));
}

/* Find a codepoint in the cache. */
static struct utf8_width_item *
utf8_find_in_width_cache(utf8_wchar wc)
{
	struct utf8_width_item	uw;

	uw.wc = wc;
	return RB_FIND(utf8_width_cache, &utf8_width_cache, &uw);
}

/* Add to width cache. */
static void
utf8_insert_width_cache(utf8_wchar wc, u_int width)
{
	struct utf8_width_item	*uw, *old;

	log_debug("Unicode width cache: %08X=%u", (u_int)wc, width);

	uw = xcalloc(1, sizeof *uw);
	uw->wc = wc;
	uw->width = width;
	uw->allocated = 1;

	old = RB_INSERT(utf8_width_cache, &utf8_width_cache, uw);
	if (old != NULL) {
		RB_REMOVE(utf8_width_cache, &utf8_width_cache, old);
		if (old->allocated)
			free(old);
		RB_INSERT(utf8_width_cache, &utf8_width_cache, uw);
	}
}

/* Parse a single codepoint option. */
static void
utf8_add_to_width_cache(const char *s)
{
	char			*copy, *cp, *endptr;
	u_int			 width;
	const char		*errstr;
	struct utf8_data	*ud;
	utf8_wchar		 wc, wc_start, wc_end;
	unsigned long long	 n;

	copy = xstrdup(s);
	if ((cp = strchr(copy, '=')) == NULL) {
		free(copy);
		return;
	}
	*cp++ = '\0';

	width = strtonum(cp, 0, 2, &errstr);
	if (errstr != NULL) {
		free(copy);
		return;
	}

	if (strncmp(copy, "U+", 2) == 0) {
		errno = 0;
		n = strtoull(copy + 2, &endptr, 16);
		if (copy[2] == '\0' ||
		    n == 0 ||
		    n > 0x10ffff ||
		    (errno == ERANGE && n == ULLONG_MAX)) {
			free(copy);
			return;
		}
		wc_start = n;
		if (*endptr == '-') {
			endptr++;
			if (strncmp(endptr, "U+", 2) != 0) {
				free(copy);
				return;
			}
			errno = 0;
			n = strtoull(endptr + 2, &endptr, 16);
			if (*endptr != '\0' ||
			    n == 0 ||
			    n > 0x10ffff ||
			    (errno == ERANGE && n == ULLONG_MAX) ||
			    (utf8_wchar)n < wc_start) {
				free(copy);
				return;
			}
			wc_end = n;
		} else {
			if (*endptr != '\0') {
				free(copy);
				return;
			}
			wc_end = wc_start;
		}

		for (wc = wc_start; wc <= wc_end; wc++)
			utf8_insert_width_cache(wc, width);
	} else {
		utf8_no_width = 1;
		ud = utf8_fromcstr(copy);
		utf8_no_width = 0;
		if (ud[0].size == 0 || ud[1].size != 0) {
			free(ud);
			free(copy);
			return;
		}
		if (utf8_towc(&ud[0], &wc) != UTF8_DONE) {
			free(ud);
			free(copy);
			return;
		}
		free(ud);

		utf8_insert_width_cache(wc, width);
	}

	free(copy);
}

/* Rebuild cache of widths. */
void
utf8_update_width_cache(void)
{
	struct utf8_width_item		*uw, *uw1;
	struct options_entry		*o;
	struct options_array_item	*a;
	u_int				 i;

	RB_FOREACH_SAFE (uw, utf8_width_cache, &utf8_width_cache, uw1) {
		RB_REMOVE(utf8_width_cache, &utf8_width_cache, uw);
		if (uw->allocated)
			free(uw);
	}

	for (i = 0; i < nitems(utf8_default_width_cache); i++) {
		RB_INSERT(utf8_width_cache, &utf8_width_cache,
		    &utf8_default_width_cache[i]);
	}

	o = options_get(global_options, "codepoint-widths");
	a = options_array_first(o);
	while (a != NULL) {
		utf8_add_to_width_cache(options_array_item_value(a)->string);
		a = options_array_next(a);
	}
}

/* Add a UTF-8 item. */
static int
utf8_put_item(const u_char *data, size_t size, u_int *index)
{
	struct utf8_item	*ui;

	ui = utf8_item_by_data(data, size);
	if (ui != NULL) {
		*index = ui->index;
		log_debug("%s: found %.*s = %u", __func__, (int)size, data,
		    *index);
		return (0);
	}

	if (utf8_next_index == 0xffffff + 1)
		return (-1);

	ui = xcalloc(1, sizeof *ui);
	ui->index = utf8_next_index++;
	RB_INSERT(utf8_index_tree, &utf8_index_tree, ui);

	memcpy(ui->data, data, size);
	ui->size = size;
	RB_INSERT(utf8_data_tree, &utf8_data_tree, ui);

	*index = ui->index;
	log_debug("%s: added %.*s = %u", __func__, (int)size, data, *index);
	return (0);
}

/* Get UTF-8 character from data. */
enum utf8_state
utf8_from_data(const struct utf8_data *ud, utf8_char *uc)
{
	u_int	index;

	if (ud->width > 2)
		fatalx("invalid UTF-8 width: %u", ud->width);

	if (ud->size > UTF8_SIZE)
		goto fail;
	if (ud->size <= 3) {
		index = (((utf8_char)ud->data[2] << 16)|
			  ((utf8_char)ud->data[1] << 8)|
			  ((utf8_char)ud->data[0]));
	} else if (utf8_put_item(ud->data, ud->size, &index) != 0)
		goto fail;
	*uc = UTF8_SET_SIZE(ud->size)|UTF8_SET_WIDTH(ud->width)|index;
	log_debug("%s: (%d %d %.*s) -> %08x", __func__, ud->width, ud->size,
	    (int)ud->size, ud->data, *uc);
	return (UTF8_DONE);

fail:
	if (ud->width == 0)
		*uc = UTF8_SET_SIZE(0)|UTF8_SET_WIDTH(0);
	else if (ud->width == 1)
		*uc = UTF8_SET_SIZE(1)|UTF8_SET_WIDTH(1)|0x20;
	else
		*uc = UTF8_SET_SIZE(1)|UTF8_SET_WIDTH(1)|0x2020;
	return (UTF8_ERROR);
}

/* Get UTF-8 data from character. */
void
utf8_to_data(utf8_char uc, struct utf8_data *ud)
{
	struct utf8_item	*ui;
	u_int			 index;

	memset(ud, 0, sizeof *ud);
	ud->size = ud->have = UTF8_GET_SIZE(uc);
	ud->width = UTF8_GET_WIDTH(uc);

	if (ud->size <= 3) {
		ud->data[2] = (uc >> 16);
		ud->data[1] = ((uc >> 8) & 0xff);
		ud->data[0] = (uc & 0xff);
	} else {
		index = (uc & 0xffffff);
		if ((ui = utf8_item_by_index(index)) == NULL)
			memset(ud->data, ' ', ud->size);
		else
			memcpy(ud->data, ui->data, ud->size);
	}

	log_debug("%s: %08x -> (%d %d %.*s)", __func__, uc, ud->width, ud->size,
	    (int)ud->size, ud->data);
}

/* Get UTF-8 character from a single ASCII character. */
u_int
utf8_build_one(u_char ch)
{
	return (UTF8_SET_SIZE(1)|UTF8_SET_WIDTH(1)|ch);
}

/* Set a single character. */
void
utf8_set(struct utf8_data *ud, u_char ch)
{
	static const struct utf8_data empty = { { 0 }, 1, 1, 1 };

	memcpy(ud, &empty, sizeof *ud);
	*ud->data = ch;
}

/* Copy UTF-8 character. */
void
utf8_copy(struct utf8_data *to, const struct utf8_data *from)
{
	u_int	i;

	memcpy(to, from, sizeof *to);

	for (i = to->size; i < sizeof to->data; i++)
		to->data[i] = '\0';
}

/* Get width of Unicode character. */
static enum utf8_state
utf8_width(struct utf8_data *ud, int *width)
{
	struct utf8_width_item	*uw;
	utf8_wchar		 wc;

	if (utf8_towc(ud, &wc) != UTF8_DONE)
		return (UTF8_ERROR);
	uw = utf8_find_in_width_cache(wc);
	if (uw != NULL) {
		*width = uw->width;
		log_debug("cached width for %08X is %d", (u_int)wc, *width);
		return (UTF8_DONE);
	}
#ifdef HAVE_UTF8PROC
	*width = utf8proc_wcwidth(wc);
	log_debug("utf8proc_wcwidth(%05X) returned %d", (u_int)wc, *width);
#else
#ifdef _WIN32
	*width = utf8_win32_wcwidth(wc);
#else
	*width = wcwidth(wc);
#endif
	log_debug("wcwidth(%05X) returned %d", (u_int)wc, *width);
	if (*width < 0) {
		/*
		 * C1 control characters are nonprintable, so they are always
		 * zero width.
		 */
		*width = (wc >= 0x80 && wc <= 0x9f) ? 0 : 1;
	}
#endif
	if (*width >= 0 && *width <= 0xff)
		return (UTF8_DONE);
	return (UTF8_ERROR);
}

/* Convert UTF-8 character to wide character. */
enum utf8_state
utf8_towc(const struct utf8_data *ud, utf8_wchar *wc)
{
#ifdef _WIN32
	const u_char	*s = ud->data;

	switch (ud->size) {
	case 1:
		if (s[0] > 0x7f)
			return (UTF8_ERROR);
		*wc = s[0];
		break;
	case 2:
		if ((s[1] & 0xc0) != 0x80)
			return (UTF8_ERROR);
		*wc = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
		if (*wc < 0x80)
			return (UTF8_ERROR);
		break;
	case 3:
		if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80)
			return (UTF8_ERROR);
		*wc = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) |
		    (s[2] & 0x3f);
		if (*wc < 0x800 || (*wc >= 0xd800 && *wc <= 0xdfff))
			return (UTF8_ERROR);
		break;
	case 4:
		if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 ||
		    (s[3] & 0xc0) != 0x80)
			return (UTF8_ERROR);
		*wc = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) |
		    ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
		if (*wc < 0x10000 || *wc > 0x10ffff)
			return (UTF8_ERROR);
		break;
	default:
		return (UTF8_ERROR);
	}
#else
#ifdef HAVE_UTF8PROC
	switch (utf8proc_mbtowc(wc, ud->data, ud->size)) {
#else
	switch (mbtowc(wc, ud->data, ud->size)) {
#endif
	case -1:
		log_debug("UTF-8 %.*s, mbtowc() %d", (int)ud->size, ud->data,
		    errno);
		mbtowc(NULL, NULL, MB_CUR_MAX);
		return (UTF8_ERROR);
	case 0:
		return (UTF8_ERROR);
	}
#endif
	log_debug("UTF-8 %.*s is U+%06X", (int)ud->size, ud->data, (u_int)*wc);
	return (UTF8_DONE);
}

/* Convert wide character to UTF-8 character. */
enum utf8_state
utf8_fromwc(utf8_wchar wc, struct utf8_data *ud)
{
	int	size, width;

#ifdef _WIN32
	if (wc > 0x10ffff || (wc >= 0xd800 && wc <= 0xdfff)) {
		log_debug("UTF-8 invalid codepoint U+%06X", (u_int)wc);
		return (UTF8_ERROR);
	}
	if (wc <= 0x7f) {
		ud->data[0] = wc;
		size = 1;
	} else if (wc <= 0x7ff) {
		ud->data[0] = 0xc0 | (wc >> 6);
		ud->data[1] = 0x80 | (wc & 0x3f);
		size = 2;
	} else if (wc <= 0xffff) {
		ud->data[0] = 0xe0 | (wc >> 12);
		ud->data[1] = 0x80 | ((wc >> 6) & 0x3f);
		ud->data[2] = 0x80 | (wc & 0x3f);
		size = 3;
	} else {
		ud->data[0] = 0xf0 | (wc >> 18);
		ud->data[1] = 0x80 | ((wc >> 12) & 0x3f);
		ud->data[2] = 0x80 | ((wc >> 6) & 0x3f);
		ud->data[3] = 0x80 | (wc & 0x3f);
		size = 4;
	}
#else
#ifdef HAVE_UTF8PROC
	size = utf8proc_wctomb(ud->data, wc);
#else
	size = wctomb(ud->data, wc);
#endif
	if (size < 0) {
		log_debug("UTF-8 %d, wctomb() %d", wc, errno);
		wctomb(NULL, 0);
		return (UTF8_ERROR);
	}
	if (size == 0)
		return (UTF8_ERROR);
#endif
	ud->size = ud->have = size;
	if (utf8_width(ud, &width) == UTF8_DONE) {
		ud->width = width;
		return (UTF8_DONE);
	}
	return (UTF8_ERROR);
}

/*
 * Open UTF-8 sequence.
 *
 * 11000010-11011111 C2-DF start of 2-byte sequence
 * 11100000-11101111 E0-EF start of 3-byte sequence
 * 11110000-11110100 F0-F4 start of 4-byte sequence
 */
enum utf8_state
utf8_open(struct utf8_data *ud, u_char ch)
{
	memset(ud, 0, sizeof *ud);
	if (ch >= 0xc2 && ch <= 0xdf)
		ud->size = 2;
	else if (ch >= 0xe0 && ch <= 0xef)
		ud->size = 3;
	else if (ch >= 0xf0 && ch <= 0xf4)
		ud->size = 4;
	else
		return (UTF8_ERROR);
	utf8_append(ud, ch);
	return (UTF8_MORE);
}

/* Append character to UTF-8, closing if finished. */
enum utf8_state
utf8_append(struct utf8_data *ud, u_char ch)
{
	int	width;

	if (ud->have >= ud->size)
		fatalx("UTF-8 character overflow");
	if (ud->size > sizeof ud->data)
		fatalx("UTF-8 character size too large");

	if (ud->have != 0 && (ch & 0xc0) != 0x80)
		ud->width = 0xff;

	ud->data[ud->have++] = ch;
	if (ud->have != ud->size)
		return (UTF8_MORE);

	if (!utf8_no_width) {
		if (ud->width == 0xff)
			return (UTF8_ERROR);
		if (utf8_width(ud, &width) != UTF8_DONE)
			return (UTF8_ERROR);
		ud->width = width;
	}

	return (UTF8_DONE);
}

/*
 * Encode len characters from src into dst, which is guaranteed to have four
 * bytes available for each character from src (for \abc or UTF-8) plus space
 * for \0.
 */
size_t
utf8_strvis(char *dst, const char *src, size_t len, int flag)
{
	struct utf8_data	 ud;
	const char		*start = dst, *end = src + len;
	enum utf8_state		 more;
	size_t			 i;

	while (src < end) {
		if ((more = utf8_open(&ud, *src)) == UTF8_MORE) {
			while (++src < end && more == UTF8_MORE)
				more = utf8_append(&ud, *src);
			if (more == UTF8_DONE) {
				/* UTF-8 character finished. */
				for (i = 0; i < ud.size; i++)
					*dst++ = ud.data[i];
				continue;
			}
			/* Not a complete, valid UTF-8 character. */
			src -= ud.have;
		}
		if ((flag & VIS_DQ) && src[0] == '$' && src < end - 1) {
			if (isalpha((u_char)src[1]) ||
			    src[1] == '_' ||
			    src[1] == '{')
				*dst++ = '\\';
			*dst++ = '$';
		} else if (src < end - 1)
			dst = vis(dst, src[0], flag, src[1]);
		else if (src < end)
			dst = vis(dst, src[0], flag, '\0');
		src++;
	}
	*dst = '\0';
	return (dst - start);
}

/* Same as utf8_strvis but allocate the buffer. */
size_t
utf8_stravis(char **dst, const char *src, int flag)
{
	char	*buf;
	size_t	 len;

	buf = xreallocarray(NULL, 4, strlen(src) + 1);
	len = utf8_strvis(buf, src, strlen(src), flag);

	*dst = xrealloc(buf, len + 1);
	return (len);
}

/* Same as utf8_strvis but allocate the buffer. */
size_t
utf8_stravisx(char **dst, const char *src, size_t srclen, int flag)
{
	char	*buf;
	size_t	 len;

	buf = xreallocarray(NULL, 4, srclen + 1);
	len = utf8_strvis(buf, src, srclen, flag);

	*dst = xrealloc(buf, len + 1);
	return (len);
}

/* Does this string contain anything that isn't valid UTF-8? */
int
utf8_isvalid(const char *s)
{
	struct utf8_data ud;
	const char	*end;
	enum utf8_state	 more;

	end = s + strlen(s);
	while (s < end) {
		if ((more = utf8_open(&ud, *s)) == UTF8_MORE) {
			while (++s < end && more == UTF8_MORE)
				more = utf8_append(&ud, *s);
			if (more == UTF8_DONE)
				continue;
			return (0);
		}
		if (*s < 0x20 || *s > 0x7e)
			return (0);
		s++;
	}
	return (1);
}

/*
 * Sanitize a string, changing any UTF-8 characters to '_'. Caller should free
 * the returned string. Anything not valid printable ASCII or UTF-8 is
 * stripped.
 */
char *
utf8_sanitize(const char *src)
{
	char		*dst = NULL;
	size_t		 n = 0;
	enum utf8_state	 more;
	struct utf8_data ud;
	u_int		 i;

	while (*src != '\0') {
		dst = xreallocarray(dst, n + 1, sizeof *dst);
		if ((more = utf8_open(&ud, *src)) == UTF8_MORE) {
			while (*++src != '\0' && more == UTF8_MORE)
				more = utf8_append(&ud, *src);
			if (more == UTF8_DONE) {
				dst = xreallocarray(dst, n + ud.width,
				    sizeof *dst);
				for (i = 0; i < ud.width; i++)
					dst[n++] = '_';
				continue;
			}
			src -= ud.have;
		}
		if (*src > 0x1f && *src < 0x7f)
			dst[n++] = *src;
		else
			dst[n++] = '_';
		src++;
	}
	dst = xreallocarray(dst, n + 1, sizeof *dst);
	dst[n] = '\0';
	return (dst);
}

/* Get UTF-8 buffer length. */
size_t
utf8_strlen(const struct utf8_data *s)
{
	size_t	i;

	for (i = 0; s[i].size != 0; i++)
		/* nothing */;
	return (i);
}

/* Get UTF-8 string width. */
u_int
utf8_strwidth(const struct utf8_data *s, ssize_t n)
{
	ssize_t	i;
	u_int	width = 0;

	for (i = 0; s[i].size != 0; i++) {
		if (n != -1 && n == i)
			break;
		width += s[i].width;
	}
	return (width);
}

/*
 * Convert a string into a buffer of UTF-8 characters. Terminated by size == 0.
 * Caller frees.
 */
struct utf8_data *
utf8_fromcstr(const char *src)
{
	struct utf8_data	*dst = NULL;
	size_t			 n = 0;
	enum utf8_state		 more;

	while (*src != '\0') {
		dst = xreallocarray(dst, n + 1, sizeof *dst);
		if ((more = utf8_open(&dst[n], *src)) == UTF8_MORE) {
			while (*++src != '\0' && more == UTF8_MORE)
				more = utf8_append(&dst[n], *src);
			if (more == UTF8_DONE) {
				n++;
				continue;
			}
			src -= dst[n].have;
		}
		utf8_set(&dst[n], *src);
		n++;
		src++;
	}
	dst = xreallocarray(dst, n + 1, sizeof *dst);
	dst[n].size = 0;
	return (dst);
}

/* Convert from a buffer of UTF-8 characters into a string. Caller frees. */
char *
utf8_tocstr(struct utf8_data *src)
{
	char	*dst = NULL;
	size_t	 n = 0;

	for(; src->size != 0; src++) {
		dst = xreallocarray(dst, n + src->size, 1);
		memcpy(dst + n, src->data, src->size);
		n += src->size;
	}
	dst = xreallocarray(dst, n + 1, 1);
	dst[n] = '\0';
	return (dst);
}

/* Get width of UTF-8 string. */
u_int
utf8_cstrwidth(const char *s)
{
	struct utf8_data	tmp;
	u_int			width;
	enum utf8_state		more;

	width = 0;
	while (*s != '\0') {
		if ((more = utf8_open(&tmp, *s)) == UTF8_MORE) {
			while (*++s != '\0' && more == UTF8_MORE)
				more = utf8_append(&tmp, *s);
			if (more == UTF8_DONE) {
				width += tmp.width;
				continue;
			}
			s -= tmp.have;
		}
		if (*s > 0x1f && *s != 0x7f)
			width++;
		s++;
	}
	return (width);
}

/* Pad UTF-8 string to width on the left. Caller frees. */
char *
utf8_padcstr(const char *s, u_int width)
{
	size_t	 slen;
	char	*out;
	u_int	 n, i;

	n = utf8_cstrwidth(s);
	if (n >= width)
		return (xstrdup(s));

	slen = strlen(s);
	out = xmalloc(slen + 1 + (width - n));
	memcpy(out, s, slen);
	for (i = n; i < width; i++)
		out[slen++] = ' ';
	out[slen] = '\0';
	return (out);
}

/* Pad UTF-8 string to width on the right. Caller frees. */
char *
utf8_rpadcstr(const char *s, u_int width)
{
	size_t	 slen;
	char	*out;
	u_int	 n, i;

	n = utf8_cstrwidth(s);
	if (n >= width)
		return (xstrdup(s));

	slen = strlen(s);
	out = xmalloc(slen + 1 + (width - n));
	for (i = 0; i < width - n; i++)
		out[i] = ' ';
	memcpy(out + i, s, slen);
	out[i + slen] = '\0';
	return (out);
}

int
utf8_cstrhas(const char *s, const struct utf8_data *ud)
{
	struct utf8_data	*copy, *loop;
	int			 found = 0;

	copy = utf8_fromcstr(s);
	for (loop = copy; loop->size != 0; loop++) {
		if (loop->size != ud->size)
			continue;
		if (memcmp(loop->data, ud->data, loop->size) == 0) {
			found = 1;
			break;
		}
	}
	free(copy);

	return (found);
}
