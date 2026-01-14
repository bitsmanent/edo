#include <stdlib.h>

/* Regional Indicator Symbol */
#define IS_RIS(c) ((c) >= 0x1F1E6 && (c) <= 0x1F1FF)

/* Zero-Width Non-Joiner */
#define ZWNJ "\xe2\x80\x8c"

int utf8_len(char *buf, int len);
size_t utf8_len_compat(char *buf, int len);
int utf8_decode(char *buf, int len, unsigned int *cp);
