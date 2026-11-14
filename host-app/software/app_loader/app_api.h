/*
 * app_api.h
 *
 * U64 App ABI, major version 2.
 *
 * The firmware calls `app_main(api, ctx)`. `api` points to a small root table
 * that holds one pointer per area (system, memory, UI, files, C64 bus, media,
 * configuration, diagnostics, network, TRX64 debug block). Every table starts
 * with its own `size`.
 *
 * Rules:
 *  - A table only grows at its end. Nothing is reordered or removed without a
 *    new major version.
 *  - An app asks with U2P_HAS() whether a field is there before it uses one
 *    that is newer than the oldest firmware it wants to run on. U2P_HAS()
 *    checks the table's size and that the entry is not NULL.
 *  - A table pointer is NULL when this firmware or this build does not offer
 *    the area at all (`diag` in a release build, for instance).
 *  - Calls that can fail return an int: 0 or a positive count on success, a
 *    negative u2p_err_e otherwise.
 *
 * Major 1 was a single flat table with a reserved tail. The loader refuses an
 * app built for it, so it has to be rebuilt against this header.
 *
 * This file is the only copy. The SDK in software/appsdk includes it.
 */

#ifndef APP_LOADER_APP_API_H_
#define APP_LOADER_APP_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define U2P_ABI_MAJOR 2
#define U2P_ABI_MINOR 1

/* Errors returned by the calls below */
enum u2p_err_e {
    U2P_OK = 0,
    U2P_ERR_ARG = -1,         /* an argument is missing or out of range */
    U2P_ERR_NOT_FOUND = -2,   /* no such file, drive, category or item */
    U2P_ERR_IO = -3,          /* the file could not be read or written */
    U2P_ERR_FORMAT = -4,      /* the file is not in a format the call accepts */
    U2P_ERR_BUSY = -5,        /* the subsystem is locked by someone else */
    U2P_ERR_UNSUPPORTED = -6, /* not on this device or not in this build */
    U2P_ERR_FAILED = -7,      /* the firmware refused, for another reason */
};

/* Whether `field` of the table `t` exists in this firmware and is set. */
#define U2P_HAS(t, field)                                                                                              \
    ((t) && ((t)->size >= offsetof(__typeof__(*(t)), field) + sizeof((t)->field)) && (t)->field)

/* App context (second argument of app_main)                          */

enum u2p_launch_source_e {
    U2P_LAUNCH_DIRECT = 0,  /* reserved */
    U2P_LAUNCH_BROWSER = 1, /* File Browser action */
    U2P_LAUNCH_MENU = 2,    /* task menu (F5) entry */
    U2P_LAUNCH_HTTP = 3,    /* PUT /v1/apps/{id}:run */
};

typedef struct u2p_app_context_s
{
    uint32_t size;             /* sizeof(u2p_app_context_t) as built into the firmware */
    const char *app_path;      /* always set, e.g. "/Usb0/apps/foo.u2a" */
    const char *selected_file; /* NULL or a full path: the browsed file, or `file` of a REST call */
    uint32_t launch_source;    /* enum u2p_launch_source_e */
    const char *args;          /* NULL or `args` of a REST call, passed on unchanged */
    const char *action;        /* manifest action, NULL for a plain "Run App" */
    int has_ui;                /* 0 when no screen is attached: dialogs then only log */

    /* Adds text to the answer of the REST call that started the app. Without a
     * REST call the text goes to the log. Returns the number of bytes kept;
     * the answer holds at most 4 KB and drops the rest. */
    int (*reply)(const char *text);
} u2p_app_context_t;

/* sys: log, time, the machine as a whole                             */

