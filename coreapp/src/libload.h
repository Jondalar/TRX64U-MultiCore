// The loader of libraries and applications (T104.1 D10, D11, D12): reads a .u2a of format 3 (libfmt.h), checks everything it can check
// from the file before it spends memory, places the image in memory from the heap, applies the relocation table, checks the entry,
// and lets a library register what it contributes. Pure: the board's heap, the cache flush and the call into the image are the
// LibHost's, so the same file runs in tests/loader/ against a model of the heap and of the address space.
#ifndef COREAPP_LIBLOAD_H
#define COREAPP_LIBLOAD_H

#include <stdint.h>
#include "libfmt.h"
#include "libapi.h"
#include "c64_api.h"
#include "libpoll.h"
#include "ops.h"
#include "opscope.h"

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
    // Whose handler the calling task runs (the slot of an image, -1 none): one value per task, so that a handler on one task does not move the account of
    // another. A host with tasks keeps it by task; the default keeps one value for the host, which is right for one task.
    virtual int task_scope() { return task_scope_; }
    virtual void set_task_scope(int slot) { task_scope_ = slot; }
    virtual uint32_t now_ms() { return 0; }
    virtual void delay_ms(uint32_t ms) { (void)ms; }
    // The lists of an application's blocks and files are touched by its tasks and the base's: a host with tasks gives a critical section.
    virtual void enter() { }
    virtual void leave() { }
    // ---- base 3.3 (svc_sync.h, svc_sys.h, svc_board.h): what the machine gives a library beyond the heap. The defaults serve a host with one task and no board:
    // a mutex is one flag in a block of the heap (a second lock waits for nothing and times out), the rest answers "not there".
    virtual void *mutex_new()
    {
        uint8_t *p = (uint8_t *)alloc(16);
        if (p) {
            *p = 0;
        }
        return p;
    }
    // timeout_ms 0 tries once, SYNC_FOREVER (0xFFFFFFFF) waits without a limit, any other value waits that long (the board rounds up to its tick)
    virtual bool mutex_lock(void *m, uint32_t timeout_ms)
    {
        (void)timeout_ms;
        uint8_t *p = (uint8_t *)m;
        if (!p || *p) {
            return false;
        }
        *p = 1;
        return true;
    }
    virtual void mutex_unlock(void *m) { if (m) { *(uint8_t *)m = 0; } }
    virtual void mutex_free(void *m) { if (m) { release(m); } }
    virtual uint16_t ms_timer() { return (uint16_t)now_ms(); }            // the 1 ms hardware counter of the ITU, 16 bit
    virtual const char *app_version() { return ""; }                      // the version text of the application image, and its short git hash (gitinfo.h)
    virtual const char *app_git() { return ""; }
    virtual void yield() { }
    virtual uint32_t fpga_caps() { return 0; }                            // the ITU capability register
    virtual bool is_commodore() { return false; }
    virtual int esp32_rpc(uint8_t cmd, const uint8_t *args, int nargs, uint8_t *reply, int maxreply, int timeout_ms)
    {
        (void)cmd; (void)args; (void)nargs; (void)reply; (void)maxreply; (void)timeout_ms;
        return -3;                                                        // no link
    }
    virtual void video_set_system(struct CoreCtx *c, int sys) { (void)c; (void)sys; }
    virtual void video_set_pll(int sys, int ppm) { (void)sys; (void)ppm; }
    // The SID sockets' expander (0x40 on the 1.8 V channel): open takes the bus lock "SID Configuration" and picks the channel (1: open; 0: the board has no bus,
    // so no expander; -1: the lock was not taken in time), write is one register, close gives the lock back (after a 1 only).
    virtual int sid_i2c_open() { return 0; }
    virtual void sid_i2c_write(uint8_t reg, uint8_t v) { (void)reg; (void)v; }
    virtual void sid_i2c_close() { }
    enum { LIB_HOST_CANNOT_RUN = -0x7FFF };

protected:
    LibHost() : task_scope_(-1) { }

private:
    int task_scope_;
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
    LIB_NOT_APP = -9,           // run() of an image that has no app_main, or a host that cannot run one
    LIB_BUDGET = -10            // the image, the heap or the registrations are over the budget the file declares, or over the limit of a table (T239.3 S13)
};

