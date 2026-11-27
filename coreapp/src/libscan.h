// The libraries of a core application at its start (base 4.0, T239.3 D58): the base loads no library file. Every library is linked into the app through its TOML B
// (libtab.h); those with `register` (libraries from outside this repository) are registered here, in the order of their needs, each with the menu record its bundle
// carries for it (record 7 version 2 of the .cor, tools/cor_pack.py pack_libs). Pure: the file system is a CoreFs, the machine behind the loader is a LibHost, the log
// is a LibLog, so tests/coreapp/ runs the whole of it against a map of byte vectors and a model of the heap. One line is logged for each library:
//
//     [lib] example (linked) : ok
//     [lib] trx64u (linked) : refused: Library "trx64u" is refused: it declares the export "ring" 1 (its file, or its TOML B) and lib_register does not return it.
//
// It also holds the wait for the stick at the start (lib_wait_for_stick).
#ifndef COREAPP_LIBSCAN_H
#define COREAPP_LIBSCAN_H

#include "libload.h"
#include "fsif.h"
#include "desc.h"

#define LIB_MOUNT_WAIT_MS 4000          // lib_wait_for_stick: how long the start waits, at most, for a stick that is known to be coming
#define LIB_EXT ".u2a"                   // the extension of a file the loader reads (an application, apps folders: appindex.h)
#define LIB_LIST_MAX 64                  // directory entries read per folder
#define LIB_COR_MENUS_MAX 12             // menu records in record 7 (LIB_MAX: one per linked library at most)
#define LIB_REFUSALS_KEPT 4

// The roots of the sticks, null-terminated, in this order (the paths are case-insensitive in the file manager).
extern const char *const lib_roots[];

// What the wait for the stick asks of the machine: the clock, a pause, and the one fact the file system cannot tell - whether the USB stack
// has a mass-storage device attached or still enumerating (libboard.cc walks the tree from the root device).
class LibMountEnv {
public:
    virtual ~LibMountEnv() { }
    virtual bool usb_storage_attached() = 0;
    virtual uint32_t now_ms() = 0;
    virtual void delay_ms(uint32_t ms) = 0;
};

struct LibWaitResult {
    bool present;                        // a library root could be listed when the wait ended
    bool waited;                         // false: the decision was made without waiting
    uint32_t ms;                         // how long it took
    char why[96];                        // the decision, for the log: "no USB mass-storage device attached: not waiting"
};

// Waits for a stick root to list, but only when a stick is known to be coming. Decided in this order:
//   a root lists already                                       -> present, no wait
//   a root's name is in the listing of "/" (the USB stack installed the device, the medium is not attached yet) -> wait
//   the core's ROMs come from /flash and the USB stack has no stick installed -> no wait (nothing there to wait for)
//   the USB stack has no mass-storage device attached          -> no wait
//   otherwise (a mass-storage device is enumerating)           -> wait
// A wait ends when a root lists or after LIB_MOUNT_WAIT_MS.
void lib_wait_for_stick(CoreFs *fs, LibMountEnv *env, bool roms_in_flash, LibWaitResult *out);

// A LibHost that answers the capability question from the core's descriptor. The board's host and the benches' hosts derive from it and
// add the machine: the heap, the address, the cache, the call.
class DescLibHost : public LibHost {
public:
    explicit DescLibHost(const CoreDesc *d) : d_(d) { }
    bool has_capability(const char *capability) { return desc_has_capability(d_, capability); }
    const char *core_name() { return d_->cor_name; }

private:
    const CoreDesc *d_;
};

class LibLog {
public:
    virtual ~LibLog() { }
    virtual void line(const char *text) = 0;
};

struct LibScanResult {
    int loaded, refused;                 // linked libraries with `register`, and menu records of the bundle no such library takes
    int nkept;
    char kept[LIB_REFUSALS_KEPT][LIB_ERR_MAX];     // the first refusals as sentences, for the screen
    char summary[96];                    // one line for the Home page: "none", "2 loaded", "1 loaded, 1 refused: trx64u"
};

// The menu records of record 7 version 2 (tools/cor_pack.py pack_libs): "TRXL", u32 2, u32 count, then per entry char name[16] (the library's name, NUL-padded),
// u32 length, the menu record (descrec.h, libmenu.h), zeros to a multiple of 4. Each field is checked against the record before anything is read from it.
struct LibCorMenus {
    int n;
    char name[LIB_COR_MENUS_MAX][U2A3_NAME_MAX];
    uint32_t off[LIB_COR_MENUS_MAX], len[LIB_COR_MENUS_MAX];   // in the .cor
    bool taken[LIB_COR_MENUS_MAX];       // handed to a library (lib_link_cor)
};
// Reads the directory of record 7 at `rec_off`/`rec_size` of the .cor: 0, or -1 with the sentence in why (a version 1 record of library files is refused: D58).
int lib_cor_menus(CoreFs *fs, const char *cor_path, uint32_t rec_off, uint32_t rec_size, LibCorMenus *out, char *why, int whylen);

// The step that registers the linked libraries (libtab.h libtab_link on the board; a test gives its own): calls menu() for each library before it registers it, and done()
// after, with the loader's code and sentence.
typedef void (*LibLinkAll)(LibLoader &ld, bool (*menu)(void *user, const char *name, DescRecord **rec, uint8_t **bytes),
                           void (*done)(void *user, const char *name, int rc, const char *err), void *user);

// Registers the linked libraries with `register` through `link_all`, each with its menu record from record 7 of the bundle (rec_size 0: none carries one), and logs
// one line for each; a menu record that no linked library takes, or a record that cannot be read, is a refusal in the result. before_first(ctx) runs once, just before
// the first library registers (the crash guard arms there, libguard.h). Never fails as a whole; the result says what happened.
void lib_link_cor(LibLoader *ld, CoreFs *fs, const char *cor_path, uint32_t rec_off, uint32_t rec_size, LibLinkAll link_all, LibLog *log, LibScanResult *out,
                  void (*before_first)(void *), void *ctx);

#endif