typedef struct u2p_sys_api_s
{
    uint32_t size;

    /* printf to the firmware log (UART, syslog). At most 255 characters. */
    int (*log)(const char *fmt, ...);

    /* Sleeps at least ms milliseconds and lets every task of lower priority
     * run meanwhile. The wait is a whole number of RTOS ticks (5 ms on the
     * U64-II): ms is rounded up, plus one tick for the one the call starts in,
     * so ms > 0 is never shorter than ms and never a bare yield. ms = 0 only
     * yields to tasks of the same priority (a task of lower priority does not
     * run). An app waiting for another task, which runs at a lower priority
     * than the UI task (the detached task, trx->detach), must sleep. */
    void (*delay_ms)(uint32_t ms);
    uint32_t (*tick_ms)(void);

    /* Read len bytes of the flash the firmware boots from, starting at byte
     * address addr. Returns len, or U2P_ERR_ARG when the range is not in the
     * flash. Interrupts are off while it reads, so keep len to a few KiB. */
    int (*flash_read)(uint32_t addr, void *buf, int len);

    /* Get the machine ready to jump into other firmware, as Run Update does
     * before it starts an updater: the C64 leaves the menu and runs on, and
     * interrupts are switched off. There is no way back into this firmware
     * afterwards; the caller copies what it starts and jumps to it. */
    void (*machine_handover)(void);
} u2p_sys_api_t;

/* mem: allocations are tracked and freed when the app returns        */

typedef struct u2p_mem_api_s
{
    uint32_t size;
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    void *(*realloc)(void *ptr, size_t size);
} u2p_mem_api_t;

/* ui: dialogs, keys, windows on the menu screen                      */

typedef struct u2p_ui_api_s
{
    uint32_t size;

    /* Dialogs. Without a screen (ctx->has_ui == 0) they log and return the
     * "cancel" result. */
    void (*popup)(const char *title, const char *msg);
    int (*confirm)(const char *msg);                           /* 0 no, 1 yes */
    int (*popup_buttons)(const char *msg, uint8_t buttons);    /* the pressed BUTTON_* */
    int (*menu)(const char *title, const char **items, int n); /* index, or -1 */

    /* Menu that accepts letter shortcuts, one letter per item in keys[].
     * Returns the index or -1. The letter is matched without regard to case.
     * Avoid w, a, s and d: with WASD cursors they steer the selection. */
    int (*menu_keys)(const char *title, const char **items, int n, const char *keys);

    int (*string_input)(const char *title, char *buf, int maxlen);
    /* buf is the start path when set; empty means "where the last pick went" */
    int (*path_input)(char *buf, int maxlen);

    /* Keys come through the firmware's keymapper. getkey waits for one;
     * getch returns -1 when none is waiting and -2 when the screen is gone,
     * after which the app has to return. */
    int (*getkey)(void);
    int (*getch)(void);

    /* Which key code the function key Fn (1..8) currently produces; -1 for an
     * unknown n. What F1 means depends on the interface the machine presents. */
    int (*fkey)(int n);

    /* UI colour: 0 fg, 1 bg, 2 sel, 3 sel_bg, 4 status, 5 inactive, 6 border */
    int (*color)(int which);

    void *(*progress_open)(const char *title, int total);
    void (*progress_set)(void *h, int value, const char *msg);
    void (*progress_close)(void *h);

    /* Windows. screen_open clears the screen, prints the title on row 0 and
     * returns a bordered window from row 2; screen_open_full returns a
     * borderless, untitled 40 column window from row 2, like the firmware
     * cartridge tools use. Close both with screen_close. */
    void *(*screen_open)(const char *title, int w, int h);
    void *(*screen_open_full)(void);
    void (*screen_close)(void *h);
    void (*screen_puts)(void *h, int x, int y, const char *s, int color); /* color -1 keeps the current one */

    void (*win_clear)(void *h);
    void (*win_set_color)(void *h, int color);
    void (*win_set_background)(void *h, int color);
    void (*win_reverse_mode)(void *h, int on);
    void (*win_move_cursor)(void *h, int x, int y);
    void (*win_putc)(void *h, char c);
    void (*win_puts)(void *h, const char *s);
    void (*win_repeat)(void *h, char c, int n);
    void (*win_set_char_at)(void *h, int x, int y, char c);
    void (*win_draw_border)(void *h);
    void (*win_reset_border)(void *h);
    int (*win_get_size_x)(void *h);
    int (*win_get_size_y)(void *h);

    /* When the app returns, the menu it was started from closes (the overlay
     * goes away, the C64 runs on) instead of showing the file browser again.
     * Set while the app runs; ignored for an app started from REST (there is
     * no menu) and after the app handed the machine over (media->run,
     * media->reset: the menu is gone then anyway) (appended). */
    void (*close_menu)(void);

    /* A wide screen (appended, T21 §8.10): the overlay switches to 80 character cells per line and
     * `rows` lines (12..50), the menu's own 40 x 25 screen is kept, and the call returns a window
     * that covers all of it from row 0 (win_get_size_x/y tell the size, 80 x rows). Close it with
     * screen_close: the overlay is 40 x 25 again and what the menu had drawn is back. The firmware
     * also takes the wide mode back whenever the overlay is given back or the app returns, whatever
     * the app did. NULL: no overlay screen here (a freeze menu on the C64's own screen, or no
     * overlay), nothing changed; use screen_open_full then. One wide screen at a time
     * (U2P_HAS(ui, screen_open_wide)). */
    void *(*screen_open_wide)(int rows);

    /* Like getch, but through the keymap the firmware's own monitor reads with (e_keymap_monitor): the
     * letters w a s d stay letters when "Navigation Style" is "WASD Cursors". The function keys are the
     * interface's, as in getch: KEY_PAGEUP, KEY_HELP, KEY_TASKS, KEY_PAGEDOWN are F1 F3 F5 F7 in the
     * Ultimate interface and F3 F7 F1 F5 in the Commodore one (fkey tells). Returns -1 when no key is
     * waiting and -2 when the screen is gone (appended). */
    int (*getch_monitor)(void);

    /* CTRL alone (appended, T55): on != 0 makes the C64 keyboard report CTRL pressed and let go with no other
     * key in between as the key KEY_CTRL_TAP (0x1F8) from getch / getch_monitor. CTRL together with a key is
     * what it was (a control code); a USB keyboard has no such key. Off for everyone else: the firmware's own
     * screens never see the code. The firmware turns it off again when the app returns or its screen callback
     * ends. (U2P_HAS(ui, key_ctrl_tap)) */
    void (*key_ctrl_tap)(int on);
} u2p_ui_api_t;

