#ifndef UI_H
#define UI_H

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
	int col, row;
} Event;

typedef struct {
	const char *name;
	void (*init)(void);
	void (*exit)(void);
	void (*frame_start)(void);
	void (*frame_flush)(void);
	int (*text_width)(char *s, int len);
	void (*move_cursor)(int x, int y);
	void (*draw_text)(int r, int c, char *txt, int len);
	void (*draw_symbol)(int r, int c, Symbol sym);
	void (*get_window_size)(int *rows, int *cols);
	Event (*next_event)(void);
} UI;

extern UI ui_tui;

#endif
