// The export "textgrid" (major 1): a text screen of a library, drawn by the generic UI (T239.3 section 5, boundary 12.2). A node of the core's UI TOML with the layout
// "terminal" and a layout parameter `grid = "<export name>"` names the export; the base finds it with SvcLib::find(name, 1, &size) and draws the grid the way build_terminal
// draws the monitor today. The sub-app trxmon draws the same grid through ABI 2. It is a table of its own, not a member of LibApi: a library puts it into LibRecord::exports
// (base 3.2) under any name the TOML uses ("textgrid" is the name of the monitor's).
//
// The grid: `cols` columns (80 for the monitor) and up to TG_ROWS_MAX rows of text, one attribute per row, a cursor cell. The screen is redrawn when `sig` changes; the base
// calls `render` first on each redraw and the other calls after it. Keys go through the operations that exist (monitor.key); there is no key call here.
//
// Rules: `size` first; within a major, members are only appended; removing or reordering one makes the next major. Every call takes no CoreCtx. Task: the UI task of the base (or the sub-app's
// task); a library keeps the calls short and never blocks in them. Strings are owned by the library and valid
// until the next `render`. Without a loaded library the base shows an empty screen.
#ifndef COREAPP_TEXTGRID_H
#define COREAPP_TEXTGRID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TG_ROWS_MAX 50                        // MON_ROWS_MAX of screens.cc: the base draws no more rows than this

// The attribute of a row (UA_* of trxmon's ui.h): the colour the base gives it.
enum {
    TG_NORMAL = 0,
    TG_STATUS = 1,
    TG_REGS = 2,
    TG_ECHO = 3,
    TG_ERR = 4,
    TG_PROMPT = 5,
    TG_BLANK = 6,                             // a row that is transparent on the overlay
    TG_SEP = 7,
    TG_HINT = 8
};

typedef struct TextGrid {
    uint32_t size;
    // Builds the rows as they are now (mon_render). Called once per redraw, before the other calls. With the session closed the grid has no rows (rows() is 0).
    void (*render)(void);
    // The columns of the grid (mon_cols; 80).
    int (*cols)(void);
    // The rows built by the last render (mon_rows), at most TG_ROWS_MAX.
    int (*rows)(void);
    // The text of row r, zero terminated, at most cols() characters; "" for r outside 0 .. rows() - 1. Never null.
    const char *(*line)(int r);
    // The attribute of row r, TG_*; TG_NORMAL for r outside the grid.
    int (*attr)(int r);
    // Rows 0 .. blank_rows() - 1 are colour index 0, transparent on the overlay (mon_blank_rows).
    int (*blank_rows)(void);
    // The cursor cell: column 0 .. cols() - 1 and row 0 .. rows() - 1.
    void (*cursor)(int *x, int *y);
    // A hash of what render built (mon_sig): equal sig, equal screen, no redraw.
    uint32_t (*sig)(void);
} TextGrid;

#ifdef __cplusplus
}
#endif

#endif