/* fs: files by full path ("/Usb0/dir/file")                           */

typedef struct u2p_fs_api_s
{
    uint32_t size;
    void *(*fopen)(const char *path, const char *mode); /* stdio modes; NULL on failure */
    int (*fread)(void *buf, int size, int n, void *f);
    int (*fwrite)(const void *buf, int size, int n, void *f);
    int (*fseek)(void *f, long offs, int whence);
    long (*ftell)(void *f);
    int (*fclose)(void *f);

    /* 1 if file 'name' exists in directory 'path', else 0 */
    int (*exists)(const char *path, const char *name);

    /* Create one directory. U2P_OK when it is there afterwards, created now or
     * already. The parent must exist. */
    int (*mkdir)(const char *path);

    /* Appended (T55, ask with U2P_HAS(fs, dir)): the entries of a directory, one cb call each, in the
     * order the file system gives them (. and .. are not among them). cb returns non-zero to stop. Names are
     * long file names; `size` is the file's size in bytes (0 for a directory). U2P_OK, U2P_ERR_NOT_FOUND
     * when the directory is not there, U2P_ERR_IO. */
    int (*dir)(const char *path, int (*cb)(void *ctx, const char *name, uint32_t size, int is_dir), void *ctx);

    /* Appended (T55): removes an empty directory. U2P_OK, U2P_ERR_NOT_FOUND, U2P_ERR_IO (not empty, or the
     * medium refused) */
    int (*rmdir)(const char *path);
} u2p_fs_api_t;

