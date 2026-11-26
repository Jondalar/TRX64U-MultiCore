// The service tables of the base API, major 4 (T104.3 D2; T239.3 P2-6), as plain structs that C and C++ both read: the base fills them (libload.cc, appsvc.cc), an
// application (C++ or C, built by app/lib/lib.mk) is handed a pointer to them in `LibApi` (libapi.h) and calls through them. Every group starts with its own
// `size`; a field is there when the size covers it (the U2P_HAS rule of format 2) and a group the base or the core does not have is a null pointer.
//
// There is no UI group: an application brings its screens as a description (T104.3 D0), never as code. What it may ask of the person while it runs are
// the dialogs of D4 (`dlg`), which the base draws in its own widgets and which block the calling task until they are answered.
//
// Base 4.0 has no machine group: SvcTrx, SvcRing and SvcC64 and the ring, trace and video records of the TRX64 line's application ABI 2 (u2p_video_t, u2p_ring_*,
// u2p_trace_*) are gone (T239.3 D20, D57). A core's library that uses those records takes them from that ABI's own header (app_api.h of the TRX64 line).
#ifndef COREAPP_SVCTAB_H
#define COREAPP_SVCTAB_H

#include <stdint.h>
#include <stdbool.h>
#include "svc_fs.h"          // SvcFsEntryLong
#include "svc_sync.h"        // base 3.3: the groups of LibApi that follow `lib`
#include "svc_settings.h"
#include "svc_ctx.h"
#include "svc_io.h"
#include "svc_board.h"
#include "svc_overlay.h"   // base 3.3: api->overlay, the overlay service (T256)
#include "svc_wb.h"        // base 3.4: api->wb, the write-back service (T239.3 D51)

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
    // Base 3.3, the members of SvcSysAdd (svc_sys.h), in its order: a library tests `api->sys->size` before it calls one.
    uint16_t (*ms_timer)(void);
    void (*yield)(void);
    void (*delay_us)(uint32_t us);
    bool (*wall_time)(uint8_t t[6]);
    const char *(*app_version)(void);
    const char *(*app_git)(void);
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
// under /flash is refused (the paths of fs_flash_exception excepted: the host's boot.cfg and the standalone core's two files), and a read-write open there gives a read-only file. A file an
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
    // Base 3.3, the member of SvcFsAdd (svc_fs.h): the listing with whole names.
    int (*list_long)(const char *dir, SvcFsEntryLong *out, int max);
} SvcFs;

// What the base asks the person while the application runs (T104.3 D4): drawn by the base in the current skin, modal over whatever is on screen, answered
// with the cursor keys, RETURN and the C64's left arrow. A call blocks the calling task - never the base's own, so an application calls them from its own
// task or a job - until the person answered. RUN/STOP, ESC and the left arrow cancel. Without a screen the group is a null pointer.
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

// TCP for an application or a library that serves (the TRX64 runtime contract) or asks (the online search). Handles are sockets on the account of the image that
// opens them; every handle an application still holds when it ends is closed.
typedef struct SvcNet {
    uint32_t size;
    int (*listen)(uint16_t port, int backlog);        // a handle, or -5 when the port is taken or no socket is free
    int (*accept)(int listener);                      // -5: none waiting, never blocks
    int (*recv)(int h, void *buf, int len);           // 0: the peer closed, -5: nothing waiting, -3: broke
    int (*send)(int h, const void *buf, int len);     // all len bytes or -3
    int (*close)(int h);
    int (*wait)(const int *handles, int n, uint32_t timeout_ms);   // a mask, bit i for handles[i]; 0 on timeout
    int (*peer)(int h, char *buf, int len);
    // The outgoing half (P2-6a). Both calls block the caller (never from the menu task or the executor task).
    int (*connect)(const char *host, uint16_t port, uint32_t timeout_ms);   // a handle as listen gives it, or -5 (no handle free), -6 (the name does not resolve), -3 (refused or no answer within timeout_ms), -7 (no socket)
    int (*resolve)(const char *name, char *buf, int len);                   // the IPv4 address of a name (or of a dotted address) as "a.b.c.d" in buf; the length, or -1
    // Base 4.0 (it was SvcTrx::announce): the TCP port the core's runtime service listens on (the TRX64 runtime contract, TCP 4312). The base keeps it and its UDP
    // ident reply (port 64, a "json" request; netsvc.cc) reports it as `rpc` in the member `trx64`, beside the identity block's id and caps (trx_io_ident); /v1/info
    // does not report it. 0 takes it back. The port is on the account of the image that announced it: its release takes it back too (S14). Task: any.
    void (*announce)(uint16_t port);
} SvcNet;

// The tables of the exports a file uses (base 4.0, T239.3 D31, P2-6; libslot.h). need(n) is the table of the calling image's own needs entry number n: the file lists
// its needs in the order of LIB_NEEDS (APP_NEEDS), "base" included, and the numbers are fixed when the file is packed; that list is the file's slot list. The loader
// bound each entry once, when it loaded the file, to the export of that name at exactly that major of the libraries the app is linked with (libslot.h; base 4.0 loads
// no library file, D58). No name is looked up when the file runs, and nothing finds a table by name (lib->find is gone). A file whose entry no export answers is not
// loaded (the refusal names the file, the export and its major). Null (and *table_size 0) for a number past the list and for the entries "base", PRG_APP_NEED and
// CART_APP_NEED (libfmt.h). *table_size is the export's size (the first word of its table). The caller is the image
// whose code makes the call, as for api->mem: a table found here is used only by that image.
typedef struct SvcLib {
    uint32_t size;
    const void *(*need)(uint32_t n, uint32_t *table_size);
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
    const SvcNet *net;
    const SvcLib *lib;
    const SvcSync *sync;
    const SvcSettings *settings;
    const SvcCtx *ctx;
    const SvcIo *io;
    const SvcBoard *board;
    const SvcOverlay *overlay;
    const SvcWb *wb;
} LibApi;
#endif

#endif
