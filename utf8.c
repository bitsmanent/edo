#define _XOPEN_SOURCE
#include <wchar.h>
#include <grapheme.h>
#include <utf8proc.h>

#include "utf8.h"

size_t
utf8_len_compat(char *buf, int len) {
	uint_least32_t cp;
	int step, i;

	i = step = utf8_decode(buf, len, &cp);
	if(!utf8_is_combining(cp) && wcwidth(cp) < 0) return step;
	while(i < len) {
		step = utf8_decode(buf + i, len - i, &cp);
		if(!utf8_is_combining(cp) || wcwidth(cp)) break;
		i += step;
	}
	return i;
}

int
utf8_len(char *buf, int len) {
	return grapheme_next_character_break_utf8(buf, len);
}

int
utf8_decode(char *buf, int len, unsigned int *cp) {
	return grapheme_decode_utf8(buf, len, cp);
}

int
utf8_is_combining(unsigned int cp) {
	const utf8proc_property_t *prop = utf8proc_get_property(cp);

	return (prop->category == UTF8PROC_CATEGORY_MN
			|| prop->category == UTF8PROC_CATEGORY_ME);
}