/* c64: the C64 bus while the machine is stopped                      */

typedef struct u2p_c64_api_s
{
    uint32_t size;
    uint8_t (*peek)(uint16_t addr);
    void (*poke)(uint16_t addr, uint8_t val);
    int (*dma_read)(uint16_t addr, void *dst, int len);
    int (*dma_write)(uint16_t addr, const void *src, int len);

    /* Read C64 memory as the firmware's own tools see it while the machine is
     * frozen: $0400-$0FFF and the colour RAM come from the freeze buffers, so
     * the caller gets the screen the C64 had, not the menu drawn over it.
     * dma_read reads the bus as it stands instead. Returns the bytes read. */
    int (*dma_read_frozen)(uint16_t addr, void *dst, int len);

    /* cart_setup_bus saves the bus registers, C64_DMA_MEMONLY and the CPU
     * port, clears C64_DMA_MEMONLY and maps the cartridge (external bus only,
     * CPU port $37); cart_restore_bus puts everything back. C64_MODE is left
     * alone. Not a freeze API: the CPU state stays with the firmware. */
    void (*cart_setup_bus)(void);
    void (*cart_restore_bus)(void);
    void (*cart_mode)(uint8_t mode); /* 0 normal, 2 ultimax, ... */

    /* GAME (bit 0) and EXROM (bit 1) as read from U64_CART_DETECT:
     * 8K 0b01, 16K 0b00, Ultimax 0b10, no cartridge 0b11 */
    uint8_t (*cart_detect)(void);

    /* Cartridge Preference: 0 Auto, 1 Internal, 2 External, 3 Manual */
    int (*cart_preference)(void);
} u2p_c64_api_t;

/* media: drives and what the machine runs                            */

enum u2p_drive_e {
    U2P_DRIVE_A = 1,
    U2P_DRIVE_B = 2,
};

enum u2p_mount_mode_e {
    U2P_MOUNT_READWRITE = 0, /* writes go back into the file */
    U2P_MOUNT_READONLY = 1,  /* the disk is write protected */
    U2P_MOUNT_UNLINKED = 2,  /* writes stay in memory */
};

typedef struct u2p_media_api_s
{
    uint32_t size;

    /* The file behind a medium in the machine: role 0 = cartridge, 1 = drive
     * A, 2 = drive B. Writes a full pathname into buf and returns its length,
     * or 0 when that slot holds nothing. */
    int (*source)(int role, char *buf, int len);

    /* Put a disk image (d64, g64, d71, g71, d81) into a drive, as
     * PUT /v1/drives/{drive}:mount does. The type follows the extension. */
    int (*mount)(int drive, const char *path, int mode);
    int (*unmount)(int drive);

    /* Start a program (.prg) or a cartridge (.crt), or reset the C64. These
     * take the machine away from the menu, so they happen after the app has
     * returned: the call checks its argument and returns U2P_OK, and the last
     * one made is carried out once app_main is back. */
    int (*run)(const char *path);
    int (*reset)(void);
} u2p_media_api_t;

/* config: the settings of the menu, by category and item name        */

#define U2P_CFG_PERSIST 0x01 /* also write the category to flash */

typedef struct u2p_config_api_s
{
    uint32_t size;

    /* Category and item are the names the menu and GET /v1/configs show,
     * compared ignoring case. Values are text: the choice for a list, the
     * number for a numeric item, the string for a text item.
     * get writes the current value into buf and returns its length. */
    int (*get)(const char *category, const char *item, char *buf, int len);

    /* Takes effect at once. Without U2P_CFG_PERSIST the change lives in memory
     * until the next boot, as with PUT /v1/configs. */
    int (*set)(const char *category, const char *item, const char *value, int flags);
} u2p_config_api_t;

/* diag: tools for development; NULL in release builds                */

typedef struct u2p_diag_api_s
{
    uint32_t size;
} u2p_diag_api_t;

/* net: TCP for an app that serves (ABI 2.1)                         */

