#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdio.h>

typedef enum {
	SYM_EMPTYLINE
} Symbol;

typedef enum {
	EV_KEY,
	EV_UKN
} EventType;

typedef struct {
	EventType type;
	int key;
	int mod;
	int row;
	int col;
} Event;

typedef struct {
	char *data;
	size_t cap;
	size_t len;
} TextPool;

#define CELL_POOL_THRESHOLD 8
typedef struct {
	union {
		char text[CELL_POOL_THRESHOLD];
		uint32_t pool_idx;
	} data;
	/*
	 * For smarter backend we can also provide this fields:
	 *
	 * idx: index of the bytes into the file buffer
	 * type: char, tab, newline, eof, ecc.
	 *
	 * So the backend may say for example "Oh, it's a tab. Let's ignore the
	 * rendered cells with spaces and use an beatuful icon instead."
	 *
	 * We should also add the style here.
	*/
	uint16_t len;
	uint16_t width;
} Cell;

typedef struct UI UI;
struct UI {
	const char *name;
	TextPool pool;
	void (*init)(void);
	void (*exit)(void);
	void (*frame_start)(void);
	void (*frame_flush)(void);
	int (*text_width)(char *s, int len);
	int (*text_index_at)(char *s, int idx);
	void (*move_cursor)(int x, int y);
	void (*draw_line)(int r, int c, char *txt, int len);
	void (*draw_line_from_cells)(UI *ui, int x, int y, Cell *cells, int count);
	void (*draw_symbol)(int r, int c, Symbol sym);
	void (*get_window_size)(int *rows, int *cols);
	Event (*next_event)(void);
};

extern UI ui_tui;

#endif
