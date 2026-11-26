// What a library or an application sees of the base, and what it hands back (T104.1 D4, D10.2; T104.3 D2). A library's entry is
//
//     extern "C" int lib_register(const LibApi *api, LibRecord *out);
//
// It fills `out` and returns 0; anything else refuses the load and nothing of it stays. The record's pointers point into the image
// (relocated by then) or at the base; the loader copies the record, so `out` may live on the stack. A library is native code
// compiled against this header: the record's struct_size has to match the loader's, or the load is refused.
//
// An application's entry is
//
//     extern "C" int app_main(const LibApi *api, AppCtx *ctx);
//
// The base calls it on a task of its own, with the action and the file of the menu entry that opened the application; when it returns, a transient
// application with no screen open is freed with everything it still holds from api->mem. An application that brings screens (its menu record has a
// route of its own, T104.3 D0) also has `lib_register`, which registers the operations and state values its screens bind to; the base calls it first,
// and takes the registrations back when the application is freed.
//
// Of the six lists of D4 the loader registers operations, state values and events (the three the registry has). Menu nodes, file actions
// and settings, and the tabs, are merged into the menu by contrib.h (D8); the poll is run by the service task (service.h LibPolls) from the moment
// the library has registered until it is released or unloaded.
#ifndef COREAPP_LIBAPI_H
#define COREAPP_LIBAPI_H

#include <stddef.h>
#include <stdint.h>
#include "ops.h"
#include "fsif.h"
#include "svctab.h"

// The service tables (svctab.h, T104.3 D2) are plain structs; LibApi is the root a library or an application gets. A group the base or the core does not
// have is a null pointer.
//
// Base 4.0 (T239.3 P2-6, D57): the groups trx (SvcTrx, with its SvcRing) and c64 (SvcC64) are gone, and so is lib->find. What they gave is the core's own (its TOML A
// and its libraries, D20, D53) or a group that stays:
//   - the C64's memory by DMA (c64 peek, poke, dma_read, dma_write): io->ram_read and io->ram_write on the base RAM window, the block `dma` (svc_io.h; the same
//     window and the same byte loop, hw.cc HwIo::ram_read/ram_write), with io->ram_memonly as before;
//   - c64 cart_mode, cart_setup_bus, cart_restore_bus: io->cart_wr, io->bus_rd/bus_wr, io->ram_read/ram_write, io->ram_memonly, by the core's own code;
//   - c64 cart_detect: the property cart_detect of the block u64_sys of the core's TOML A, through its driver table (drvtab.h, D53);
//   - trx regs, caps, acquire, release, video and ring: the core's own blocks (ident, trx_dbg_run, trx_debug) through its driver table, and its ring library's
//     export "ring";
//   - trx announce: net->announce (svctab.h SvcNet);
//   - trx reset_c64: ops->call("machine.warm_reset", ...), the operation it called;
//   - lib->find: the slots of libslot.h (linked code) and lib->need (a file).
// A library or an application of major 3 is refused (libfmt.h U2A_ABI_MAJOR).
struct LibApi {
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
    const SvcLib *lib;                    // the tables of the calling file's needs entries (base 4.0: need only; libslot.h)
    // the generic application's own services, each table size first. io is null on a core without a register window.
    const SvcSync *sync;
    const SvcSettings *settings;
    const SvcCtx *ctx;
    const SvcIo *io;
    const SvcBoard *board;
    const SvcOverlay *overlay;            // svc_overlay.h: the overlay service; null in a loader without a core
    const SvcWb *wb;                      // svc_wb.h: the write-back service for libraries (D51); null in a loader without a core
};
// Does a LibApi of this struct_size have the member? A library built against a header with a group that a later minor of 4 appends, and that must also run on a base before
// that minor, tests `LIB_API_COVERS(api, member)`.
#define LIB_API_COVERS(api, member) ((api)->struct_size >= offsetof(LibApi, member) + sizeof(((const LibApi *)0)->member))
// Does a call table of a group (an Svc* table, `size` first) have the call `field`, set? A library built against a header with a newer call that must also run on an
// older base tests SVC_HAS(api->overlay, goto_screen) before the call (the form of U2P_HAS in u2p_app.h).
#define SVC_HAS(t, field) ((t) && ((t)->size >= offsetof(__typeof__(*(t)), field) + sizeof((t)->field)) && (t)->field)
// The app ABI: SvcFsEntry is frozen at 72 bytes (a 56 byte name); FsEntry carries the whole long name and the bridge (LibLoader::svc_list) converts.
typedef char svc_fsentry_layout_check[(sizeof(SvcFsEntry) == 72 && sizeof(((SvcFsEntry *)0)->name) == 56) ? 1 : -1];
// Base 3.3 list_long: SvcFsEntryLong is FsEntry, member for member, so the base hands CoreFs::list the caller's array as it is.
typedef char svc_fsentrylong_layout_check[(sizeof(SvcFsEntryLong) == sizeof(FsEntry) && sizeof(((SvcFsEntryLong *)0)->name) == FS_NAME_MAX + 1 &&
                                           offsetof(SvcFsEntryLong, size) == offsetof(FsEntry, size) && offsetof(SvcFsEntryLong, attr) == offsetof(FsEntry, attr)) ? 1 : -1];

