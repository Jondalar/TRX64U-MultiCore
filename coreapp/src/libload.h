// The loader of libraries and applications (T104.1 D10, D11, D12): reads a .u2a of format 3 (libfmt.h), checks everything it can check
// from the file before it spends memory, places the image in memory from the heap, applies the relocation table, checks the entry,
// and lets a library register what it contributes. Pure: the board's heap, the cache flush and the call into the image are the
// LibHost's, so the same file runs in tests/loader/ against a model of the heap and of the address space.
#ifndef COREAPP_LIBLOAD_H
#define COREAPP_LIBLOAD_H

#include <stdint.h>
#include "libfmt.h"
#include "libapi.h"
#include "ops.h"

// Where a file comes from: the flash, a stick, or a record of the .cor already in memory.
class LibSource {
public:
    virtual ~LibSource() { }
    virtual uint32_t size() = 0;
    virtual bool read(uint32_t offset, void *dst, uint32_t n) = 0;
};

// What the loader asks of the machine it runs on.
class LibHost {
public:
    virtual ~LibHost() { }
    virtual void *alloc(uint32_t n) = 0;                                  // from the heap; null: no room
    virtual void release(void *p) = 0;
    // The address the CPU running the image sees at byte p. The board: p itself. A model with its own address space: its own mapping.
    virtual uint32_t addr_of(const void *p) = 0;
    // The image was written as data and is about to run as code (an instruction cache has to be told).
    virtual void sync_code(void *p, uint32_t n) = 0;
    virtual bool has_capability(const char *capability) = 0;               // does the running core have it (T104 5.2)
    virtual const char *core_name() = 0;
    // The first instruction of the image: `lib_register(api, rec)` at `entry_addr`. The board casts the address and calls it.
    struct Entry {
        const char *name;                                                  // what the file provides
        uint32_t entry_addr;
        uint32_t image_addr;
        void *image;
    };
    virtual int call_register(const Entry &e, const LibApi *api, LibRecord *rec) = 0;
    // An application's first instruction: `app_main(api, ctx)` at `entry_addr` (T104.3 D2). A host that cannot run applications
    // keeps this default and every run is refused.
    virtual int call_main(const Entry &e, const LibApi *api, AppCtx *ctx)
    {
        (void)e; (void)api; (void)ctx;
        return LIB_HOST_CANNOT_RUN;
    }
    virtual uint32_t room() { return 0xFFFFFFFFu; }
    virtual uint32_t now_ms() { return 0; }
    virtual void delay_ms(uint32_t ms) { (void)ms; }
    // The lists of an application's blocks and files are touched by its tasks and the base's: a host with tasks gives a critical section.
    virtual void enter() { }
    virtual void leave() { }
    enum { LIB_HOST_CANNOT_RUN = -0x7FFF };
};

enum LibRc {
    LIB_OK = 0,
    LIB_BAD_FILE = -1,          // not a .u2a, the wrong format, a range outside the file or the image, a table that is not in order
    LIB_ABI = -2,
    LIB_NEEDS = -3,             // a dependency is missing or too old
    LIB_CAPABILITY = -4,        // the core lacks what the library serves
    LIB_NO_ROOM = -5,
    LIB_DUPLICATE = -6,         // something of that name is loaded already
    LIB_REGISTER = -7,          // lib_register failed or what it registered was refused
    LIB_STATE = -8,             // finish() of a resident image, or of a handle that is not loaded
    LIB_NOT_APP = -9            // run() of an image that has no app_main, or a host that cannot run one
};

#define LIB_MAX 12              // images loaded at once
#define LIB_ERR_MAX 240

struct DescRecord;
struct LibInfo {
    char name[U2A3_NAME_MAX];
    uint32_t version;
    uint8_t kind, residence;
    uint32_t image_addr, image_size;      // as the CPU sees it; image_size includes bss
    uint32_t main_addr;                   // an application's app_main; 0: none
    LibRecord record;                     // what lib_register returned (all zero for an image that has none)
    DescRecord *menu;                     // the menu record the file carries (libmenu.h), null: none; for the merge, with `record`
    uint8_t *menu_bytes;
};

class LibLoader {
public:
    // base_minor: the minor of the ABI this base offers (a library `needs base >= 3.x`). The tests lower it.
    LibLoader(LibHost *host, OpRegistry *reg, uint16_t base_minor = U2A_BASE_MINOR);
    ~LibLoader();

    // Loads the file. On success *handle is the slot; on failure everything the load took is given back, nothing stays registered,
    // and err says why in a sentence for the screen, the System page and the log (names the library and, for a version, both numbers).
    int load(LibSource *src, int *handle, char *err, int errlen);
    // An application that has run: a transient image is freed. A resident image, or a library, is never freed (LIB_STATE).
    int finish(int handle, char *err, int errlen);

    // Runs an application (T104.3 D2): app_main(api, ctx) with the action and the file it was opened with, on the calling task; `reply` gets the line it
    // left (may be null). *app_rc is what app_main returned. A transient application is freed afterwards with every block it still holds from
    // api->mem and every registration it made - unless `keep` is set (its screens are still up: the base calls release() when they close); a resident
    // one stays with its blocks.
    int run(int handle, const char *action, const char *file, char *reply, int replylen, int *app_rc, char *err, int errlen, bool keep = false);
    // A transient application kept open by run(): frees it. Its registrations go with it (the registry's drop_owner).
    int release(int handle, char *err, int errlen);
    // T197 D8: how many running applications need the image (a library). use() at open, unuse() when the application ends; a library with none may be evicted.
    void use(int handle);
    void unuse(int handle);
    int uses(int handle) const;
    // Frees a resident library nothing uses (its registrations go with it). LIB_STATE for an application, a handle not loaded, or a library in use.
    int unload(int handle, char *err, int errlen);

