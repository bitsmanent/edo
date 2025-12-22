/* osaentuhaosenuhaoesnuthaoesnutha oesnthaoesuntha snethu asoenhu saoenhtuaoesn uthaoesunthaoesuntaoeh usaoneth asoenth aoesnth aoesnthaoseuthaoseuthaoesunthaoeusnh asoentuh */

/* 👩‍❤️‍💋‍👩 */

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ui.h"

typedef struct {
	char *buf;
	size_t cap;
	int len;
} Line;

typedef struct {
	Line **lines;
	char *file_name;
	int file_size;
	int lines_cap;
	size_t lines_tot;
	//int ref_count;
} Buffer;

typedef struct {
	Buffer *buf;
	int line_num;
	int col_num;
	int row_offset;
	int col_offset;
	int screen_rows;
	int screen_cols;
	//int pref_col;
} View;

/* variables */
int running = 1;
View *vcur;
UI *ui;

/* function declarations */
void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *p, size_t size);
void insert_data(char *dst, char *txt, int txtlen, int len);
void delete_char(char *dst, int count, int len);
void line_insert_text(Line *line, size_t index, char *txt, int len);
void line_delete_char(Line *line, int index, int count);
Line *line_create(char *content);
void line_destroy(Line *l);
void buffer_insert_line(Buffer *b, int index, Line *line);
void buffer_delete_line(Buffer *b, int index, int count);
int buffer_load_file(Buffer *b);
Line *buffer_get_line(Buffer *b, int index);
Buffer *buffer_create(char *fn);
void buffer_destroy(Buffer *b);
View *view_create(Buffer *b);
void view_destroy(View *v);
void view_cursor_fix(View *v);
void view_cursor_hfix(View *v);
void view_cursor_vfix(View *v);
void view_cursor_left(View *v);
void view_cursor_right(View *v);
void view_cursor_up(View *v);
void view_cursor_down(View *v);
int view_idx2col(View *v, Line *line, int idx);
void view_scroll_fix(View *v);
int measure_span(char *s, int len, int start_x);
int render(Cell *cells, char *buf, int buflen, int xoff, int cols);
char *cell_get_text(Cell *cell, char *pool_base);
void view_place_cursor(View *v);
void draw_view(View *v);
void textpool_ensure_cap(TextPool *pool, int len);
int textpool_insert(TextPool *pool, char *s, int len);

/* function implementations */
void
die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}
	exit(0);
}

void *
ecalloc(size_t nmemb, size_t size) {
	void *p;

	if(!(p = calloc(nmemb, size)))
		die("Cannot allocate memory.");
	return p;
}

void *
erealloc(void *p, size_t size) {
	if(!(p = realloc(p, size)))
		die("Cannot reallocate memory.");
	return p;
}

void
insert_data(char *dst, char *txt, int txtlen, int len) {
	memmove(dst + txtlen, dst, len);
	memcpy(dst, txt, txtlen);
}

void
delete_char(char *dst, int count, int len) {
	memmove(dst, dst + count, len);
}

void
line_insert_text(Line *line, size_t index, char *txt, int len) {
	size_t newlen = line->len + len;

	assert(index >= 0 && index <= line->len);
	if(newlen > line->cap) {
		line->cap = line->cap ? line->cap * 2 : 16;
		line->buf = erealloc(line->buf, line->cap);
	}
	insert_data(&line->buf[index], txt, len, line->len - index);
	line->len = newlen;
}

void
line_delete_char(Line *line, int index, int count) {
	delete_char(&line->buf[index],  count, line->len - index);
	line->len -= count;
}

Line *
line_create(char *content) {
	Line *l = ecalloc(1, sizeof(Line));

	if(content) {
		l->buf = strdup(content);
		l->len = strlen(l->buf);
		l->cap = l->len + 1;
	}
	return l;
}

void
line_destroy(Line *l) {
	if(l->buf)
		free(l->buf);
	free(l);
}

void
buffer_insert_line(Buffer *b, int index, Line *line) {
	size_t nb = (b->lines_tot - index) * sizeof(Line *);

	assert(index >= 0 && index <= b->lines_tot);
	if(b->lines_tot >= b->lines_cap) {
		b->lines_cap = b->lines_cap ? b->lines_cap * 2 : 64;
		b->lines = erealloc(b->lines, sizeof(Line *) * b->lines_cap);
	}
	if(nb)
		memmove(b->lines + index + 1, b->lines + index, nb);
	b->lines[index] = line;
	++b->lines_tot;
}