// What an application is opened with (T104.3 D6): the action of the menu entry and the file it was on, and room for one line it may
// leave as its result (the screen's status line, or a transport's answer).
struct AppCtx {
    uint32_t struct_size;
    const char *action;                   // "" when none
    const char *file;                     // "" when none
    char *reply;
    int reply_len;
};

typedef int (*app_main_fn)(const LibApi *api, AppCtx *ctx);

struct LibOpDecl {
    const char *name;
    const ArgSpec *args;
    int nargs;
    op_fn fn;
};
struct LibStateDecl {
    const char *name;
    state_fn get;
};
struct LibEventDecl {
    const char *name;
    const ArgSpec *payload;               // null: none
};

// LN_LIST (base 3.5, T262 P1): a row that opens the rows of the state `state` as a list; `op` is called with the index of the row the person takes (api.md 6.19). Not accepted in a library of a base before 3.5.
enum LibNodeKind { LN_OP = 0, LN_SETTING = 1, LN_STATE = 2, LN_WIDGET = 3, LN_GROUP = 4, LN_LIST = 5 };
struct LibNodeDecl {
    const char *anchor;                   // "F5/drives/drive_a": area key / tab id / group (D8)
    uint8_t kind;
    const char *label;
    const char *op;                       // the operation a button calls
    const char *state;                    // the state value a line shows
    int16_t order;
    const char *when_state;               // "drive.drive_a.mode != empty": the row is offered only while it holds (contrib.h cond_holds)
    const char *capability;
    // The tail was added with the menu merge (T104.1 D8); a record that leaves it zero has no id of its own (the id is then the operation or the
    // state), binds nothing and does not close the menu.
    const char *id;                       // what the description hides or renames the node by
    const OpBind *bind;                   // an op node fills every argument of its operation from these (no $file: a node has no file)
    int nbind;
    bool closes_menu;
    // T104.2 D10 (screens): how the node is shown, "key;key=value;..." (ui_attr in contrib.h): primary, link, goto=<F3 or F5/drives>, sub=<value>,
    // chip=<value>, tone=<muted|cyan|violet|amber|coral>, off=<the reason it is greyed>, why=<the reason while its when_state does not hold>. Null: a plain row. The anchor's group names the slot of the tab's layout.
    const char *attr;
};
struct LibFileActionDecl {
    const char *const *ext;               // ".d64", ".g64"
    int next;
    const char *label;
    const char *op;
    const OpBind *bind;
    int nbind;
    const uint32_t *size_in;              // optional set of sizes
    int nsize_in;
    bool closes_menu;
};
struct LibSettingDecl {
    const char *group;
    const char *key;
    const char *const *values;            // a list of labels, or null with lo..hi
    int nvalues;
    int32_t lo, hi;
    int32_t def;
    const char *apply;                    // the operation that takes `value`
};
// One poll for the record: `op` is called every period_ms (at least 1) while the library is loaded and the core runs. `op` is registered by the
// library or by the base and takes no arguments; the same poll may stand in the menu record instead, not in both (the library is refused). A poll the base cannot serve refuses a
// library loaded after the core has started; one loaded before loses its poll and the log says why (the menu merge). The poll does not run while the machine is frozen
// by the menu unless the library declares when_frozen (base 3.3): then it keeps running with the menu open, beside the executor and the core hooks (core_hooks.h).
struct LibPollDecl {
    const char *op;                       // null: no poll
    uint16_t period_ms;
    bool when_frozen;                     // true: the poll also runs while the machine is frozen for the menu; false: it waits for the menu to close
};

