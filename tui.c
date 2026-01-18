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

#define ESC             "\x1b"
#define CURPOS          ESC"[%d;%dH"
#define CLEARRIGHT      ESC"[0K"
#define CURHIDE         ESC"[?25l"
#define CURSHOW         ESC"[?25h"
//#define CLEARLEFT       ESC"[1K"
//#define ERASECHAR       ESC"[1X"

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
	ab_write(&frame, CURHIDE, sizeof CURHIDE - 1);
}

void
tui_frame_flush(void) {
	ab_write(&frame, CURSHOW, sizeof CURSHOW - 1);
	ab_flush(&frame);
}

/* XXX move to utils? */
int
hexlen(unsigned int n) {
	int len = 0;

	do {
		len++;
		n >>= 4;
	} while(n > 0);
	return len;
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

		if(wc < 0) wc = wcwidth(cp);
		assert(wc != -1);

		if(wc > 0) w += wc;
		else if(compat_mode && !utf8_is_combining(cp)) w += hexlen(cp) + 2; /* 2 for < and > */
		//else w += hexlen(cp) + 2; /* 2 for < and > */
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
	int i;

	tui_move_cursor(x, y);
	for(i = 0; i < count; i++) {
		txt = cell_get_text(cells + i, ui->pool.data);

		unsigned int cp;
		int o = 0;
		int vw = 0;

		while(o < cells[i].len) {
			int step = utf8_decode(txt + o, cells[i].len - o, &cp);
			int cw = cells[i].width;
			int neederase = cells[i].len > 1 && (cells[i].width == 1 || IS_RIS(cp));

			switch(cp) {
			case '\t':
				for(int t = 0; t < cells[i].width; t++)
					ab_write(&frame, " ", 1);
				break;
			default:
				cw = wcwidth(cp);
				if(cw < 0) break;

				if(!cw) {
					if(!cells[i].width) break;

					char tag[16];
					snprintf(tag, sizeof tag, "<%0x>", cp);

					if(cells[i].flags & CELL_TRUNC_L) {
						cw = tui_text_width(txt + o, cells[i].len, 0);
						o = cw - cells[i].width;
						if(o < 0) o = 0;
					}
					{ const char t[] = ESC"[48;5;233m"; ab_write(&frame, t, sizeof t - 1); }
					cw = ab_printf(&frame, "%.*s", cells[i].width, tag + o);
					{ const char t[] = ESC"[0m"; ab_write(&frame, t, sizeof t - 1); }
					break;
				}

				if(cells[i].flags & CELL_TRUNC_L) {
					ab_write(&frame, "<", 1);
					cw = 1;
					break;
				}
				if(cells[i].flags & CELL_TRUNC_R) {
					ab_write(&frame, ">", 1);
					cw = 1;
					break;
				}

				if(neederase) {
					const char t[] = ESC"[48;5;232m";
					ab_write(&frame, t, sizeof t - 1);
				}

				ab_write(&frame, txt + o, step);

				/* to preserve coherence between terminals always split RIS
				 * so that we can see individual components. This is needed
				 * to ensure cursor synchronization between terminals that
				 * renders the same glyph with 2 different widths. */
				if(IS_RIS(cp)) ab_write(&frame, ZWNJ, sizeof ZWNJ - 1);

				if(neederase) {
					const char t[] = ESC"[0m";
					ab_write(&frame, t, sizeof t - 1);
				}
				break;
			}

			vw += cw;

			/* stop processing after truncation */
			if(cells[i].flags & (CELL_TRUNC_L | CELL_TRUNC_R)) break;

			/* no more visual characters expected for this cell */
			if(neederase) break;

			o += step;
		}

		/* pad clusters having unexpected width */
		if(vw < cells[i].width) {
			/* this is needed to handle inconsistences between terminals
			 * whose may use single cell or 2-cell cursors for the same
			 * exact character. */
			tui_move_cursor(x + vw, y);

			while(vw++ < cells[i].width) ab_write(&frame, " ", 1);
		}

		x += cells[i].width;
	}

	if(x < ws.ws_col) ab_write(&frame, CLEARRIGHT, strlen(CLEARRIGHT));
}

/* TODO: add support for cell flags */
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
	/*
	ti.c_iflag &= ~(BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	ti.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	ti.c_cflag &= ~(CSIZE|PARENB);
	ti.c_cflag |= CS8;
	*/
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