#define LIB_MAX 12              // images loaded at once
#define LIB_NET_MAX 16          // sockets of api->net held at once (the base's own table is as big)
#define LIB_JOB_MAX 8           // jobs of api->job held at once, ended ones not yet joined included (the base has 4 slots)
#define LIB_BASE_EXPORTS_MAX 16 // the tables the base itself answers find with (add_export)
#define LIB_ERR_MAX 240

struct DescRecord;
struct CoreCtx;
class CoreIo;
// One mutex of api->sync (svc_sync.h): the host's own object and the place in the account of the library that made it.
struct SvcMutex {
    void *h;
    SvcMutex *next, *prev;
    int32_t owner;
};
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

// The names and majors a file exports (header flag U2A3_FLAG_EXPORTS), at most U2A3_EXPORTS_MAX into `out`: how many (0: none), -1 when the record is malformed. Needs only the header,
// so the order of a folder (libscan.cc) and the list of the apps folder (appindex.cc) know what a file provides before it is loaded.
int lib_read_exports(LibSource *src, const U2a3Header &h, U2a3ExportName *out);

class LibLoader : private OpScope {
public:
    // base_minor: the minor of the ABI this base offers (a library `needs base >= 3.x`). A loader that does not call add_services has the five groups of base 3.3 null
    // and passes 2 (base 3.2), so that an image which needs base>=3.3 is refused; the host application's zip reader does. The tests lower it too.
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
    // T197 D8: how many loaded images need this one (a library): load() takes a use on every library the image needs and discard() gives it
    // back, so a resident application, or a library imported by a loaded library, keeps its library; a library with none may be evicted.
    void use(int handle);
    void unuse(int handle);
    int uses(int handle) const;
    // True when the library declares anything the base serves from the library's memory: a poll, menu nodes, file actions, settings or tabs, in its
    // lib_register record or in its menu record, or exports the table "corehooks" (hookhost.h). Such a library is never evicted (AppIndex::prepare_open): the poll would call into freed code and the
    // merged menu would point into freed strings.
    bool contributes(int handle) const;
    // Frees a resident library nothing uses (its registrations go with it). LIB_STATE for an application, a handle not loaded, a library in use, or one with a job that still runs (the jobs are asked
    // to stop, none is waited for: the caller holds the core lock, which a job may need to end).
    int unload(int handle, char *err, int errlen);

    // Where the poll a library declares is served (T239.3 S3): a library that registers after this is called has its poll added to the sink, and the
    // poll ends when the library is released or unloaded. A poll the sink refuses refuses the library, and so does a poll in lib_register and in the menu record. Null: no poll is served (the libraries
    // loaded before the core starts get theirs from the menu merge, run.cc, which calls this afterwards).
    void set_poll_sink(LibPollSink *s) { polls_ = s; }
    // The call tables libraries export, as api->lib hands them to a library: the base asks it for the table "corehooks" (hookhost.h).
    const SvcLib *svc_lib() const { return &lib_; }
    // Told the slot of an image, and the range it lies in (as the CPU sees it), just before its memory is given back (an unload, a release, a load that fails after the image was
    // placed): what the base kept of that image (a table the image's code gave it) is let go there. Null: nobody is told.
    void set_discard_notify(void (*fn)(int handle, uint32_t image_addr, uint32_t image_size)) { discard_notify_ = fn; }