// A tab a contributor adds to an area of the route (T104.1 D8, Alex 2026-10-26): appended after the tabs the area has, with the next free printed number
// unless `n` says one; a refusal by name when the area is not on the route, the id is taken, or the area has eight tabs (MENU_STRIP_TABS). `widget` is a TabWidget of menutree.h.
struct LibTabDecl {
    const char *area;                     // "F5"
    const char *id;                       // lower case, [a-z0-9_]
    const char *name;
    uint8_t widget;
    const char *groups;                   // widget settings: the settings groups it lists, comma separated; else null
    uint8_t n;                            // 0: the next free number
};

// One table of a file under a name (base 3.2): a file may export several, and what needs a table names the export and its major, not the library that provides it.
// `table` is a table of function pointers in the image (or a block the file keeps), `size` its size in bytes (at least 4; the table starts with a uint32_t `size` of
// its own, and a caller checks it before it uses a member). Within a major the table is only appended to. The name is 1 to 15 characters of [a-z0-9_] and is not the
// name of an export of the same major of the app (libslot.h) or of another loaded file; the major is at least 1. The same names, with their majors, stand in the file
// (U2A3_FLAG_EXPORTS, libfmt.h), which is what orders the load of a folder. Base 4.0: the loader binds an export to the needs entries of the files that need it when
// they load (SvcLib::need); the base reads a sub-app's export by its name (the file manager's "online", online_api.h). No call of LibApi finds it by name.
struct LibExportDecl {
    const char *name;
    uint32_t major;
    const void *table;
    uint32_t size;
};
#define LIB_EXPORTS_MAX 8                // at most U2A3_EXPORTS_MAX entries in a record

struct LibRecord {
    uint32_t struct_size;
    const LibOpDecl *ops;                 int nops;
    const LibStateDecl *states;           int nstates;
    const LibEventDecl *events;           int nevents;
    const LibNodeDecl *nodes;             int nnodes;
    const LibFileActionDecl *file_actions; int nfile_actions;
    const LibSettingDecl *settings;       int nsettings;
    LibPollDecl poll;
    const LibTabDecl *tabs;               int ntabs;
    // The named tables of the file (LibExportDecl above, T239.3 D20). Base 4.0: the one way a file exports a table; the unnamed table of 3.1 (`exports`, `exports_size`,
    // found by the library's name) is gone with lib->find.
    const LibExportDecl *export_list;
    int nexport_list;
    // T239.3 L-3: how the tabs of `tabs` are drawn: one string for each tab, in the order of `tabs`, as a route's own tab says it ("split;title=...;side=...", tools/descrec.py UI_TAB_KEYS);
    // a null entry, or a null array, leaves the tab a flat list.
    const char *const *tab_layouts;
};
// Does a record of this struct_size have the member? (a record of an older base ends before it)
#define LIB_RECORD_COVERS(size, member) ((size) >= offsetof(LibRecord, member) + sizeof(((const LibRecord *)0)->member))

typedef int (*lib_register_fn)(const LibApi *api, LibRecord *out);

#endif
