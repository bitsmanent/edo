#define _XOPEN_SOURCE
#include <wchar.h>
#include <grapheme.h>

int utf8_len(char *buf, int len);
int utf8_decode(char *buf, int len, unsigned int *cp);
size_t utf8_len_compat(char *buf, int len);

size_t
utf8_len_compat(char *buf, int len) {
	int i = 0, step, next;
	uint_least32_t cp;

	step = utf8_decode(buf, len, &cp);
	i += step;

	if(cp == 0x200D) return i;
	while(i < len) {
		next = utf8_decode(buf + i, len - i, &cp);
		if(cp == 0x200D) break;
		if(wcwidth((wchar_t)cp) > 0) break;
		i += next;
	}
	return i;
}

int
utf8_len(char *buf, int len) {
	return grapheme_next_character_break_utf8(buf, len);
}

int
utf8_decode(char *buf, int len, unsigned int *cp) {
	return grapheme_decode_utf8(buf, len, (uint_least32_t *)cp);
}
