/*
 * ui_shim.h
 *
 *  Thin C layer over the app ABI. It gives the ported cartridge tools the
 *  firmware building blocks they were written against: a Window, a key loop
 *  that drives a poll function like UserInterface drives UIObject::poll,
 *  snprintf-style formatting, hex input, file helpers and create_user_file().
 */

#ifndef UI_SHIM_H
#define UI_SHIM_H

#include "u2p_app.h"
#include "u2p_ui.h"
#include <stdarg.h>
#include <stdbool.h>

// Set by app_main before anything else runs
extern const u2p_api_t *g_api;

// Debug output to the firmware console, like printf in the firmware tools
#define LOG g_api->sys->log

// Poll results, identical to the firmware UIObject codes
#define MENU_CLOSE -1
#define MENU_EXIT  -3

// FatFs result codes used by the tools, same values as the firmware
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR = 1,
    FR_DENIED = 7,
    FR_EXIST = 8,
    FR_NOT_ENOUGH_CORE = 17,
    FR_INVALID_PARAMETER = 19
} FRESULT;

// The ABI window handle, used like the firmware Window class
typedef struct Window Window;

// Borderless window at x=0, y=2, 40 x (screen height - 2), cleared
Window *window_open(void);
void window_close(Window *w);
void window_clear(Window *w);
void window_set_color(Window *w, int color);
void window_reverse_mode(Window *w, int on);
void window_move_cursor(Window *w, int x, int y);
void window_output(Window *w, const char *s);
void window_output_char(Window *w, char c);
int window_get_size_y(Window *w);

// Calls poll(obj, key) for every keymapped key until poll returns non-zero.
// Returns that value, or MENU_EXIT when the UI host is gone.
// Function keys arrive as KEY_F1..KEY_F8, whatever interface the machine presents.
typedef int (*ui_poll_fn)(void *obj, int key);
int ui_run(void *obj, ui_poll_fn poll);

// The physical function key behind a keymapped code, or the code unchanged
int ui_fkey_pressed(int c);

// Supports %d %u %x %X %s %c %%, a '0' flag, a width and a precision for %s
int ui_snprintf(char *buf, int size, const char *fmt, ...);
int ui_vsnprintf(char *buf, int size, const char *fmt, va_list ap);

// String box for up to max_digits hex digits; returns the value or -1
int ui_hex_input(const char *prompt, int max_digits);

// Files: path and name are joined with '/', like FileManager::fopen(path, name)
void *file_open(const char *path, const char *name, const char *mode);
int file_read(void *f, void *buf, int len);
int file_write(void *f, const void *buf, int len);
int file_seek(void *f, long offset);
void file_close(void *f);

// Asks for a file name (max 22 chars, buffer holds the default), fixes it,
// sets the extension and creates the file in path, asking before it
// overwrites an existing file. Same flow as the firmware create_user_file().
FRESULT create_user_file(const char *message, const char *ext, const char *path, void **f, char *buffer);
// The file name create_user_file() made of buffer (out: 32 bytes), to open the same file again later
void user_file_name(const char *base, const char *ext, char *out);

#endif
