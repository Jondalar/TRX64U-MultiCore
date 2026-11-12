/*
 * ui_shim.c
 *
 *  Firmware building blocks for the ported cartridge tools, implemented on
 *  top of the app ABI. Freestanding: also provides the few libc string
 *  functions the tools use.
 */

#include "ui_shim.h"

#ifndef HOST_TEST // the host harness links the host's libc
// =========================================================================
//  Freestanding libc subset
// =========================================================================

// GCC may emit calls to these even in freestanding code, so they carry the
// standard names. The attribute keeps GCC from turning the loops into calls.
#define NO_CALL_LOOP __attribute__((optimize("no-tree-loop-distribute-patterns")))

NO_CALL_LOOP void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

NO_CALL_LOOP void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) {
            return p[i] - q[i];
        }
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

NO_CALL_LOOP char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

NO_CALL_LOOP char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = 0;
    }
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#endif

// =========================================================================
//  Window
// =========================================================================

Window *window_open(void) { return (Window *)g_api->ui->screen_open_full(); }

void window_close(Window *w)
{
    if (w) {
        g_api->ui->screen_close(w);
    }
}

// A NULL window (a screen that did not open) draws nothing
void window_clear(Window *w) { if (w) g_api->ui->win_clear(w); }
void window_set_color(Window *w, int color) { if (w) g_api->ui->win_set_color(w, color); }
void window_reverse_mode(Window *w, int on) { if (w) g_api->ui->win_reverse_mode(w, on); }
void window_move_cursor(Window *w, int x, int y) { if (w) g_api->ui->win_move_cursor(w, x, y); }
void window_output(Window *w, const char *s) { if (w) g_api->ui->win_puts(w, s); }
void window_output_char(Window *w, char c) { if (w) g_api->ui->win_putc(w, c); }
int window_get_size_y(Window *w) { return w ? g_api->ui->win_get_size_y(w) : 0; }

// =========================================================================
//  Key loop
// =========================================================================

// The code a function key arrives as depends on the interface the machine
// presents: the Ultimate one sends F1 as KEY_PAGEUP, the Commodore one as
// KEY_TASKS. The tools label their keys F1..F7 and mean the physical key, so
// the code is turned back into that key before a tool sees it. Without that,
// F7 (select slot) reached the writer as F3 (write) in the Commodore interface.
int ui_fkey_pressed(int c)
{
    static const int fkeys[8] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8 };
    if (g_api->ui->fkey) {
        for (int n = 1; n <= 8; n++) {
            if (c == g_api->ui->fkey(n)) {
                return fkeys[n - 1];
            }
        }
        return c;
    }
    // A firmware without ui_fkey has only the Ultimate layout
    switch (c) {
    case KEY_PAGEUP:   return KEY_F1;
    case KEY_HELP:     return KEY_F3;
    case KEY_TASKS:    return KEY_F5;
    case KEY_PAGEDOWN: return KEY_F7;
    default:           return c;
    }
}

int ui_run(void *obj, ui_poll_fn poll)
{
    for (;;) {
        int c = g_api->ui->getch();
        if (c == -2) {
            return MENU_EXIT;
        }
        if (c < 0) {
            g_api->sys->delay_ms(10);
            continue;
        }
        int ret = poll(obj, ui_fkey_pressed(c));
        if (ret != 0) {
            return ret;
        }
    }
}

// =========================================================================
//  Formatting
// =========================================================================

typedef struct
{
    char *buf;
    int size;
    int pos;
} fmt_out_t;

static void fmt_char(fmt_out_t *o, char c)
{
    if (o->pos < o->size - 1) {
        o->buf[o->pos] = c;
    }
    o->pos++;
}

int ui_vsnprintf(char *buf, int size, const char *fmt, va_list ap)
{
    fmt_out_t o = { buf, size, 0 };
    char digits[12];

    while (*fmt) {
        if (*fmt != '%') {
            fmt_char(&o, *fmt++);
            continue;
        }
        fmt++;

        char pad = ' ';
        int width = 0;
        int precision = -1;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt++ - '0');
            }
        }

        const char *str = digits;
        int len = 0;
        bool negative = false;

        switch (*fmt) {
        case 'd':
        case 'u':
        case 'x':
        case 'X': {
            unsigned int base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
            const char *hex = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            unsigned int value;
            if (*fmt == 'd') {
                int v = va_arg(ap, int);
                negative = (v < 0);
                value = negative ? (unsigned int)(-(v + 1)) + 1 : (unsigned int)v;
            } else {
                value = va_arg(ap, unsigned int);
            }
            char rev[12];
            int n = 0;
            do {
                rev[n++] = hex[value % base];
                value /= base;
            } while (value);
            if (negative) {
                digits[len++] = '-';
            }
            while (n) {
                digits[len++] = rev[--n];
            }
            break;
        }
        case 's':
            str = va_arg(ap, const char *);
            if (!str) {
                str = "(null)";
            }
            while (str[len] && (precision < 0 || len < precision)) {
                len++;
            }
            break;
        case 'c':
            digits[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 0:
            continue; // format ends after '%'
        default:
            fmt_char(&o, '%');
            digits[0] = *fmt;
            len = 1;
            break;
        }
        fmt++;

        // A zero-padded negative number keeps its sign in front of the zeros
        if (negative && pad == '0') {
            fmt_char(&o, '-');
            str++;
            len--;
            width--;
        }
        for (int i = len; i < width; i++) {
            fmt_char(&o, pad);
        }
        for (int i = 0; i < len; i++) {
            fmt_char(&o, str[i]);
        }
    }

    if (size > 0) {
        o.buf[(o.pos < size) ? o.pos : size - 1] = 0;
    }
    return o.pos;
}

int ui_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = ui_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

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