void
buffer_delete_line(Buffer *b, int index, int count) {
	if(index < 0 || index >= b->lines_tot) return;
	if(index + count > b->lines_tot) count = b->lines_tot - index;

	/* do not remove the only existing line (but clear it) */
	if(b->lines_tot == 1 && !index) {
		b->lines[0]->len = 0;
		return;
	}

	for (int i = 0; i < count; i++)
		line_destroy(b->lines[index + i]);

	int remaining = b->lines_tot - (index + count);
	if (remaining > 0)
		memmove(b->lines + index, b->lines + index + count, remaining * sizeof(Line *));
	b->lines_tot -= count;
}

int
buffer_load_file(Buffer *b) {
	Line *l;
	FILE *fp;
	char *buf = NULL;
	size_t cap;
	int len;

	if(!(fp = fopen(b->file_name, "r")))
		return -1;
	while((len = getline(&buf, &cap, fp)) != -1) {
		if(len && buf[len-1] == '\n') buf[len - 1] = 0;
		b->file_size += len;
		l = line_create(buf);
		buffer_insert_line(b, b->lines_tot, l);
	}
	fclose(fp);
	return 0;
}

Line *
buffer_get_line(Buffer *b, int index) {
	if(index < 0 || index >= b->lines_tot)
		return NULL;
	return b->lines[index];
}

Buffer *
buffer_create(char *fn) {
	Buffer *b = ecalloc(1, sizeof(Buffer));

	b->lines = NULL;
	b->lines_tot = 0;
	b->file_size = 0;
	if(fn) {
		if(!(b->file_name = strdup(fn)))
			return NULL;
		if(buffer_load_file(b))
			printf("%s: cannot load file\n", fn);
	}

	/* ensure we have at least a line */
	if(!b->lines_tot) buffer_insert_line(b, 0, line_create(NULL));

	return b;
}

void
buffer_destroy(Buffer *b) {
	int i;

	if(b->lines) {
		for(i = 0; i < b->lines_tot; i++)
			line_destroy(b->lines[i]);
		free(b->lines);
	}
	if(b->file_name)
		free(b->file_name);
	free(b);
}

View *
view_create(Buffer *b) {
	View *v = ecalloc(1, sizeof(View));

	v->line_num = 0;
	v->col_num = 0;
	v->row_offset = 0;
	v->col_offset = 0;
	v->buf = b;

	ui->get_window_size(&v->screen_rows, &v->screen_cols);
	return v;
}

void
view_destroy(View *v) {
	free(v);
}

void
view_cursor_fix(View *v) {
	/* Note: if called together then vfix must always be called first */
	view_cursor_vfix(v);
	view_cursor_hfix(v);
}

/* actual invariant for the cursor */
void
view_cursor_hfix(View *v) {
	Line *l = v->buf->lines[v->line_num];

	if (v->col_num < 0) v->col_num = 0;
	if (v->col_num > l->len) v->col_num = l->len;
}

void
view_cursor_vfix(View *v) {
	if (v->line_num >= v->buf->lines_tot)
		v->line_num = v->buf->lines_tot - 1;
	if (v->line_num < 0)
		v->line_num = 0;
}

void
view_cursor_left(View *v) {
	if(v->col_num)
		--v->col_num;
}

void
view_cursor_right(View *v) {
	Line *l = v->buf->lines[v->line_num];

	if(v->col_num < l->len)
		++v->col_num;
}

void
view_cursor_up(View *v) {
	if(v->line_num) {
		--v->line_num;
		view_cursor_hfix(v);
	}
}

void
view_cursor_down(View *v) {
	if(v->line_num < v->buf->lines_tot - 1) {
		++v->line_num;
		view_cursor_hfix(v);
	}
}

int
view_idx2col(View *v, Line *line, int idx) {
	(void)v;
	if(!line->len) return 0;
	return measure_span(line->buf, idx, 0);
}

void
view_scroll_fix(View *v) {
	/* vertical */
	if (v->line_num < v->row_offset)
		v->row_offset = v->line_num;
	if (v->line_num >= v->row_offset + v->screen_rows)
		v->row_offset = v->line_num - v->screen_rows + 1;

	/* horizontal */
	Line *l = buffer_get_line(v->buf, v->line_num);
	int vx = view_idx2col(v, l, v->col_num);

	if(vx < v->col_offset)
		v->col_offset = vx;
	if(vx >= v->col_offset + v->screen_cols)
		v->col_offset = vx - v->screen_cols + 1;
}

int
measure_span(char *s, int slen, int start_x) {
	int x = start_x;
	int len, i;

	for(i = 0; i < slen; i += len) {
		len = 1; /* TODO: decode UTF8 */
		x += ui->text_width(s + i, len, x);
	}
	return x - start_x;
}

