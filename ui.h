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

enum CellFlags {
	CELL_DEFAULT,
	CELL_TRUNC_L,
	CELL_TRUNC_R
};

#define CELL_POOL_THRESHOLD 8
typedef struct {
	union {
		char text[CELL_POOL_THRESHOLD];
		uint32_t pool_idx;
	} data;
	uint16_t len;
	uint16_t width;
	int flags;
} Cell;

typedef struct UI UI;
struct UI {
	const char *name;
	TextPool pool;
	void (*init)(void);
	void (*exit)(void);
	void (*frame_start)(void);
	void (*frame_flush)(void);
	int (*text_width)(char *s, int len, int x);
	int (*text_len)(char *s, int len);
	void (*move_cursor)(int x, int y);
	void (*draw_line)(UI *ui, int x, int y, Cell *cells, int count);
	void (*draw_symbol)(int r, int c, Symbol sym);
	void (*get_window_size)(int *rows, int *cols);
	Event (*next_event)(void);
};

extern UI ui_tui;

#endif
