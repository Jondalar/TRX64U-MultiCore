// The service tables of ABI 3 (T104.3 D2), as plain structs that C and C++ both read: the base fills them (libload.cc, appsvc.cc), an application
// (C++ or C, built by app/lib/lib.mk) is handed a pointer to them in `LibApi` (libapi.h) and calls through them. Every group starts with its own
// `size`; a field is there when the size covers it (the U2P_HAS rule of format 2) and a group the base or the core does not have is a null pointer.
//
// There is no UI group: an application brings its screens as a description (T104.3 D0), never as code. What it may ask of the person while it runs are
// the dialogs of D4 (`dlg`), which the base draws in its own widgets and which block the calling task until they are answered.
//
// The record types of the ring and the trace in `SvcRing` are those of the application ABI 2 of the TRX64 line (vendor/gideon/software/app_loader/app_api.h,
// GPL-3.0; the trxmon application reads them as they are): PROVENANCE.md.
#ifndef COREAPP_SVCTAB_H
#define COREAPP_SVCTAB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct OpIn;
struct OpResult;

typedef struct SvcSys {
    uint32_t size;
    void (*log)(const char *text);
    uint32_t (*ms)(void);                 // milliseconds since start
    void (*sleep_ms)(uint32_t ms);        // ms of 5 or more: pdMS_TO_TICKS(ms) + 1 ticks; less, 0 included: one tick of 5 ms. No yield
    const char *(*core_name)(void);
    bool (*has_capability)(const char *capability);
    // The flash the firmware boots from, len bytes from addr. len, or -1 when the range is not in the flash. The monolith reads its SPI flash.
    int (*flash_read)(uint32_t addr, void *buf, int len);
    // Gets the machine ready to jump into other firmware: the C64 leaves the menu and runs on, interrupts go off. There is no way back.
    void (*machine_handover)(void);
} SvcSys;

// From the app's own account: what a transient app still holds when it ends is given back with its image (T104.3 D2).
typedef struct SvcMem {
    uint32_t size;
    void *(*malloc)(uint32_t n);
    void (*free)(void *p);
    void *(*realloc)(void *p, uint32_t n);
} SvcMem;

// The registry of T104.1 D1, by name: the same checked call a menu entry or a transport makes.
typedef struct SvcOps {
    uint32_t size;
    int (*call)(const char *name, const struct OpIn *in, int nin, struct OpResult *r);
    const char *(*state)(const char *name, uint32_t *rev);
} SvcOps;

// The entries of a directory, as fsif.h's FsEntry lays them out (libapi.h checks the two are the same size).
typedef struct SvcFsEntry {
    char name[56];
    uint16_t fdate, ftime;
    uint8_t attr;
    bool dir;
    uint64_t size;
} SvcFsEntry;

// The files of the core's CoreFs: 64-bit offsets, all-or-nothing reads and writes. The rule of fsif.h holds here too: a write, delete, mkdir or rename
// under /flash is refused (the three paths of fs_flash_exception excepted), and a read-write open there gives a read-only file. A file an
// application leaves open is closed when it ends.
struct SvcFile;
typedef struct SvcFs {
    uint32_t size;
    struct SvcFile *(*open)(const char *path, bool rw);     // null: not there, or the medium cannot be read
    bool (*read)(struct SvcFile *f, uint64_t off, void *buf, uint32_t len);
    bool (*write)(struct SvcFile *f, uint64_t off, const void *buf, uint32_t len);   // at the end it makes the file longer
    uint64_t (*fsize)(struct SvcFile *f);
    bool (*writable)(struct SvcFile *f);
    bool (*sync)(struct SvcFile *f);
    void (*close)(struct SvcFile *f);
    int (*list)(const char *dir, SvcFsEntry *out, int max);  // the count, -1: not a directory
    bool (*stat)(const char *path, uint64_t *size, bool *dir);
    int (*load)(const char *path, void *buf, uint32_t max);
    bool (*save)(const char *path, const void *buf, uint32_t len);
    bool (*remove)(const char *path);
    bool (*mkdir)(const char *path);
    bool (*rename)(const char *from, const char *to);
} SvcFs;

