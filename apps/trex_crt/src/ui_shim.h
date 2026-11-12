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

// Set by the application's task before anything else runs
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
    FR_INVALID_PARAMETER = 19,
    FR_CANCELLED = 99 // the person closed the screens, or asked to stop: not an error
} FRESULT;

// There is no window: the screens are a description the base draws (T104.3 D0). The tools still ask for one as they did; it is a handle that draws nothing,
// and what they would have drawn is published through cart_screen.c (the state values the description binds to).
typedef struct Window Window;
Window *window_open(void);
void window_close(Window *w);
void window_clear(Window *w);
void window_set_color(Window *w, int color);
void window_reverse_mode(Window *w, int on);
void window_move_cursor(Window *w, int x, int y);
void window_output(Window *w, const char *s);
void window_output_char(Window *w, char c);
int window_get_size_y(Window *w);

// Supports %d %u %x %X %s %c %%, a '0' flag, a width and a precision for %s
#define ui_snprintf u2p_snprintf
#define ui_vsnprintf u2p_vsnprintf

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
// The same without the questions about the name: `base` is the file name without the extension. Asks only whether to overwrite a file that is there.
FRESULT create_named_file(const char *path, const char *base, const char *ext, void **f);
// The file name create_user_file() made of buffer (out: 32 bytes), to open the same file again later
void user_file_name(const char *base, const char *ext, char *out);

#endif
