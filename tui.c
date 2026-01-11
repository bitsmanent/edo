#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include <wchar.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "utf8.h"
#include "ui.h"

#define CURPOS          "\33[%d;%dH"
//#define CLEARLEFT       "\33[1K"
#define CLEARRIGHT      "\33[0K"
#define CURHIDE         "\33[?25l"
#define CURSHOW         "\33[?25h"
#define ERASECHAR       "\33[1X"

/* UTF-8 Regional Indicator Symbol */
#define IS_RIS(c) ((c) >= 0x1F1E6 && (c) <= 0x1F1FF)

typedef struct {
	char *buf;
	int len;
	int cap;
} Abuf;

/* globals */
struct termios origti;
struct winsize ws;
Abuf frame;
int compat_mode;

/* TODO: edo.h? */
extern void *ecalloc(size_t nmemb, size_t size);
extern void *erealloc(void *p, size_t size);
extern void die(const char *fmt, ...);
extern char *cell_get_text(Cell *cell, char *pool_base);

/* function declarations */
void ab_free(Abuf *ab);
void ab_ensure_cap(Abuf *ab, size_t addlen);
void ab_write(Abuf *ab, const char *s, size_t len);
int ab_printf(Abuf *ab, const char *fmt, ...);
void ab_flush(Abuf *ab);
void tui_frame_start(void);
void tui_frame_flush(void);
int tui_text_width(char *s, int len, int x);
int tui_text_len(char *s, int len);
void tui_get_window_size(int *rows, int *cols);
void tui_exit(void);
void tui_move_cursor(int x, int y);
void tui_draw_line(UI *ui, int x, int y, Cell *cells, int screen_cols);
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
tui_text_width(char *s, int len, int x) {
	int tabstop = 8;
	int w = 0, i;
	int step, wc;
	unsigned int cp;

	for(i = 0; i < len; i += step) {
		step = utf8_decode(s + i, len - i, &cp);
		if(cp == '\t') {
			w += tabstop - x % tabstop;
			continue;
		}

		wc = -1;
		if(compat_mode) {
			if(cp == 0x200D) {
				w += 6;
				continue;
			}
			if(IS_RIS(cp)) wc = 2;
		} else {
			/* force 2 cells width for emoji followed by VS16 */
			int nxi = i + step;
			if(nxi < len) {
				unsigned int nxcp;
				utf8_decode(s + nxi, len - nxi, &nxcp);

				if(nxcp == 0xFE0F && ((cp >= 0x203C && cp <= 0x3299) || cp >= 0x1F000))
					wc = 2;
			}
		}

		if(wc == -1) wc = wcwidth(cp);
		if(wc > 0) w += wc;
	}
	return w;
}

int
tui_text_len(char *s, int len) {
	return compat_mode ? utf8_len_compat(s, len) : utf8_len(s, len);
}

void
tui_get_window_size(int *rows, int *cols) {
	*rows = ws.ws_row;
	*cols = ws.ws_col;
}

void
tui_exit(void) {
	tcsetattr(0, TCSANOW, &origti);
	printf(CURPOS CLEARRIGHT, ws.ws_row, 0);
}

void
tui_move_cursor(int c, int r) {
	int x = c + 1, y = r + 1; /* TERM coords are 1-based */
	ab_printf(&frame, CURPOS, y, x);
}

void
tui_draw_line_compat(UI *ui, int x, int y, Cell *cells, int count) {
	char *txt;
	int neederase, i;

	tui_move_cursor(x, y);
	for(i = 0; i < count; i++) {
		txt = cell_get_text(cells + i, ui->pool.data);

		unsigned int cp;
		int o = 0;

		neederase = 0;
		while(o < cells[i].len) {
			int step = utf8_decode(txt + o, cells[i].len - o, &cp);
			int w = wcwidth(cp);
			int cw = w > 0 ? w : cells[i].width;

			if(cells[i].len > 1 && (cells[i].width == 1 || IS_RIS(cp)))
				neederase = 1;

			switch(cp) {
			case 0x200D:
				ab_write(&frame, "<200d>", cells[i].width);
				break;
			case '\t':
				for(int t = 0; t < cells[i].width; t++)
					ab_write(&frame, " ", 1);
				break;
			default:
				if(neederase) {
					const char t[] = "\x1b[48;5;232m"ERASECHAR;
					ab_write(&frame, t, sizeof t - 1);
				}
				ab_write(&frame, txt + o, step);
				if(neederase) {
					const char t[] = "\x1b[0m";
					ab_write(&frame, t, sizeof t - 1);
				}
				break;
			}

			/* pad clusters having unexpected width */
			if(cw < cells[i].width)
				while(cw++ < cells[i].width) ab_write(&frame, " ", 1);

			/* no more visual characters expected for this cell */
			if(neederase) break;

			o += step;
		}

		x += cells[i].width;
	}
	ab_write(&frame, CLEARRIGHT, strlen(CLEARRIGHT));
}

void
tui_draw_line(UI *ui, int x, int y, Cell *cells, int count) {
	assert(x < ws.ws_col && y < ws.ws_row);

	if(compat_mode) {
		tui_draw_line_compat(ui, x, y, cells, count);
		return;
	}

	char *txt;
	int i;

	tui_move_cursor(x, y);
	for(i = 0; i < count; i++) {
		x += cells[i].width;
		txt = cell_get_text(cells + i, ui->pool.data);

		/* TODO: temp code for testing, we'll se how to deal with this later */
		if(txt[0] == '\t')
			ab_printf(&frame, "%*s", cells[i].width, " ");
		else
			ab_write(&frame, txt, cells[i].len);
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

	/* check for compat mode */
	/* TODO: auto-detect */
	compat_mode = 1;
}

int
tui_read_byte(void) {
	char c;

	if(read(STDIN_FILENO, &c, 1) == 1)
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
	.text_len = tui_text_len,
	.move_cursor = tui_move_cursor,
	.draw_line = tui_draw_line,
	.draw_symbol = tui_draw_symbol,
	.get_window_size = tui_get_window_size,
	.next_event = tui_next_event
};