// What the base asks the person while the application runs (T104.3 D4): drawn by the base in the current skin, modal over whatever is on screen, answered
// with the cursor keys, RETURN and the C64's left arrow. A call blocks the calling task - never the base's own, so an application calls them from its own
// task or a job - until the person answered. RUN/STOP, ESC and the left arrow cancel. Without a screen they log and answer "cancelled".
#define SVC_BUTTON_OK 1
#define SVC_BUTTON_YES 2
#define SVC_BUTTON_NO 4
typedef struct SvcDlg {
    uint32_t size;
    void (*message)(const char *title, const char *text);
    // BUTTON_YES | BUTTON_NO and/or BUTTON_OK: the pressed one, 0 when cancelled.
    int (*buttons)(const char *title, const char *text, int buttons);
    bool (*confirm)(const char *title, const char *text);
    int (*choose)(const char *title, const char *const *items, int n, int sel);   // the index, -1: cancelled
    // `keys`: one letter per item (null: none), matched without regard to case.
    int (*choose_keys)(const char *title, const char *const *items, int n, const char *keys);
    bool (*text)(const char *title, char *buf, int max);        // buf holds the start text; false: cancelled, buf unchanged
    bool (*pick_file)(const char *start, const char *ext, char *out, int outlen);   // ext ".crt" or null for every file; the full path
    bool (*pick_folder)(const char *start, char *out, int outlen);                  // "[ use this folder ]" first
    // A bar with a caption while the application works. Not a question: it returns at once and the bar stays until progress_close.
    void *(*progress_open)(const char *title, int total);
    void (*progress_set)(void *h, int value, const char *text);
    void (*progress_close)(void *h);
} SvcDlg;

// Long work (T104.3 D7): a job runs on a task of its own. cancelled() says the person asked to stop (the job decides whether it can); the base
// stops and waits for every job of an application before its image goes.
struct SvcJobH;
typedef struct SvcJob {
    uint32_t size;
    struct SvcJobH *(*start)(int (*fn)(struct SvcJobH *j, void *user), void *user, uint32_t stack_bytes);   // null: no free slot or no task
    bool (*cancelled)(struct SvcJobH *j);
    int (*join)(struct SvcJobH *j);       // what fn returned; -1: not a job
    bool (*running)(struct SvcJobH *j);
    bool (*stop_requested)(void);         // the base wants the application's own task to end (it is being closed)
} SvcJob;

// The TRX debug block and the cartridge port's slot block: the register window of the bitstream. Null unless the core's ID block reads "TRX2".
typedef struct {
    uint16_t lines;                       // per frame: 312 (PAL, NTSC-50) or 263 (NTSC, PAL-60)
    uint16_t cycles_per_line;             // 63 or 65
    uint32_t frame_cycles;
    uint32_t cpu_hz;
    uint32_t fps_x100;
    uint8_t hz60;
    uint8_t reserved[3];
} u2p_video_t;

typedef struct {
    uint8_t present, on, landed, reu_off;
    int32_t frames, targets;
    uint32_t head_seq, oldest_seq, landed_seq;
} u2p_ring_status_t;

typedef struct {
    uint32_t seq, c24;
    uint64_t cycles;
    uint16_t line, x;
    uint8_t flags, target;
    uint16_t reserved;
} u2p_ring_frame_t;

typedef struct {
    uint64_t cycle;
    uint32_t write_start;
    uint16_t pc, write_count;
    uint8_t a, x, y, sp, p, opcode, b1, b2;
} u2p_trace_rec_t;

typedef struct {
    uint64_t cycle;
    uint16_t addr, pc;
    uint8_t old_value, new_value;
    uint16_t flags;
    uint32_t index;
} u2p_trace_hit_t;
#define U2P_TRACE_HIT_OLD_UNKNOWN 1
#define U2P_TRACE_HIT_NEW_UNKNOWN 2

typedef struct {
    uint8_t present, on, gap, reserved;
    uint32_t records, capacity, lost;
    uint64_t oldest_cycle, newest_cycle;
} u2p_trace_status_t;