/* Handles are the firmware's sockets. Every handle an app still holds when it
 * returns is closed by the loader. */
typedef struct u2p_net_api_s
{
    uint32_t size;

    /* A listening TCP socket on every interface. Returns a handle, or
     * U2P_ERR_BUSY when the port is taken or no socket is free. */
    int (*listen)(uint16_t port, int backlog);

    /* A waiting connection, or U2P_ERR_BUSY when there is none. Never blocks. */
    int (*accept)(int listener);

    /* Up to len bytes. 0: the peer closed. U2P_ERR_BUSY: nothing waiting.
     * U2P_ERR_IO: the connection broke. Never blocks. */
    int (*recv)(int h, void *buf, int len);

    /* All len bytes (blocks while the send buffer is full), or U2P_ERR_IO. */
    int (*send)(int h, const void *buf, int len);

    int (*close)(int h);

    /* Waits up to timeout_ms until one of the n handles (n <= 16) has data, a
     * close or, for a listener, a connection waiting. Returns a mask, bit i
     * for handles[i]; 0 on timeout. A handle < 0 is skipped. */
    int (*wait)(const int *handles, int n, uint32_t timeout_ms);

    /* The remote end of a connection as "a.b.c.d:port", NUL-terminated, cut to
     * len. Returns the length, or U2P_ERR_ARG for a handle that is not a
     * connection. Appended after ABI 2.1: ask with U2P_HAS(net, peer). */
    int (*peer)(int h, char *buf, int len);
} u2p_net_api_t;

/* trx: the TRX64 Ultimate debug block (ABI 2.1)                      */

typedef struct
{
    uint16_t lines;           /* per frame: 312 (PAL, NTSC-50) or 263 (NTSC, PAL-60) */
    uint16_t cycles_per_line; /* 63 or 65 */
    uint32_t frame_cycles;    /* lines * cycles_per_line */
    uint32_t cpu_hz;
    uint32_t fps_x100;
    uint8_t hz60;
    uint8_t reserved[3];
} u2p_video_t;

/* The T20 frame ring, as the debugger uses it (TRX64-Ultimate
 * docs/specs/T20-delta-ring.md, T21-monitor.md §4.10; the firmware's
 * software/io/c64/trx_ring.h). Entries are frame starts (snaps), oldest
 * first; an index moves when old entries go, a seq does not. */
typedef struct
{
    uint8_t present;  /* the bitstream has the engine */
    uint8_t on;       /* the ring records */
    uint8_t landed;   /* a land is open: the machine stands in entry landed_seq's frame */
    uint8_t reu_off;  /* A08: the REU is enabled, so there is no ring (was reserved: 0 from older firmware) */
    int32_t frames;   /* entries */
    int32_t targets;  /* entries a land can go to */
    uint32_t head_seq, oldest_seq, landed_seq;
} u2p_ring_status_t;

typedef struct
{
    uint32_t seq;
    uint32_t c24;    /* the machine's phi2 cycle count after the snap edge, low 24 bits:
                      * the debug block's CYCLES counts the same edges */
    uint64_t cycles; /* phi2 cycles since the ring started */
    uint16_t line, x; /* VIC position of the snap edge */
    uint8_t flags;   /* the marker's flags (T20 trx_engine.v F1) */
    uint8_t target;  /* a land can go there (a whole chip block, the logs reach it) */
    uint16_t reserved;
} u2p_ring_frame_t;

/* The instruction trace (T21-monitor.md §4.11): TRX64's DeltaEntry, 24 bytes, the history of
 * the timeline the machine is on. cycle is the timeline's phi2 count after the edge that ended
 * the opcode fetch (u2p_ring_frame_t.cycles' coordinate); a, x, y, sp, p and pc are BEFORE the
 * instruction; b1/b2 are 0 where the opcode has no such byte. The instruction's writes are the
 * stream 0 records [write_start, write_start + write_count). */