int
render(Cell *cells, char *buf, int buflen, int xoff, int cols) {
	int nc = 0, vx = 0, i = 0;
	int w, len, x;

	while(i < buflen) {
		len = 1; /* TODO: decode UTF8 */
		w = ui->text_width(buf + i, len, vx);

		if(vx + w <= xoff) goto next; /* horizontal scroll */

		x = vx - xoff;
		if(x >= cols) break; /* screen has been filled */
		if(x + w > cols) break; /* truncated character (TODO: draw a symbol?) */

		if(len > CELL_POOL_THRESHOLD) {
			/* TODO: manage pool */
			die("Arena pool to be implemented.\n");
		}
		else {
			memcpy(cells[nc].data.text, buf + i, len);
		}

		cells[nc].len = len;
		cells[nc].width = w;
		if(vx < xoff) cells[nc].width -= xoff - vx; /* partial rendering */

		++nc;
next:
		vx += w;
		i += len;
	}

	return nc;
}

/* TODO: is this the cleaner way to do it? */
void
view_place_cursor(View *v) {
	Line *l;
	int x, y;

	x = v->col_offset;
	l = buffer_get_line(v->buf, v->line_num);
	if(l) {
		x = view_idx2col(v, l, v->col_num);
		x -= v->col_offset;
		y = v->line_num - v->row_offset;
	} else {
		x = y = 0;
	}
	ui->move_cursor(x, y);
}

void
draw_view(View *v) {
	Line *l;
	int row, y, nc;

	ui->frame_start();
	view_scroll_fix(v);

	Cell *cells = ecalloc(1, sizeof(Cell) * v->screen_cols);

	for(y = 0; y < v->screen_rows; y++) {
		row = v->row_offset + y;
		l = buffer_get_line(v->buf, row);
		if(!l) {
			ui->draw_symbol(0, y, SYM_EMPTYLINE);
			continue;
		}
		nc = render(cells, l->buf, l->len, v->col_offset, v->screen_cols);
		ui->draw_line(ui, 0, y, cells, nc);
	}

	free(cells);

	view_place_cursor(v);
	ui->frame_flush();
}

void
textpool_ensure_cap(TextPool *pool, int len) {
	size_t newlen = pool->len + len;

	if(newlen <= pool->cap) return;
	pool->cap *= 2;
	if(pool->cap < newlen) pool->cap = newlen + 1024;
	pool->data = erealloc(pool->data, pool->cap);
}

int
textpool_insert(TextPool *pool, char *s, int len) {
	int olen = len;

	textpool_ensure_cap(pool, len);
	memcpy(pool->data + pool->len, s, len);
	pool->len += len;
	return olen;
}

char *
cell_get_text(Cell *c, char *pool_base) {
	if(c->len > CELL_POOL_THRESHOLD)
		return pool_base + c->data.pool_idx;
	return c->data.text;
}

void
run(void) {
	Event ev;

	while(running) {
		ev = ui->next_event();
		switch(ev.type) {
		case EV_KEY:
			if(ev.key == 'k') view_cursor_up(vcur);
			else if(ev.key == 'j') view_cursor_down(vcur);
			else if(ev.key == 'h') view_cursor_left(vcur);
			else if(ev.key == 'l') view_cursor_right(vcur);
			else if(ev.key == 'q') running = 0;
			else if(ev.key == 'D') {
				buffer_delete_line(vcur->buf, vcur->line_num, 1);
				view_cursor_fix(vcur);
			} else if(ev.key == 'K') {
				Line *l = line_create(NULL);
				buffer_insert_line(vcur->buf, vcur->line_num, l);

				/* we should call view_cursor_hfix() here since we're moving into
				 * another line (the new one). Since only col_num may be wrong we
				 * can avoid a function call by setting it manually. */
				vcur->col_num = 0;
			}
			else if(ev.key == 'J' || ev.key == '\n') {
				Line *l = line_create(NULL);
				buffer_insert_line(vcur->buf, vcur->line_num + 1, l);
				view_cursor_down(vcur);
			} else {
				/* TODO: view_insert_text()? */
				line_insert_text(vcur->buf->lines[vcur->line_num], vcur->col_num, (char *)&ev.key, 1);
				vcur->col_num += 1;
			}
			break;
		case EV_UKN:
			break;
		}
		draw_view(vcur);
	}
}

int
main(int argc, char *argv[]) {
	char *fn = NULL;

	if(argc > 2) die("Usage: %s [file]", argv[0]);
	if(argc == 2) fn = argv[1];

	ui = &ui_tui; /* the one and only... */
	atexit(ui->exit);
	ui->init();
	Buffer *b = buffer_create(fn);
	View *v = view_create(b);
	vcur = v; /* current view */
	draw_view(v);
	run();
	buffer_destroy(v->buf);
	view_destroy(v);
	ui->exit();
	free(ui->pool.data);
	return 0;
}