typedef struct SvcRing {
    uint32_t size;
    int (*status)(u2p_ring_status_t *st);
    int (*frame)(int index, u2p_ring_frame_t *f);
    int (*find)(uint32_t seq);
    int (*land)(int index);
    void (*commit)(void);
    const char *(*error)(int code);
    const void *reserved_files[3];
    int (*trace_ctl)(int on);
    int (*trace_status)(u2p_trace_status_t *st);
    int (*trace_read)(uint32_t first, uint32_t n, u2p_trace_rec_t *out);
    const void *reserved_trace_writes;
    int (*trace_whowrote)(uint16_t addr, int limit, u2p_trace_hit_t *hits, int *exhausted);
    int (*log_read)(uint64_t *out, int max, uint32_t *lost);
} SvcRing;

typedef struct SvcTrx {
    uint32_t size;
    volatile uint8_t *regs;               // the IO block, byte wide: 0x101A0000 on the board
    uint32_t caps;                        // its CAPS register
    int (*acquire)(uint32_t flags);       // the machine is held, then OWNER is set: the application's C64_STOP no longer stops it
    void (*release)(void);
    void (*announce)(uint16_t rpc_port);  // the TCP port the application serves the runtime contract on (0: none)
    const SvcRing *ring;                  // null without the engine (CAPS b2)
    int (*video)(u2p_video_t *v);
    void (*reset_c64)(void);              // a C64 reset (the cartridge tools end with one: the bank registers are write only)
} SvcTrx;

// The C64's bus while the machine is stopped (the cartridge tools): the memory window of the core, the bus bridge and the pin readings. Null unless the
// core is the C64.
typedef struct SvcC64 {
    uint32_t size;
    uint8_t (*peek)(uint16_t addr);
    void (*poke)(uint16_t addr, uint8_t val);
    int (*dma_read)(uint16_t addr, void *dst, int len);
    int (*dma_write)(uint16_t addr, const void *src, int len);
    // Saves the bus registers, C64_DMA_MEMONLY and the CPU port, then maps the cartridge: the window sees the C64's memory map (I/O, cartridge), only the
    // external bus is on and the CPU port selects BASIC, KERNAL and I/O. C64_MODE is left alone. cart_restore_bus puts everything back.
    void (*cart_setup_bus)(void);
    void (*cart_restore_bus)(void);
    void (*cart_mode)(uint8_t mode);      // 0 normal, 2 ultimax, ...
    uint8_t (*cart_detect)(void);         // GAME (bit 0) and EXROM (bit 1) as read from the expansion port
} SvcC64;

// TCP for an application that serves (trxmon). Handles are sockets; every handle an application still holds when it ends is closed.
typedef struct SvcNet {
    uint32_t size;
    int (*listen)(uint16_t port, int backlog);        // a handle, or -5 when the port is taken or no socket is free
    int (*accept)(int listener);                      // -5: none waiting, never blocks
    int (*recv)(int h, void *buf, int len);           // 0: the peer closed, -5: nothing waiting, -3: broke
    int (*send)(int h, const void *buf, int len);     // all len bytes or -3
    int (*close)(int h);
    int (*wait)(const int *handles, int n, uint32_t timeout_ms);   // a mask, bit i for handles[i]; 0 on timeout
    int (*peer)(int h, char *buf, int len);
} SvcNet;

// The call tables libraries export (T197 P1, base 3.1): find() hands back the table a loaded library put in its record (LibRecord::exports), when a library
// of that name is loaded at that version or newer; `size` is how many bytes of table the library has. The layout of a table is the library's header.
typedef struct SvcLib {
    uint32_t size;
    const void *(*find)(const char *name, uint32_t min_version, uint32_t *table_size);
} SvcLib;

#ifdef __cplusplus
}
#endif

// The root a C application is handed (libapi.h declares the same struct for C++, field for field; app/sdk/u2p_compat.c is the one C user).
#ifndef __cplusplus
struct OpIn;
typedef struct LibApi {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    void (*log)(const char *text);
    const SvcSys *sys;
    const SvcMem *mem;
    const SvcOps *ops;
    const SvcFs *fs;
    const SvcDlg *dlg;
    const SvcJob *job;
    const SvcTrx *trx;
    const SvcNet *net;
    const SvcC64 *c64;
    const SvcLib *lib;
} LibApi;
#endif

#endif