    // The context the operations of api->ops run with (the menu's CoreCtx); none: api->ops answers OPE_UNSUPPORTED.
    void set_ctx(CoreCtx *c) { ctx_ = c; }
    // The groups the base fills in itself (appsvc.cc): dialogs, jobs, the debug block, the network. Null: the application finds a null pointer.
    void set_dlg(const SvcDlg *t) { api_.dlg = t; }
    void set_job(const SvcJob *t) { api_.job = t; }
    void set_trx(const SvcTrx *t) { api_.trx = t; }
    void set_net(const SvcNet *t) { api_.net = t; }
    void set_c64(const SvcC64 *t) { api_.c64 = t; }
    // The bytes the heap can still give (the loader window of a board); a host with no such limit says "all". AppIndex asks it (T197 P4).
    uint32_t room() const { return host_->room(); }
    void set_sys_extra(int (*flash_read)(uint32_t, void *, int), void (*handover)(void)) { sys_.flash_read = flash_read; sys_.machine_handover = handover; }
    // Whose account the allocations and files of api->mem / api->fs go to from now on: the application whose tasks run (-1: nobody's). run() sets it
    // for the call; the base sets it for as long as an application's screens or jobs are up.
    void set_current(int handle) { cur_ = handle; }
    int current() const { return cur_; }

    // Blocks handed out by api->mem: to the application that runs (or registers) now, else to nobody (never given back by the loader).
    int account_blocks(int handle) const;     // handle -1: the blocks of nobody
    uint32_t account_bytes(int handle) const;
    int account_files(int handle) const;      // files opened through api->fs and not closed yet

    // Where a library's api->log goes (nothing, until this is called).
    void set_log(void (*fn)(const char *text));

    const LibInfo *info(int handle) const;
    int handle_of(const char *name) const;    // the slot of a loaded image by name, -1: not loaded
    int count() const { return count_; }
    uint32_t resident_bytes() const;      // heap held by images that stay, what Q7's budget counts
    // Always false since 2026-10-26: everything a record declares is served (the menu merge, contrib.h; the poll task, run.cc).
    static bool unserved_text(const char *name, const LibRecord &rec, char *out, int outlen);
    int unserved_count() const;           // always 0 (see above)

private:
    struct Slot {
        bool used;
        LibInfo info;
        int uses;                         // running applications that need it (T197 D8)
        void *raw;                        // what the heap gave; the image lies in it, aligned
    };
    LibHost *host_;
    OpRegistry *reg_;
    uint16_t base_minor_;
    LibApi api_;
    Slot slot_[LIB_MAX];
    int count_;

    struct Blk {                          // in front of every block of api->mem, kept to 16 bytes of alignment for what follows
        Blk *next, *prev;
        uint32_t size;
        int32_t owner;                    // the slot, or -1
    } __attribute__((aligned(16)));
    Blk *acct_[LIB_MAX + 1];              // the lists: one per slot, the last one is nobody's
    int cur_;                             // the slot whose code runs now, -1 none
    CoreCtx *ctx_;
    SvcSys sys_;
    SvcMem mem_;
    SvcOps ops_;
    SvcLib lib_;
    SvcFs fs_;
    SvcFile *files_[LIB_MAX + 1];         // the open files of api->fs, one list per slot as acct_

    static void *svc_malloc(uint32_t n);
    static void svc_free(void *p);
    static void *svc_realloc(void *p, uint32_t n);
    static int svc_call(const char *name, const OpIn *in, int nin, OpResult *r);
    static const char *svc_state(const char *name, uint32_t *rev);
    static const void *svc_lib_find(const char *name, uint32_t min_version, uint32_t *table_size);
    static void svc_log(const char *text);
    static uint32_t svc_ms(void);
    static void svc_sleep(uint32_t ms);
    static const char *svc_core_name(void);
    static bool svc_has_capability(const char *c);
    static SvcFile *svc_open(const char *path, bool rw);
    static bool svc_fread(SvcFile *f, uint64_t off, void *buf, uint32_t len);
    static bool svc_fwrite(SvcFile *f, uint64_t off, const void *buf, uint32_t len);
    static uint64_t svc_fsize(SvcFile *f);
    static bool svc_fwritable(SvcFile *f);
    static bool svc_fsync(SvcFile *f);
    static void svc_close(SvcFile *f);
    static int svc_list(const char *dir, SvcFsEntry *out, int max);
    static bool svc_stat(const char *path, uint64_t *size, bool *dir);
    static int svc_load(const char *path, void *buf, uint32_t max);
    static bool svc_save(const char *path, const void *buf, uint32_t len);
    static bool svc_remove(const char *path);
    static bool svc_mkdir(const char *path);
    static bool svc_rename(const char *from, const char *to);
    static CoreFs *fs_of();               // the core's CoreFs, null without a context
    void free_account(int si);
    Blk **list_of(int owner) { return &acct_[owner < 0 ? LIB_MAX : owner]; }

    void discard(int si);                 // gives the image of a slot back and empties it
    int fail(int rc, char *err, int errlen, const char *fmt, ...);
    const Slot *find(const char *name) const;
};

#endif