    // The context the operations of api->ops run with (the menu's CoreCtx); none: api->ops answers OPE_UNSUPPORTED. It is also the core that the groups settings, ctx, io
    // and board of base 3.3 act on; api->io is null for a context without a register window (no io, or a core with no machine: CoreDesc::menu_first).
    void set_ctx(CoreCtx *c);
    // Base 3.3 (libsvc.cc, svc_*.h): fills the groups sync, settings, ctx, io and board of api. The core application calls it once, when it makes the loader (libboard.cc), and links
    // libsvc.cc; a loader of another application (the host's zip reader) does not, and its api has those five groups null. The members 3.3 added to api->sys and api->fs are in every loader.
    void add_services();
    // How a setting's value is put into the core (SvcSettings::apply): the application hands over regset_apply_setting (regset.h). Null: apply does nothing.
    void set_settings_apply(void (*fn)(CoreCtx *, int)) { apply_ = fn; }
    // The state "net.gated" (svc_settings.h): true while the REST token gate is on. Null: never gated.
    void set_gate_probe(bool (*fn)(void)) { gated_ = fn; }
    // The groups the base fills in itself (appsvc.cc): dialogs, jobs, the debug block, the network. Null: the application finds a null pointer.
    void set_dlg(const SvcDlg *t) { api_.dlg = t; }
    // job and net (base 3.3, S14): in a loader that has the services (add_services) a library and an application get the loader's own tables, which put every socket and every job on
    // the account of the image whose code asked and call the table given here; one of them given before or after add_services is the same, and with none given the calls fail (a socket
    // -7, a job null). A loader without the services hands the table on as it is.
    void set_job(const SvcJob *t) { base_job_ = t; api_.job = services_ ? &job_ : t; }
    void set_trx(const SvcTrx *t) { api_.trx = t; }
    void set_net(const SvcNet *t) { base_net_ = t; api_.net = services_ ? &net_ : t; }
    // What the base does to a job that its owner has to give up: sets the cancel flag the job's `cancelled()` reads (apphost.cc). Null: jobs cannot be asked to stop.
    void set_job_cancel(void (*fn)(struct SvcJobH *)) { job_cancel_ = fn; }
    void set_c64(const SvcC64 *t) { api_.c64 = t; }
    // The tables the base itself answers api->lib->find with (base 3.2: "c64"; base 3.3: c64cart, cbm1541, uci, sid, tempdisk, textgrid): a name, the major of the table and the table, whose first
    // member is its size (uint32_t). A table with the name and major of an earlier one replaces it; a null table takes the entry away. `name` is kept, not copied (a string literal). find answers
    // such a table unless a loaded library exports the name at a lower or equal major. False: the list is full (LIB_BASE_EXPORTS_MAX).
    bool add_export(const char *name, uint32_t major, const void *table);
    // The table the base itself offers under the export name C64_API_NAME (c64_api.h, base 3.2). Null: the base has none (the core is no C64) and find answers only what libraries export.
    void set_c64_api(const C64Api *t) { add_export(C64_API_NAME, C64_API_MAJOR, t); }
    // What api->lib->find answers for `name` at the major of min_version or later: the same call a library makes, for the code of the base (the screens that draw a library's TextGrid).
    static const void *find_export(const char *name, uint32_t min_version, uint32_t *table_size) { return svc_lib_find(name, min_version, table_size); }
    // The machine's CoreIo for the two calls of the board that need no core context: sys.delay_us (io.delay_us) and sys.wall_time. The core application gives it at the start
    // (libboard.cc), before the first library registers, so a wait in lib_register waits; once set_ctx has run, the context's io is the one used.
    void set_board_io(CoreIo *io) { board_io_ = io; }
    // The bytes the heap can still give (the loader window of a board); a host with no such limit says "all". AppIndex asks it (T197 P4).
    uint32_t room() const { return host_->room(); }
    void set_sys_extra(int (*flash_read)(uint32_t, void *, int), void (*handover)(void)) { sys_.flash_read = flash_read; sys_.machine_handover = handover; }
    // Whose account the allocations and files of api->mem / api->fs go to from now on: the application whose tasks run (-1: nobody's). run() sets it
    // for the call; the base sets it for as long as an application's screens or jobs are up. That value is the one of every task. A handler of a loaded
    // image the registry runs (OpScope, enter/leave below) has the image as its owner for the calling task only, and the task's owner comes first.
    void set_current(int handle) { cur_ = handle; }
    int current() const { return owner(); }