typedef struct
{
    uint64_t cycle;
    uint32_t write_start;
    uint16_t pc;
    uint16_t write_count;
    uint8_t a, x, y, sp, p, opcode, b1, b2;
} u2p_trace_rec_t;

typedef struct
{
    uint64_t cycle; /* of the writing instruction */
    uint16_t addr, pc;
    uint8_t old_value, new_value;
    uint16_t flags; /* U2P_TRACE_HIT_*: values the trace cannot give (writes of I/O addresses) */
    uint32_t index; /* of the writing instruction in the history */
} u2p_trace_hit_t;
#define U2P_TRACE_HIT_OLD_UNKNOWN 1 /* old_value is not known (I/O: the record has no old byte) */
#define U2P_TRACE_HIT_NEW_UNKNOWN 2 /* new_value is not known (a read-modify-write of an I/O register) */

typedef struct
{
    uint8_t present; /* the bitstream has the trace former and the ring runs */
    uint8_t on;      /* the trace gate */
    uint8_t gap;     /* the history has a hole (a forward land past what the replays reached) */
    uint8_t reserved;
    uint32_t records;  /* in the history */
    uint32_t capacity; /* the area holds this many (1 s at 1 MHz) */
    uint32_t lost;     /* records the engine dropped */
    uint64_t oldest_cycle, newest_cycle;
} u2p_trace_status_t;

typedef struct u2p_ring_api_s
{
    uint32_t size;
    int (*status)(u2p_ring_status_t *st);
    int (*frame)(int index, u2p_ring_frame_t *f); /* U2P_OK, U2P_ERR_NOT_FOUND */
    int (*find)(uint32_t seq);                    /* the index, or -1 */

    /* Puts the machine on entry index and leaves it paused on that frame's
     * snap edge, the debugger holding it, without cutting the timeline: what
     * runs from there is undone by the next land. The stop record says
     * CYC_CMP; the app sets IPC (page 3 IPC_SET). 0, or < 0: error(). */
    int (*land)(int index);
    /* The debugger runs on from a land: the frames after it are cut and the
     * ring records again. Nothing when no land is open. */
    void (*commit)(void);
    const char *(*error)(int code);

    /* Three slots that held the .c64re / .c64rering dump and load (removed in M1, TODO T66).
     * They stay so the offsets of what follows do not move; always NULL. */
    const void *reserved_files[3];

    /* The instruction trace (appended, T21 §4.11): U2P_HAS(ring, trace_status) */
    /* on 1/0 sets the trace gate, -1 asks; returns the gate, or < 0 */
    int (*trace_ctl)(int on);
    int (*trace_status)(u2p_trace_status_t *st);
    /* records first.. (0 = oldest) of the history; returns the count read */
    int (*trace_read)(uint32_t first, uint32_t n, u2p_trace_rec_t *out);
    /* was trace_writes (the .c64retrace export, removed in M1, TODO T66); always NULL, keeps the offset */
    const void *reserved_trace_writes;
    /* the last writers of addr, newest first. *exhausted: bit 0, the scan reached the start of the
     * history with fewer than limit hits; bit 1 (value 2), addr is an I/O address ($D000-$DFFF), its
     * writers were found from the instruction records (the RAM write log has no I/O), and the
     * history holds writes through a zero page pointer whose address the record does not carry,
     * so those could not be checked. Returns the hits. */
    int (*trace_whowrote)(uint16_t addr, int limit, u2p_trace_hit_t *hits, int *exhausted);

    /* The debug block's log action (T55, appended: U2P_HAS(ring, log_read)): the engine's stream 1 records of
     * space 8 that came since the last call, 8 bytes each ([15:0] address, [23:16] data, [27:24] {0, IB, write,
     * unit}, [31:28] 8, [47:32] IPC of the instruction, [62:48] phi2 cycle count, low 15 bits). They exist
     * while the frame ring runs. max = 0 moves the reader to the present. *lost: records the area overwrote
     * before they were read. Returns the count. */
    int (*log_read)(uint64_t *out, int max, uint32_t *lost);
} u2p_ring_api_t;

