/*
 * u2p_app.h - the application ABI 2 of the TRX64 line, as the four applications of T104.3 (unzip, softboot, trex_crt, trxmon) were written against it,
 * carried on the service tables of the base API, major 4 (coreapp/src/svctab.h). Base 4.0 dropped the tables trx and c64 (T239.3 P2-6, D57).
 *
 * PROVENANCE: the names and the layout of the tables are those of vendor/gideon/software/app_loader/app_api.h (the TRX64 line's application ABI 2.1,
 * GPL-3.0). Changed: only the calls the four applications make are here; every one is implemented over the service tables in u2p_compat.c; the
 * character screen (screen_open, win_*) and the keys (getch) are gone - an application brings a description of its screens (T104.3 D0) and the
 * base draws it. A dialog is a blocking call of the base (dlg), a window is not.
 */
#ifndef U2P_APP_H_
#define U2P_APP_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "svctab.h"

enum u2p_err_e {
    U2P_OK = 0,
    U2P_ERR_ARG = -1,
    U2P_ERR_NOT_FOUND = -2,
    U2P_ERR_IO = -3,
    U2P_ERR_FORMAT = -4,
    U2P_ERR_BUSY = -5,
    U2P_ERR_UNSUPPORTED = -6,
    U2P_ERR_FAILED = -7,
};

#define U2P_HAS(t, field) ((t) && ((t)->size >= offsetof(__typeof__(*(t)), field) + sizeof((t)->field)) && (t)->field)

#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04


/* Key codes, identical to the firmware's (software/io/c64/keyboard.h); the cartridge tools compare the keys they are given with these. */
#define KEY_BACK      0x08
#define KEY_TAB       0x09
#define KEY_RETURN    0x0D
#define KEY_BREAK     0x11
#define KEY_DOWN      0x12
#define KEY_HOME      0x13
#define KEY_ESCAPE    0x1B
#define KEY_RIGHT     0x1D
#define KEY_SPACE     0x20
#define KEY_CTRL_TAP  0x1F8 /* CTRL alone (the monitor's word completion), when the base turns it on; none in this base's keys */
#define KEY_DELETE    0x7F
#define KEY_F1        0x85
#define KEY_F3        0x86
#define KEY_F5        0x87
#define KEY_F7        0x88
#define KEY_F2        0x89
#define KEY_F4        0x8A
#define KEY_F6        0x8B
#define KEY_F8        0x8C
#define KEY_UP        0x91
#define KEY_PAGEUP    0x92
#define KEY_LEFT      0x9D
#define KEY_PAGEDOWN  0x95
#define KEY_END       0x96

enum u2p_launch_source_e {
    U2P_LAUNCH_DIRECT = 0,
    U2P_LAUNCH_BROWSER = 1,
    U2P_LAUNCH_MENU = 2,
    U2P_LAUNCH_HTTP = 3,
};

typedef struct u2p_app_context_s {
    uint32_t size;
    const char *app_path;
    const char *selected_file;            /* NULL or the full path the entry was opened on */
    uint32_t launch_source;
    const char *args;
    const char *action;                   /* the manifest's word, NULL for a plain open */
    int has_ui;
    int (*reply)(const char *text);       /* a line for the status row of the screen */
} u2p_app_context_t;

typedef struct u2p_sys_api_s {
    uint32_t size;
    int (*log)(const char *fmt, ...);
    void (*delay_ms)(uint32_t ms);
    uint32_t (*tick_ms)(void);
    int (*flash_read)(uint32_t addr, void *buf, int len);
    void (*machine_handover)(void);
} u2p_sys_api_t;

typedef struct u2p_mem_api_s {
    uint32_t size;
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    void *(*realloc)(void *ptr, size_t size);
} u2p_mem_api_t;

typedef struct u2p_ui_api_s {
    uint32_t size;
    void (*popup)(const char *title, const char *msg);
    int (*confirm)(const char *msg);
    int (*popup_buttons)(const char *msg, uint8_t buttons);
    int (*menu)(const char *title, const char **items, int n);
    int (*menu_keys)(const char *title, const char **items, int n, const char *keys);
    int (*string_input)(const char *title, char *buf, int maxlen);
    int (*path_input)(char *buf, int maxlen);
    /* Added for the applications of T104.3: a file of the medium, picked from `start` (a folder); the full path in out. Returns its length, -1 when cancelled. */
    int (*pick_file)(const char *start, const char *ext, char *out, int outlen);
    void *(*progress_open)(const char *title, int total);
    void (*progress_set)(void *h, int value, const char *msg);
    void (*progress_close)(void *h);
} u2p_ui_api_t;

typedef struct u2p_fs_api_s {
    uint32_t size;
    void *(*fopen)(const char *path, const char *mode);
    int (*fread)(void *buf, int size, int n, void *f);
    int (*fwrite)(const void *buf, int size, int n, void *f);
    int (*fseek)(void *f, long offs, int whence);
    long (*ftell)(void *f);
    int (*fclose)(void *f);
    int (*exists)(const char *path, const char *name);
    int (*mkdir)(const char *path);
    int (*dir)(const char *path, int (*cb)(void *ctx, const char *name, uint32_t size, int is_dir), void *ctx);
} u2p_fs_api_t;

typedef struct u2p_media_api_s {
    uint32_t size;
    int (*reset)(void);
} u2p_media_api_t;

/* The ABI 2 name of the network table: the service table itself. Base 4.0 has no debug block table (u2p_trx_api_t), no ring table and no C64 bus table (SvcC64):
   an application that used them reaches the core's blocks through `root` (root->io, the core's driver table) and its libraries' exports through its needs entries
   (root->lib->need, libslot.h). media.reset is the operation "machine.warm_reset" (u2p_compat.c), what SvcTrx::reset_c64 called. */
typedef SvcNet u2p_net_api_t;

typedef struct u2p_api_s {
    uint32_t size;
    const u2p_sys_api_t *sys;
    const u2p_mem_api_t *mem;
    const u2p_ui_api_t *ui;
    const u2p_fs_api_t *fs;
    const u2p_media_api_t *media;
    const u2p_net_api_t *net;
    const SvcJob *job;
    const char *firmware_git;
    const char *firmware_version;
    const LibApi *root;                   /* base 4.0: the root the base gave app_main, for the groups ABI 2 has no table for (io, ops, lib, ctx, ...) */
} u2p_api_t;

/* Builds the tables of ABI 2 over the root the base gave (LibApi, svctab.h); once, first thing in app_main. */
const u2p_api_t *u2p_compat_init(const LibApi *api);

/* The dialogs of the SDK's u2p_ui.h (the redraw hook is not needed: the base keeps the screen) */
void u2p_ui_init(const u2p_api_t *api);
void u2p_ui_popup(const char *title, const char *msg);
int u2p_ui_popup_buttons(const char *msg, uint8_t buttons);
int u2p_ui_confirm(const char *msg);
int u2p_ui_menu(const char *title, const char **items, int n);
int u2p_ui_menu_keys(const char *title, const char **items, int n, const char *keys);
int u2p_ui_string_input(const char *title, char *buf, int maxlen);
int u2p_ui_path_input(char *buf, int maxlen);
/* the flags the tools of trex_crt keep in trex_main.c */

/* snprintf of the few conversions the applications use: %d %u %x %X %s %c %%, a 0 flag, a width, a precision for %s, l and ll are skipped */
int u2p_snprintf(char *buf, int size, const char *fmt, ...);
int u2p_vsnprintf(char *buf, int size, const char *fmt, __builtin_va_list ap);

#endif
