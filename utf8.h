#include <stdlib.h>

int utf8_len(char *buf, int len);
size_t utf8_len_compat(char *buf, int len);
int utf8_decode(char *buf, int len, unsigned int *cp);