/* NULL unless the bitstream's IO block (0x101A0000) reads "TRX2" and has
 * the debug page (CAPS b3). The registers are the app's (T21-monitor.md
 * §5.1); the table holds only what the firmware has to know about. */
typedef struct u2p_trx_api_s
{
    uint32_t size;
    volatile uint8_t *regs; /* the IO block, byte-wide */
    uint32_t caps;          /* its CAPS register */

    /* Run control for the debugger: the machine is held (PAUSE_CB), then
     * OWNER is set, so the app's C64_STOP no longer stops the machine. The
     * loader calls release() when the app returns. flags: reserved, 0. */
    int (*acquire)(uint32_t flags);

    /* Disarms the exec bitmap, the watch units, the drive comparator and
     * CYC_CMP, clears OWNER and the debugger's hold: C64_STOP rules the
     * machine again. */
    void (*release)(void);

    /* The TCP port the app serves the TRX64 runtime contract on (0: none).
     * The ident service and GET /v1/info report it as trx64.rpc. Cleared
     * when the app returns. */
    void (*announce)(uint16_t rpc_port);

    /* Keeps the app in memory and runs fn(arg) in a task of its own (low
     * priority) after app_main has returned, so the menu and the machine go
     * on while the app serves. Until fn returns no other app starts; then the
     * loader frees what the app allocated, closes its sockets and releases
     * the debug block. fn must not use the ui table. U2P_ERR_BUSY when a
     * task is already running. */
    int (*detach)(void (*fn)(void *arg), void *arg);

    /* The frame ring (NULL without the engine, CAPS b2) */
    const u2p_ring_api_t *ring;

    /* The video System Mode that runs now (T40), read on each call (appended) */
    int (*video)(u2p_video_t *v);

    /* Non-zero once the loader wants the detached task to end: a person
     * started another app from the menu. The task polls it and returns from
     * fn as soon as it can (appended) */
    int (*stop_requested)(void);

    /* For a detached task, which must not use the ui table itself (appended):
     * queues "open the overlay and call fn(arg)". The firmware does what the
     * menu button does: takes the overlay, calls fn in the UI task with a
     * valid ui table, and when fn returns draws the menu underneath again and
     * gives the overlay back. While a person is in a menu or another app the
     * request waits and runs when that closes. U2P_OK: queued. U2P_ERR_BUSY:
     * one is queued already. U2P_ERR_UNSUPPORTED: no overlay user interface.
     * Dropped when the detached task ends. */
    int (*ui_request)(void (*fn)(void *arg), void *arg);
} u2p_trx_api_t;

/* Root table                                                         */

typedef struct u2p_api_s
{
    uint16_t abi_major;           /* U2P_ABI_MAJOR */
    uint16_t abi_minor;           /* informational: U2P_HAS() decides what an app may use */
    uint32_t size;                /* sizeof(u2p_api_t) as built into the firmware */
    const char *firmware_version; /* e.g. "3.15" */

    const u2p_sys_api_t *sys;
    const u2p_mem_api_t *mem;
    const u2p_ui_api_t *ui;
    const u2p_fs_api_t *fs;
    const u2p_c64_api_t *c64;
    const u2p_media_api_t *media;
    const u2p_config_api_t *config;
    const u2p_diag_api_t *diag;
    const u2p_net_api_t *net; /* ABI 2.1 */
    const u2p_trx_api_t *trx; /* ABI 2.1; NULL without the TRX64 debug block */

    /* Appended after ABI 2.1 (ask with U2P_HAS(api, firmware_git)): the short git
     * hash of the firmware build, the git_commit_hash of GET /v1/version. */
    const char *firmware_git;
} u2p_api_t;

/* App entry point                                                    */

/* Every app implements this. The return value is the exit code: 0 success. */
int app_main(const u2p_api_t *api, const u2p_app_context_t *ctx);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* APP_LOADER_APP_API_H_ */
