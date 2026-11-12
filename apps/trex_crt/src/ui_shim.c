/*
 * ui_shim.c
 *
 *  Building blocks of the ported cartridge tools: files, names, hex input. The window calls are handles that draw nothing (ui_shim.h).
 *
 */

#include "ui_shim.h"

// =========================================================================
//  Window (a handle that draws nothing, see ui_shim.h)
// =========================================================================

static char s_window;
Window *window_open(void) { return (Window *)&s_window; }
void window_close(Window *w) { (void)w; }
void window_clear(Window *w) { (void)w; }
void window_set_color(Window *w, int color) { (void)w; (void)color; }
void window_reverse_mode(Window *w, int on) { (void)w; (void)on; }
void window_move_cursor(Window *w, int x, int y) { (void)w; (void)x; (void)y; }
void window_output(Window *w, const char *s) { (void)w; (void)s; }
void window_output_char(Window *w, char c) { (void)w; (void)c; }
int window_get_size_y(Window *w) { (void)w; return 22; }

// =========================================================================
//  Hex input (CartInspector::hex_input)
// =========================================================================

int ui_hex_input(const char *prompt, int max_digits)
{
    char buf[8];
    memset(buf, 0, sizeof(buf));

    int ret = u2p_ui_string_input(prompt, buf, max_digits);
    if (ret <= 0 || buf[0] == 0)
        return -1;

    int value = 0;
    for (int i = 0; buf[i]; i++) {
        char c = buf[i];
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return -1;
        value = (value << 4) | digit;
    }
    return value;
}

// =========================================================================
//  Files
// =========================================================================

void *file_open(const char *path, const char *name, const char *mode)
{
    char full[256];
    int n = 0;
    while (path[n] && n < 250) {
        full[n] = path[n];
        n++;
    }
    if (n > 0 && full[n - 1] != '/') {
        full[n++] = '/';
    }
    for (int i = 0; name[i] && n < 255; i++) {
        full[n++] = name[i];
    }
    full[n] = 0;
    return g_api->fs->fopen(full, mode);
}

int file_read(void *f, void *buf, int len) { return g_api->fs->fread(buf, 1, len, f); }
int file_write(void *f, const void *buf, int len) { return g_api->fs->fwrite(buf, 1, len, f); }
int file_seek(void *f, long offset) { return g_api->fs->fseek(f, offset, 0); }
void file_close(void *f) { g_api->fs->fclose(f); }

// ---- File name helpers, same rules as components/pattern.cc ----

static void add_extension(char *buffer, const char *ext, int buf_size)
{
    // skip leading dots of extension to set
    while (*ext == '.') {
        ext++;
    }
    int ext_len = strlen(ext) + 1; // +1 because of dot
    if (buf_size < 1 + ext_len)
        return; // cant append, even to an empty base

    if (!(*ext)) { // there is nothing to append
        return;
    }

    int name_len = strlen(buffer);
    if (name_len + ext_len + 1 > buf_size) {
        buffer[buf_size - ext_len - 1] = 0; // truncate to make space for extension!
        name_len = buf_size - ext_len - 1;
    }
    buffer[name_len++] = '.';
    // strcat + tolower
    while (*ext) {
        char c = *(ext++);
        buffer[name_len++] = (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
    }
    buffer[name_len] = 0;
}

static void set_extension(char *buffer, const char *ext, int buf_size)
{
    // try to remove the existing extension
    int name_len = strlen(buffer);
    for (int i = name_len - 1; i >= 0; i--) {
        if (buffer[i] == '.') {
            buffer[i] = 0;
            break;
        }
    }

    add_extension(buffer, ext, buf_size);
}

static int fix_filename(char *buffer)
{
    const char illegal[] = "\"*:/<>\\?|,\x7F";
    int illegal_count = strlen(illegal);
    int len = strlen(buffer);

    int replacements = 0;
    for (int i = 0; i < len; i++) {
        for (int j = 0; j < illegal_count; j++) {
            if (buffer[i] == illegal[j]) {
                buffer[i] = '_';
                replacements++;
            }
        }
    }
    return replacements;
}

static FRESULT create_file_ask_if_exists(const char *path, const char *filename, void **f)
{
    if (g_api->fs->exists(path, filename)) {
        if (u2p_ui_popup_buttons("File already exists. Overwrite?", BUTTON_YES | BUTTON_NO) != BUTTON_YES) {
            return FR_EXIST;
        }
    }
    *f = file_open(path, filename, "wb");
    return *f ? FR_OK : FR_DENIED;
}

FRESULT create_named_file(const char *path, const char *base, const char *ext, void **f)
{
    char fname[32];
    strncpy(fname, base, 31);
    fname[31] = 0;
    fix_filename(fname);
    set_extension(fname, ext, 32);
    return create_file_ask_if_exists(path, fname, f);
}

// The name create_user_file() gives the file for what the user typed
void user_file_name(const char *base, const char *ext, char *out)
{
    strncpy(out, base, 31);
    out[31] = 0;
    fix_filename(out);
    set_extension(out, ext, 32);
}

FRESULT create_user_file(const char *message, const char *ext, const char *path, void **f, char *buffer)
{
    char filename[32];
    *f = NULL;
    if ((u2p_ui_string_input(message, buffer, 22) > 0) && (*buffer)) {
        strcpy(filename, buffer);
        fix_filename(filename);
        set_extension(filename, ext, 32);
        return create_file_ask_if_exists(path, filename, f);
    }
    return FR_DENIED;
}