    // Blocks handed out by api->mem: to the image whose code runs now (it registers, one of its handlers runs, or it is the application that is open), else
    // to nobody (never given back by the loader).
    int account_blocks(int handle) const;     // handle -1: the blocks of nobody
    uint32_t account_bytes(int handle) const;
    int account_files(int handle) const;      // files opened through api->fs and not closed yet
    int account_sockets(int handle) const;    // sockets of api->net not closed yet (base 3.3)
    int account_jobs(int handle) const;       // jobs of api->job started and not joined yet (a job that has ended counts until it is joined or its slot is used again)
    // What one image holds of the machine (S14); the base calls these when an application ends (apphost.cc) and discard() calls them when an image goes. Nothing of any other image is touched.
    void close_sockets(int handle);           // closes the sockets of the account
    void cancel_jobs(int handle);             // asks the jobs of the account to stop (set_job_cancel)
    bool jobs_running(int handle) const;      // does one of them run
    void reap_jobs(int handle);               // joins the jobs of the account that have ended, so their slots are free
    bool stop_jobs(int handle);               // cancel, wait up to 5 s, reap; false: one still runs
    // Runs the code of an image as its owner: for the calls whose code lies in no loaded image (the base, a core hook it makes), the calling task takes everything api->mem, fs, sync, net and
    // job give from here to the end of the object on the account of `handle`; a call made by code inside a loaded image is always on that image's account (owner_at). The base puts one around a call into a core hook or an export table that it makes itself; no-op for a handle that is not loaded.
    class Scope {
    public:
        Scope(LibLoader *l, int handle);
        ~Scope();
    private:
        LibLoader *l_;
        int prev_;
        bool on_;
    };

    // Where a library's api->log goes (nothing, until this is called).
    void set_log(void (*fn)(const char *text));

    const LibInfo *info(int handle) const;
    int handle_of(const char *name) const;    // the slot of a loaded image by name, -1: not loaded
    // The slot of the loaded library that provides `name` at the major of `min_version` or later: one that exports the name (LibExportDecl), else the library of that
    // name at that version or later - what a `needs` entry resolves against (D20). -1: none.
    int handle_providing(const char *name, uint32_t min_version) const;
    int count() const { return count_; }
    uint32_t resident_bytes() const;      // heap held by the images that stay (image and bss), what Q7's budget counts
    // The whole account of one image (T239.3 S14): everything the heap gave for it and has not got back - the image with its bss and alignment, the blocks of api->mem
    // with their headers, the handles of api->fs, the menu record the file carries. handle -1: what nobody's account holds. Unloading or releasing the image returns
    // all of it; the heap free before the load is the heap free after.
    uint32_t held_bytes(int handle) const;
    uint32_t held_total() const;          // the accounts of every loaded image and of nobody together
    // Always false: everything a record declares is served (the menu merge, contrib.h; the poll, set_poll_sink above).
    static bool unserved_text(const char *name, const LibRecord &rec, char *out, int outlen);
    int unserved_count() const;           // always 0 (see above)

private:
    struct Slot {
        bool used;
        LibInfo info;
        int uses;                         // loaded images that need it (T197 D8)
        int dep[U2A3_NEEDS_MAX];          // the slots this image counted as used when it loaded; unuse()d when it goes
        int ndep;
        void *raw;                        // what the heap gave; the image lies in it, aligned
        uint32_t raw_size;                // the bytes asked for it (image, bss, alignment)
        uint32_t menu_cost;               // the bytes of the menu record (the record and its bytes, from the heap by new)
        uint32_t heap_limit;              // the declared heap peak (U2A3_BUDGET_NONE: none): the most api->mem holds for this image at once
        uint32_t blk_bytes;               // what its blocks hold now (the sizes asked for), the figure the limit is held against
        bool heap_denied;                 // an allocation was refused for the limit; the first one is told (heap_want, heap_had)
        uint32_t heap_want, heap_had;
    };
    LibHost *host_;
    OpRegistry *reg_;
    uint16_t base_minor_;
    LibApi api_;
    LibPollSink *polls_;
    Slot slot_[LIB_MAX];
    int count_;

