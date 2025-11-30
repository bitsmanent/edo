#include "ui.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CURPOS          "\33[%d;%dH"
//#define CLEARLEFT       "\33[1K"
#define CLEARRIGHT      "\33[0K"
#define CURHIDE         "\33[?25l"
#define CURSHOW         "\33[?25h"

typedef struct {
	char *buf;
	int len;
	int cap;
} Abuf;

struct termios origti;
struct winsize ws;
Abuf frame;

extern void *ecalloc(size_t nmemb, size_t size);
extern void *erealloc(void *p, size_t size);
extern void die(const char *fmt, ...);

/* function declarations */
void ab_free(Abuf *ab);
void ab_ensure_cap(Abuf *ab, size_t addlen);
void ab_write(Abuf *ab, const char *s, size_t len);
int ab_printf(Abuf *ab, const char *fmt, ...);
void ab_flush(Abuf *ab);
void tui_frame_start(void);
void tui_frame_flush(void);
int tui_text_width(char *s, int len);
void tui_get_window_size(int *rows, int *cols);
void tui_exit(void);
void tui_move_cursor(int x, int y);
void tui_draw_line(int c, int r, char *txt, int len);
void tui_draw_symbol(int r, int c, Symbol sym);
void tui_init(void);

/* function implementations */
void
ab_free(Abuf *ab) {
	if(!ab->buf)
		return;
	free(ab->buf);
	ab->buf = NULL;
	ab->len = 0;
	ab->cap = 0;
}

void
ab_ensure_cap(Abuf *ab, size_t addlen) {
	size_t newlen = ab->len + addlen;

	if(newlen <= ab->cap)
		return;
	while(newlen > ab->cap)
		ab->cap = ab->cap ? ab->cap * 2 : 8;
	ab->buf = erealloc(ab->buf, ab->cap);
}

void
ab_write(Abuf *ab, const char *s, size_t len) {
	ab_ensure_cap(&frame, len);
	memcpy(ab->buf + ab->len, s, len);
	ab->len += len;
}

int
ab_printf(Abuf *ab, const char *fmt, ...) {
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	assert(len >= 0);
	va_end(ap);

	ab_ensure_cap(ab, len + 1);

	va_start(ap, fmt);
	vsnprintf(ab->buf + ab->len, len + 1, fmt, ap);
	va_end(ap);

	ab->len += len;
	return len;
}

void
ab_flush(Abuf *ab) {
	write(STDOUT_FILENO, ab->buf, ab->len);
	ab_free(ab);
}

void
tui_frame_start(void) {
	ab_printf(&frame, CURHIDE);
}

void
tui_frame_flush(void) {
	ab_printf(&frame, CURSHOW);
	ab_flush(&frame);
}

int
tui_text_width(char *s, int len) {
	int tabstop = 8;
	int w = 0, i;

	for(i = 0; i < len; i++) {
		if(s[i] == '\t')
			w += tabstop - w % tabstop;
		else
			++w;
	}
	return w;
}

int
tui_text_index_at(char *str, int target_x) {
	int tabstop = 8;
	int x = 0, i = 0, w;

	while(str[i]) {
		w = (str[i] == '\t') ? tabstop : 1;
		if (x + w > target_x)
			return i;
		x += w;
		i++;
	}
	return i;
}

void
tui_get_window_size(int *rows, int *cols) {
	*rows = ws.ws_row;
	*cols = ws.ws_col;
}

void
tui_exit(void) {
	tcsetattr(0, TCSANOW, &origti);
}

void
tui_move_cursor(int c, int r) {
	int x = c + 1, y = r + 1; /* TERM coords are 1-based */
	ab_printf(&frame, CURPOS, y, x);
}

void
tui_draw_line(int c, int r, char *txt, int len) {
	if(c >= ws.ws_col || r >= ws.ws_row) return;

	int x = c;
	int tabstop = 8;
	int i = 0;
	int cw, clen;

	tui_move_cursor(x < 0 ? 0 : x, r);
	while(i < len) {
		cw = (txt[i] == '\t' ? tabstop : 1);

		if(x + cw <= 0)
			continue;

		if(txt[i] == '\t') {
			int spaces = tabstop;
			clen = 1;

			if(x < 0) spaces += x;
			if(x + cw > ws.ws_col) spaces -= (x + cw - ws.ws_col);
			while(spaces--) ab_write(&frame, " ", 1);
		} else {
			if(x + cw > ws.ws_col) break;
			clen = 1;
			ab_write(&frame, txt + i, clen);
		}

		x += cw;
		i += clen;
	}
	ab_write(&frame, CLEARRIGHT, strlen(CLEARRIGHT));
}

void
tui_draw_symbol(int c, int r, Symbol sym) {
	int symch;

	switch(sym) {
	case SYM_EMPTYLINE: symch = '~'; break;
	default: symch = '?'; break;
	}

	tui_move_cursor(c, r);
	ab_printf(&frame, "%c" CLEARRIGHT, symch);
}

void
tui_init(void) {
	struct termios ti;

	setlocale(LC_CTYPE, "");
	tcgetattr(0, &origti);
	cfmakeraw(&ti);
	ti.c_iflag |= ICRNL;
	ti.c_cc[VMIN] = 1;
	ti.c_cc[VTIME] = 0;
	tcsetattr(0, TCSAFLUSH, &ti);
	setbuf(stdout, NULL);
	ioctl(0, TIOCGWINSZ, &ws);
}

int
tui_read_byte(void) {
	char c;

	if (read(STDIN_FILENO, &c, 1) == 1)
		return c;
	return -1;
}

Event
tui_next_event(void) {
	Event ev;
	int c = tui_read_byte();

	if(c == 0x1B) {
		ev.type = EV_UKN;
		return ev;
	}

	ev.type = EV_KEY;
	ev.key = c;
	return ev;
}

UI ui_tui = {
	.name = "TUI",
	.init = tui_init,
	.exit = tui_exit,
	.frame_start = tui_frame_start,
	.frame_flush = tui_frame_flush,
	.text_width = tui_text_width,
	.text_index_at = tui_text_index_at,
	.move_cursor = tui_move_cursor,
	.draw_line = tui_draw_line,
	.draw_symbol = tui_draw_symbol,
	.get_window_size = tui_get_window_size,
	.next_event = tui_next_event
};
