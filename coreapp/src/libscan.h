// The libraries of a folder, loaded at the start of a core application (T104.1 D12: TRX64 Ultimate and the sticks of the MultiCore
// boards read them as files from one fixed folder). Pure: the file system is a CoreFs, the machine behind the loader is a LibHost, the log
// is a LibLog, so tests/coreapp/ runs the whole of it against a map of byte vectors and a model of the heap.
//
// What it does, in this order: lists LIB_FOLDER on each root; reads the header and the needs of every *.u2a; puts the files in dependency
// order (a provider before whoever needs it, ties by path, a cycle refused); loads them one by one through LibLoader::load; logs one line
// for each:
//
//     [lib] testlib v1.0 at 0x00a31230 : ok
//     [lib] cbm1541 v1.0 : refused: Library "cbm1541" needs the capability "drive_a", which the core "Atari 800 XL" does not have.
//
// A folder that does not exist is not an error. A file that is not a library, or not a file of this format, is refused with the loader's
// own sentence and does not stop the others.
#ifndef COREAPP_LIBSCAN_H
#define COREAPP_LIBSCAN_H

#include "libload.h"
#include "fsif.h"
#include "desc.h"

#define LIB_MOUNT_WAIT_MS 4000          // lib_wait_for_stick: how long the start waits, at most, for a stick that is known to be coming
#define LIB_FOLDER "trx/lib"             // the one place: <root>/trx/lib/<name>.u2a
#define LIB_EXT ".u2a"
#define LIB_SCAN_MAX 24                  // files looked at; more are named in the log and left alone
#define LIB_LIST_MAX 64                  // directory entries read per root
#define LIB_REFUSALS_KEPT 4

// The roots a library folder is looked for on, null-terminated, in this order (the sticks; the paths are case-insensitive in the file manager).
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

// What the loader of a file needs to know before it spends memory: from the header and the needs, nothing else.
struct LibMeta {
    bool valid;                          // magic, header size, format 3 and a name; a file that is not leaves the rest to the loader's sentence
    uint8_t kind;
    char name[U2A3_NAME_MAX];
    uint32_t version;
    int nneeds;
    char need[U2A3_NEEDS_MAX][U2A3_NAME_MAX];
};

// The order to load n files in. order[] receives indexes: first every library whose dependencies are in front of it (ties in the given
// order), then the libraries that lie behind a cycle (the loader refuses them with "not loaded"), then what is not a library or not valid
// (the loader or the caller refuses them). A library that is part of a cycle - or needs its own name - is not in order[]; cycle[i] is the
// lowest index of its group, -1 for everyone else. "base" is not a file and never an edge; a name no file provides is the loader's to refuse.
// Returns the number of entries in order[].
int lib_dep_order(const LibMeta *m, int n, int *order, int *cycle);

struct LibScanResult {
    int roots_found;                     // roots that have the folder
    int files;                           // *.u2a files seen
    int loaded, refused;
    int ignored;                         // files beyond LIB_SCAN_MAX
    int nkept;
    char kept[LIB_REFUSALS_KEPT][LIB_ERR_MAX];     // the first refusals as sentences, for the screen
    int unserved;                        // always 0: every declaration of a record is served
    char summary[96];                    // one line for the Home page: "none", "2 loaded", "1 loaded, 1 refused: cbm1541", "1 loaded, 1 with unserved parts"
};

// Loads the libraries of `folder` on each of `roots` into `ld`. Never fails as a whole; the result says what happened.
void lib_load_folder(LibLoader *ld, CoreFs *fs, const char *const *roots, const char *folder, LibLog *log, LibScanResult *out);
// The same, and before_first(ctx) runs once, just before the first library file is loaded (the crash guard arms there, libguard.h).
void lib_load_folder(LibLoader *ld, CoreFs *fs, const char *const *roots, const char *folder, LibLog *log, LibScanResult *out,
                     void (*before_first)(void *), void *ctx);

// The libraries a core bundle carries (record 7, T104.1 D12, Q5: a copy in every .cor that needs it): `rec_off`/`rec_size` is the record in the file
// (CorInfo off/size of COR_PART_LIBS). Same rules, same loader, same refusals as a folder; load these first, so a library of the bundle wins
// over a stick's of the same name (point 4 of 2026-10-20: one fixed winner, independent of the roots). rec_size 0: nothing to do.
void lib_load_cor(LibLoader *ld, CoreFs *fs, const char *cor_path, uint32_t rec_off, uint32_t rec_size, LibLog *log, LibScanResult *out,
                  void (*before_first)(void *), void *ctx);

#endif