    struct Blk {                          // in front of every block of api->mem, kept to 16 bytes of alignment for what follows
        Blk *next, *prev;
        uint32_t size;
        int32_t owner;                    // the slot, or -1
    } __attribute__((aligned(16)));
    Blk *acct_[LIB_MAX + 1];              // the lists: one per slot, the last one is nobody's
    void (*discard_notify_)(int handle, uint32_t image_addr, uint32_t image_size);
    int cur_;                             // the slot of the application that is open (and of an image's lib_register or app_main as run() calls it), -1 none
    CoreCtx *ctx_;
    SvcSys sys_;
    SvcMem mem_;
    SvcOps ops_;
    SvcLib lib_;
    SvcFs fs_;
    SvcFile *files_[LIB_MAX + 1];         // the open files of api->fs, one list per slot as acct_
    // base 3.3 (libsvc.cc): the groups the base fills for every library and application
    SvcSync sync_;
    SvcSettings settings_;
    SvcCtx ctxtab_;
    SvcIo io_;
    SvcBoard board_;
    SvcMutex *mus_[LIB_MAX + 1];          // the mutexes of api->sync, one list per slot as acct_
    void (*apply_)(CoreCtx *, int);
    bool (*gated_)(void);
    uint8_t sid_ctrl_;                    // the control byte of the two SID sockets (board.sid_socket)
    friend struct LibSvc;
    bool services_;                       // add_services ran
    const char *(*extra_state_)(CoreCtx *, const char *);   // libsvc.cc: "set.<key>" and "net.gated", before the registry
    void free_mutexes(int si);
    uint32_t mutex_bytes(int a) const;

    // OpScope (opscope.h): the registry runs a library's handler; its allocations are that library's from here to leave()
    int enter(int slot) override;
    void leave(int prev) override { host_->set_task_scope(prev); }
    int owner() const;                    // the account of the calling task: its handler's image, else cur_
    int owner_at(const void *ra) const;   // the account of a service call made from the code at `ra`: the image that holds it, else owner()
    void *alloc_for(int owner, uint32_t n);
    bool over_budget(const char *name, const U2a3Budget &b, const LibRecord *rec, const LibRecord *menu, char *err, int errlen) const;
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
    static int svc_list_long(const char *dir, SvcFsEntryLong *out, int max);
    static uint16_t svc_ms_timer(void);
    static void svc_yield(void);
    static void svc_delay_us(uint32_t us);
    static bool svc_wall_time(uint8_t t[6]);
    static const char *svc_app_version(void);
    static const char *svc_app_git(void);
    static bool svc_stat(const char *path, uint64_t *size, bool *dir);
    static int svc_load(const char *path, void *buf, uint32_t max);
    static bool svc_save(const char *path, const void *buf, uint32_t len);
    static bool svc_remove(const char *path);
    static bool svc_mkdir(const char *path);
    static bool svc_rename(const char *from, const char *to);
    static CoreFs *fs_of();               // the core's CoreFs, null without a context
    static LibLoader *self();             // the one loader of the application (the service functions carry no context)
    void free_account(int si);
    Blk **list_of(int owner) { return &acct_[owner < 0 ? LIB_MAX : owner]; }

    void retire(const Slot &s);           // the library goes: its poll ends, then its operations, state values and events
    void discard(int si);                 // gives the image of a slot back and empties it
    int fail(int rc, char *err, int errlen, const char *fmt, ...);
    const Slot *find(const char *name) const;
    // The loaded library that exports `name` with the lowest major that is at least the major of min_version (*ex is that entry); *below is the highest major of the name that is
    // lower, 0 when none (a refusal can say "1.0 is loaded"). Null: no library exports it at that major.
    const Slot *provider(const char *name, uint32_t min_version, const LibExportDecl **ex, uint32_t *below) const;
    bool exports_bad(int si, const LibRecord &rec, const U2a3ExportName *file, int nfile, char *why, int whylen) const;

    // sockets and jobs (S14): what api->net and api->job handed out, with the account. Fixed tables: the base's own tables are as small (16 sockets, 4 jobs).
    struct NetRec {
        int h;
        int32_t owner;
        bool used;
    };
    struct JobRec {
        struct SvcJobH *h;                // null while the base has not returned it
        int32_t owner;
        bool used;
        int (*fn)(struct SvcJobH *, void *);
        void *user;
    };
    NetRec nets_[LIB_NET_MAX];
    JobRec jobs_[LIB_JOB_MAX];
    SvcNet net_;
    SvcJob job_;
    const SvcNet *base_net_;
    const SvcJob *base_job_;
    void (*job_cancel_)(struct SvcJobH *);
    CoreIo *board_io_;
    struct BaseExport {
        const char *name;
        uint32_t major;
        const void *table;
    };
    BaseExport base_[LIB_BASE_EXPORTS_MAX];
    int nbase_;
    const BaseExport *base_export(const char *name, uint32_t want_major) const;   // the lowest major of the name that is at least want_major
};

#endif
