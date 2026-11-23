#include "libload.h"
#include "libmenu.h"
#include "ctx.h"
#include "contrib.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void no_log(const char *) { }

// The service table's functions are plain C pointers without a context: they reach the one loader of the application through this.
static LibLoader *s_ld;
LibLoader *LibLoader::self() { return s_ld; }

LibLoader::LibLoader(LibHost *host, OpRegistry *reg, uint16_t base_minor)
    : host_(host), reg_(reg), base_minor_(base_minor), polls_(0), count_(0), cur_(-1), ctx_(0), apply_(0), gated_(0), sid_ctrl_(0), services_(false), extra_state_(0), c64api_(0)
{
    memset(slot_, 0, sizeof(slot_));
    memset(acct_, 0, sizeof(acct_));
    memset(files_, 0, sizeof(files_));
    memset(mus_, 0, sizeof(mus_));
    s_ld = this;
    reg_->set_scope(this);
    sys_.size = sizeof(sys_);
    sys_.log = svc_log;
    sys_.ms = svc_ms;
    sys_.sleep_ms = svc_sleep;
    sys_.core_name = svc_core_name;
    sys_.has_capability = svc_has_capability;
    sys_.flash_read = 0;
    sys_.machine_handover = 0;
    mem_.size = sizeof(mem_);
    mem_.malloc = svc_malloc;
    mem_.free = svc_free;
    mem_.realloc = svc_realloc;
    ops_.size = sizeof(ops_);
    ops_.call = svc_call;
    ops_.state = svc_state;
    lib_.size = sizeof(lib_);
    lib_.find = svc_lib_find;
    fs_.size = sizeof(fs_);
    fs_.open = svc_open;
    fs_.read = svc_fread;
    fs_.write = svc_fwrite;
    fs_.fsize = svc_fsize;
    fs_.writable = svc_fwritable;
    fs_.sync = svc_fsync;
    fs_.close = svc_close;
    fs_.list = svc_list;
    fs_.stat = svc_stat;
    fs_.load = svc_load;
    fs_.save = svc_save;
    fs_.remove = svc_remove;
    fs_.mkdir = svc_mkdir;
    fs_.rename = svc_rename;
    sys_.ms_timer = svc_ms_timer;                       // base 3.3 (svc_sys.h, svc_fs.h)
    sys_.yield = svc_yield;
    sys_.delay_us = svc_delay_us;
    sys_.wall_time = svc_wall_time;
    sys_.app_version = svc_app_version;
    sys_.app_git = svc_app_git;
    fs_.list_long = svc_list_long;
    memset(&sync_, 0, sizeof(sync_));
    memset(&settings_, 0, sizeof(settings_));
    memset(&ctxtab_, 0, sizeof(ctxtab_));
    memset(&io_, 0, sizeof(io_));
    memset(&board_, 0, sizeof(board_));
    api_.struct_size = sizeof(api_);
    api_.abi_major = U2A_ABI_MAJOR;
    api_.abi_minor = base_minor;
    api_.log = no_log;
    api_.sys = &sys_;
    api_.mem = &mem_;
    api_.ops = &ops_;
    api_.fs = &fs_;
    api_.dlg = 0;
    api_.job = 0;
    api_.trx = 0;
    api_.net = 0;
    api_.c64 = 0;
    api_.lib = &lib_;
    api_.sync = 0;                                      // add_services
    api_.settings = 0;
    api_.ctx = 0;
    api_.io = 0;                                        // and set_ctx: a context with a register window
    api_.board = 0;
}

void LibLoader::set_ctx(CoreCtx *c)
{
    ctx_ = c;
    api_.io = services_ && c && c->io && !(c->desc && c->desc->menu_first) ? &io_ : 0;
}

void LibLoader::free_mutexes(int si)
{
    SvcMutex **head = &mus_[si < 0 ? LIB_MAX : si];
    while (*head) {
        SvcMutex *m = *head;
        *head = m->next;
        host_->mutex_free(m->h);
        host_->release(m);
    }
}

uint32_t LibLoader::mutex_bytes(int a) const
{
    uint32_t n = 0;
    for (const SvcMutex *m = mus_[a]; m; m = m->next) {
        n += (uint32_t)sizeof(SvcMutex);
    }
    return n;
}

LibLoader::~LibLoader()
{
    if (reg_->scope() == static_cast<OpScope *>(this)) {
        reg_->set_scope(0);
    }
    if (s_ld == this) {
        s_ld = 0;
    }
}

// The registry runs a handler of the image in `slot`: it is the owner of the calling task until leave(), which puts back what that task had. Other tasks keep theirs.
int LibLoader::enter(int slot)
{
    int prev = host_->task_scope();
    host_->set_task_scope(slot);
    return prev;
}

// Whose account the calling task works on: the image of the handler it runs, as long as that image is loaded, else the application that is open.
int LibLoader::owner() const
{
    int t = host_->task_scope();
    return t >= 0 && t < LIB_MAX && slot_[t].used ? t : cur_;
}

// ---- the service table (T104.3 D2)

void LibLoader::svc_log(const char *text) { s_ld->api_.log(text); }
uint32_t LibLoader::svc_ms(void) { return s_ld->host_->now_ms(); }
void LibLoader::svc_sleep(uint32_t ms) { s_ld->host_->delay_ms(ms); }
const char *LibLoader::svc_core_name(void) { return s_ld->host_->core_name(); }
bool LibLoader::svc_has_capability(const char *c) { return c && s_ld->host_->has_capability(c); }

void *LibLoader::svc_malloc(uint32_t n)
{
    LibLoader *L = s_ld;
    if (n > 0x7FFFFFF0u - sizeof(Blk)) {
        return 0;
    }
    // the heap budget of the image whose code runs: what its blocks hold plus this one may not pass it; the figure is reserved before the heap is asked
    // and given back when the heap has no room
    int owner = L->owner();
    L->host_->enter();
    if (owner >= 0) {
        Slot &s = L->slot_[owner];
        if (s.heap_limit != U2A3_BUDGET_NONE && (uint64_t)s.blk_bytes + n > s.heap_limit) {
            bool first = !s.heap_denied;
            if (first) {
                s.heap_denied = true;
                s.heap_want = n;
                s.heap_had = s.blk_bytes;
            }
            bool loading = !s.used;
            L->host_->leave();
            if (first && !loading) {
                char line[LIB_ERR_MAX];
                snprintf(line, sizeof(line), "\"%s\": %u bytes asked with %u held, over its declared heap peak of %u; the request is refused.", s.info.name,
                         (unsigned)n, (unsigned)s.blk_bytes, (unsigned)s.heap_limit);
                L->api_.log(line);
            }
            return 0;
        }
        s.blk_bytes += n;
    }
    L->host_->leave();
    Blk *b = (Blk *)L->host_->alloc(sizeof(Blk) + n);
    if (!b) {
        if (owner >= 0) {
            L->host_->enter();
            L->slot_[owner].blk_bytes -= n;
            L->host_->leave();
        }
        return 0;
    }
    L->host_->enter();
    Blk **head = L->list_of(owner);
    b->size = n;
    b->owner = owner;
    b->prev = 0;
    b->next = *head;
    if (*head) {
        (*head)->prev = b;
    }
    *head = b;
    L->host_->leave();
    return b + 1;
}

void LibLoader::svc_free(void *p)
{
    if (!p) {
        return;
    }
    LibLoader *L = s_ld;
    Blk *b = (Blk *)p - 1;
    L->host_->enter();
    Blk **head = L->list_of(b->owner);
    if (b->prev) {
        b->prev->next = b->next;
    } else {
        *head = b->next;
    }
    if (b->next) {
        b->next->prev = b->prev;
    }
    if (b->owner >= 0) {
        L->slot_[b->owner].blk_bytes -= b->size;
    }
    L->host_->leave();
    L->host_->release(b);
}

void *LibLoader::svc_realloc(void *p, uint32_t n)
{
    if (!p) {
        return svc_malloc(n);
    }
    if (!n) {
        svc_free(p);
        return 0;
    }
    Blk *b = (Blk *)p - 1;
    void *q = svc_malloc(n);
    if (!q) {
        return 0;                                       // as realloc: the old block stays
    }
    memcpy(q, p, b->size < n ? b->size : n);
    svc_free(p);
    return q;
}

int LibLoader::svc_call(const char *name, const OpIn *in, int nin, OpResult *r)
{
    LibLoader *L = s_ld;
    if (!L->ctx_) {
        if (r) {
            r->code = OPE_UNSUPPORTED;
            r->more = false;
            snprintf(r->msg, sizeof(r->msg), "No operations here.");
        }
        return OPE_UNSUPPORTED;
    }
    return L->reg_->call(L->ctx_, name, in, nin, r);
}

// What `find` and a `needs` entry resolve a name against, in this order: a table a loaded library exports under that name (the lowest major that is at least the one
// asked for; the minor of min_version means nothing to an export, which has a major only), then the library of that name at that version or later.
const LibLoader::Slot *LibLoader::provider(const char *name, uint32_t min_version, const LibExportDecl **ex, uint32_t *below) const
{
    uint32_t want = min_version >> 16;
    const Slot *best = 0;
    *ex = 0;
    *below = 0;
    for (int i = 0; i < LIB_MAX; i++) {
        const Slot &s = slot_[i];
        if (!s.used || s.info.kind != U2A3_KIND_LIB || !LIB_RECORD_COVERS(s.info.record.struct_size, export_list)) {
            continue;
        }
        for (int k = 0; k < s.info.record.nexport_list; k++) {
            const LibExportDecl *e = &s.info.record.export_list[k];
            if (strcmp(e->name, name)) {
                continue;
            }
            if (e->major >= want) {
                if (!*ex || e->major < (*ex)->major) {
                    best = &s;
                    *ex = e;
                }
            } else if (e->major > *below) {
                *below = e->major;
            }
        }
    }
    return best;
}

int LibLoader::handle_providing(const char *name, uint32_t min_version) const
{
    const LibExportDecl *ex;
    uint32_t below;
    const Slot *s = name ? provider(name, min_version, &ex, &below) : 0;
    if (!s && name) {
        s = find(name);
        if (s && (s->info.kind != U2A3_KIND_LIB || s->info.version < min_version)) {
            s = 0;
        }
    }
    return s ? (int)(s - slot_) : -1;
}

const void *LibLoader::svc_lib_find(const char *name, uint32_t min_version, uint32_t *table_size)
{
    if (!name) {
        return 0;
    }
    const LibExportDecl *ex;
    uint32_t below;
    const Slot *p = s_ld->provider(name, min_version, &ex, &below);
    // the base's own table of "c64" competes as a major of its own: the lowest major that is at least the one asked for wins
    const C64Api *own = s_ld->c64api_;
    if (own && !strcmp(name, C64_API_NAME) && C64_API_MAJOR >= (min_version >> 16) && (!p || C64_API_MAJOR < ex->major)) {
        if (table_size) {
            *table_size = own->size;
        }
        return own;
    }
    if (p) {
        if (table_size) {
            *table_size = ex->size;
        }
        return ex->table;
    }
    const Slot *s = s_ld->find(name);
    if (!s || s->info.kind != U2A3_KIND_LIB || s->info.version < min_version || !s->info.record.exports) {
        return 0;
    }
    if (table_size) {
        *table_size = s->info.record.exports_size;
    }
    return s->info.record.exports;
}

const char *LibLoader::svc_state(const char *name, uint32_t *rev)
{
    LibLoader *L = s_ld;
    if (!L->ctx_) {
        return 0;
    }
    if (L->extra_state_ && (!strncmp(name, "set.", 4) || !strcmp(name, "net.gated"))) {       // base 3.3 (svc_settings.h): states of the base, not of the registry
        if (rev) {
            *rev = 0;
        }
        return L->extra_state_(L->ctx_, name);
    }
    return L->reg_->state(L->ctx_, name, rev);
}

// ---- api->fs: the core's CoreFs with the rule of fsif.h applied here as well, so a CoreFs that forgets it does not open the flash to an app

struct SvcFile {
    CoreFile *f;
    CoreFs *fs;                           // the one that opened it, which closes it
    SvcFile *next, *prev;
    int32_t owner;
};

CoreFs *LibLoader::fs_of() { return s_ld->ctx_ ? s_ld->ctx_->fs : 0; }

SvcFile *LibLoader::svc_open(const char *path, bool rw)
{
    LibLoader *L = s_ld;
    CoreFs *fs = fs_of();
    if (!fs || !path) {
        return 0;
    }
    SvcFile *h = (SvcFile *)L->host_->alloc(sizeof(SvcFile));
    if (!h) {
        return 0;
    }
    h->f = fs->open(path, rw && !fs_write_refused(path));
    if (!h->f) {
        L->host_->release(h);
        return 0;
    }
    int owner = L->owner();
    L->host_->enter();
    SvcFile **head = &L->files_[owner < 0 ? LIB_MAX : owner];
    h->fs = fs;
    h->owner = owner;
    h->prev = 0;
    h->next = *head;
    if (*head) {
        (*head)->prev = h;
    }
    *head = h;
    L->host_->leave();
    return h;
}

bool LibLoader::svc_fread(SvcFile *f, uint64_t off, void *buf, uint32_t len) { return f && f->f->read(off, buf, len); }
bool LibLoader::svc_fwrite(SvcFile *f, uint64_t off, const void *buf, uint32_t len) { return f && f->f->write(off, buf, len); }
uint64_t LibLoader::svc_fsize(SvcFile *f) { return f ? f->f->size() : 0; }
bool LibLoader::svc_fwritable(SvcFile *f) { return f && f->f->writable(); }
bool LibLoader::svc_fsync(SvcFile *f) { return f && f->f->sync(); }

void LibLoader::svc_close(SvcFile *f)
{
    if (!f) {
        return;
    }
    LibLoader *L = s_ld;
    L->host_->enter();
    SvcFile **head = &L->files_[f->owner < 0 ? LIB_MAX : f->owner];
    if (f->prev) {
        f->prev->next = f->next;
    } else {
        *head = f->next;
    }
    if (f->next) {
        f->next->prev = f->prev;
    }
    L->host_->leave();
    f->fs->close(f->f);
    L->host_->release(f);
}

// The app ABI keeps SvcFsEntry (56 bytes of name, svctab.h): the core's listing has the whole FAT long name (FsEntry), the bridge cuts it to 55
// characters as the listing did before T180 #38, so an application built against the old layout sees what it always saw.
int LibLoader::svc_list(const char *dir, SvcFsEntry *out, int max)
{
    CoreFs *fs = fs_of();
    if (!fs || !dir) {
        return -1;
    }
    if (max <= 0) {
        FsEntry none;
        return fs->list(dir, &none, 0);
    }
    FsEntry *tmp = (FsEntry *)s_ld->host_->alloc((uint32_t)max * sizeof(FsEntry));
    if (!tmp) {
        return -1;
    }
    int n = fs->list(dir, tmp, max);
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        strncpy(out[i].name, tmp[i].name, sizeof(out[i].name) - 1);
        out[i].fdate = tmp[i].fdate;
        out[i].ftime = tmp[i].ftime;
        out[i].attr = tmp[i].attr;
        out[i].dir = tmp[i].dir;
        out[i].size = tmp[i].size;
    }
    s_ld->host_->release(tmp);
    return n;
}
// base 3.3 list_long: SvcFsEntryLong is FsEntry (libapi.h checks it), so the caller's array is the listing's. max <= 0 counts: CoreFs::list stops at max, so the count is taken with the
// 72 byte entries of list_short in a block of the heap that doubles until the listing fits (-1 when the heap has no room for it).
int LibLoader::svc_list_long(const char *dir, SvcFsEntryLong *out, int max)
{
    CoreFs *fs = fs_of();
    if (!fs || !dir) {
        return -1;
    }
    if (max > 0) {
        return fs->list(dir, (FsEntry *)out, max);
    }
    for (int cap = 64; cap <= 16384; cap *= 2) {
        FsEntryShort *tmp = (FsEntryShort *)s_ld->host_->alloc((uint32_t)cap * sizeof(FsEntryShort));
        if (!tmp) {
            return -1;
        }
        int n = fs->list_short(dir, tmp, cap);
        s_ld->host_->release(tmp);
        if (n < cap) {
            return n;
        }
    }
    return -1;
}
uint16_t LibLoader::svc_ms_timer(void) { return s_ld->host_->ms_timer(); }
void LibLoader::svc_yield(void) { s_ld->host_->yield(); }
const char *LibLoader::svc_app_version(void) { return s_ld->host_->app_version(); }
const char *LibLoader::svc_app_git(void) { return s_ld->host_->app_git(); }
void LibLoader::svc_delay_us(uint32_t us)
{
    CoreIo *io = s_ld->ctx_ ? s_ld->ctx_->io : 0;
    if (io) {
        io->delay_us(us);
    }
}
bool LibLoader::svc_wall_time(uint8_t t[6])
{
    CoreIo *io = s_ld->ctx_ ? s_ld->ctx_->io : 0;
    uint8_t w[6];
    if (!io || !io->wall_time(w) || w[0] < 20) {                      // no clock, or one that was never set (the year reads before 2020)
        return false;
    }
    memcpy(t, w, 6);
    return true;
}
bool LibLoader::svc_stat(const char *path, uint64_t *size, bool *dir) { CoreFs *fs = fs_of(); return fs && path && fs->stat(path, size, dir); }
int LibLoader::svc_load(const char *path, void *buf, uint32_t max) { CoreFs *fs = fs_of(); return fs && path ? fs->load(path, buf, max) : -1; }

bool LibLoader::svc_save(const char *path, const void *buf, uint32_t len)
{
    CoreFs *fs = fs_of();
    return fs && path && !fs_write_refused(path) && fs->save(path, buf, len);
}

bool LibLoader::svc_remove(const char *path)
{
    CoreFs *fs = fs_of();
    return fs && path && !fs_write_refused(path) && fs->remove(path);
}

bool LibLoader::svc_mkdir(const char *path)
{
    CoreFs *fs = fs_of();
    return fs && path && !fs_write_refused(path) && fs->mkdir(path);
}

bool LibLoader::svc_rename(const char *from, const char *to)
{
    CoreFs *fs = fs_of();
    return fs && from && to && !fs_write_refused(from) && !fs_write_refused(to) && fs->rename(from, to);
}

int LibLoader::account_files(int handle) const
{
    int n = 0;
    for (const SvcFile *f = files_[handle < 0 || handle >= LIB_MAX ? LIB_MAX : handle]; f; f = f->next) {
        n++;
    }
    return n;
}

void LibLoader::free_account(int si)
{
    SvcFile **fh = &files_[si < 0 ? LIB_MAX : si];
    while (*fh) {
        SvcFile *f = *fh;
        *fh = f->next;
        f->fs->close(f->f);
        host_->release(f);
    }
    free_mutexes(si);
    Blk **head = list_of(si);
    while (*head) {
        Blk *b = *head;
        *head = b->next;
        host_->release(b);
    }
}

int LibLoader::account_blocks(int handle) const
{
    int n = 0;
    for (const Blk *b = acct_[handle < 0 || handle >= LIB_MAX ? LIB_MAX : handle]; b; b = b->next) {
        n++;
    }
    return n;
}

uint32_t LibLoader::account_bytes(int handle) const
{
    uint32_t n = 0;
    for (const Blk *b = acct_[handle < 0 || handle >= LIB_MAX ? LIB_MAX : handle]; b; b = b->next) {
        n += b->size;
    }
    return n;
}

// What one account holds of the heap, as the heap gave it: every block with the header the loader put in front of it, every file handle, and for a
// loaded image its own block (image, bss, alignment) and its menu record. This is the figure that comes back when the image goes.
uint32_t LibLoader::held_bytes(int handle) const
{
    int a = handle < 0 || handle >= LIB_MAX ? LIB_MAX : handle;
    uint32_t n = 0;
    for (const Blk *b = acct_[a]; b; b = b->next) {
        n += b->size + (uint32_t)sizeof(Blk);
    }
    for (const SvcFile *f = files_[a]; f; f = f->next) {
        n += (uint32_t)sizeof(SvcFile);
    }
    n += mutex_bytes(a);
    if (a < LIB_MAX && slot_[a].used) {
        n += slot_[a].raw_size + slot_[a].menu_cost;
    }
    return n;
}

uint32_t LibLoader::held_total() const
{
    uint32_t n = held_bytes(-1);
    for (int i = 0; i < LIB_MAX; i++) {
        if (slot_[i].used) {
            n += held_bytes(i);
        }
    }
    return n;
}

void LibLoader::set_log(void (*fn)(const char *text))
{
    api_.log = fn ? fn : no_log;
}

int LibLoader::fail(int rc, char *err, int errlen, const char *fmt, ...)
{
    if (err && errlen > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return rc;
}

void LibLoader::retire(const Slot &s)
{
    if (polls_) {
        polls_->drop(s.info.name);
    }
    reg_->drop_owner(s.info.name);
}

void LibLoader::discard(int si)
{
    for (int i = 0; i < slot_[si].ndep; i++) {
        unuse(slot_[si].dep[i]);                        // what this image needed is free for eviction once nothing else uses it
    }
    delete slot_[si].info.menu;
    delete[] slot_[si].info.menu_bytes;
    free_account(si);
    host_->release(slot_[si].raw);
    memset(&slot_[si], 0, sizeof(slot_[si]));
}

const LibLoader::Slot *LibLoader::find(const char *name) const
{
    for (int i = 0; i < LIB_MAX; i++) {
        if (slot_[i].used && !strcmp(slot_[i].info.name, name)) {
            return &slot_[i];
        }
    }
    return 0;
}

int LibLoader::handle_of(const char *name) const
{
    const Slot *s = find(name);
    return s ? (int)(s - slot_) : -1;
}

const LibInfo *LibLoader::info(int handle) const
{
    return handle >= 0 && handle < LIB_MAX && slot_[handle].used ? &slot_[handle].info : 0;
}

uint32_t LibLoader::resident_bytes() const
{
    uint32_t n = 0;
    for (int i = 0; i < LIB_MAX; i++) {
        if (slot_[i].used && slot_[i].info.residence == U2A3_RES_RESIDENT) {
            n += slot_[i].info.image_size;
        }
    }
    return n;
}

static bool record_contributes(const LibRecord &r)
{
    return (r.poll.op && r.poll.op[0]) || r.nnodes > 0 || r.nfile_actions > 0 || r.nsettings > 0 || r.ntabs > 0;
}

// A library that exports the table "corehooks" (core_hooks.h): the base keeps a pointer into it for the whole run (hookhost.cc) and calls it from the menu close and every
// change of a base-owned setting, so the library is never evicted, whatever else it declares.
static bool exports_corehooks(const LibInfo *li)
{
    if (!strcmp(li->name, "corehooks") && li->record.exports) {
        return true;
    }
    if (!LIB_RECORD_COVERS(li->record.struct_size, export_list)) {
        return false;
    }
    for (int k = 0; k < li->record.nexport_list; k++) {
        if (!strcmp(li->record.export_list[k].name, "corehooks")) {
            return true;
        }
    }
    return false;
}

bool LibLoader::contributes(int handle) const
{
    const LibInfo *li = info(handle);
    return li && (record_contributes(li->record) || (li->menu && record_contributes(li->menu->rec)) || exports_corehooks(li));
}

bool LibLoader::unserved_text(const char *, const LibRecord &, char *, int)
{
    // nodes, file actions, settings, tabs and the poll of a record are served now: the menu merge (contrib.h) takes the first four, the poll task
    // (run.cc) the last. Nothing a record declares goes unserved; the function stays for the callers that ask.
    return false;
}

int LibLoader::unserved_count() const
{
    int n = 0;
    char tmp[8];
    for (int i = 0; i < LIB_MAX; i++) {
        n += slot_[i].used && unserved_text("", slot_[i].info.record, tmp, sizeof(tmp));
    }
    return n;
}

// A name: 1..15 characters of [a-z0-9_] and a NUL inside the field. The empty string is allowed where the caller says so.
static bool field_ok(const char *f, bool may_be_empty)
{
    int n = 0;
    while (n < U2A3_NAME_MAX && f[n]) {
        char ch = f[n];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) {
            return false;
        }
        n++;
    }
    return n < U2A3_NAME_MAX && (n > 0 || may_be_empty);
}

static void version_text(char *out, uint32_t v)
{
    snprintf(out, 16, "%u.%u", (unsigned)(v >> 16), (unsigned)(v & 0xFFFF));
}

// Is [off, off + len) inside a total of `total` bytes, without the sum wrapping?
static bool range_ok(uint32_t off, uint32_t len, uint32_t total)
{
    return off <= total && len <= total - off;
}

// What a record and a menu record bring of each kind that a table holds (the menu record has no operations, state values or events).
struct Counts {
    int nodes, file_actions, settings, tabs, polls;
};

static int nn(int n) { return n > 0 ? n : 0; }

static void add_counts(Counts *c, const LibRecord *r)
{
    if (!r) {
        return;
    }
    c->nodes += nn(r->nnodes);
    c->file_actions += nn(r->nfile_actions);
    c->settings += nn(r->nsettings);
    c->tabs += nn(r->ntabs);
    c->polls += r->poll.op && r->poll.op[0];
}

static int tabs_in_area(const LibRecord *r, const char *area)
{
    int n = 0;
    for (int i = 0; r && i < r->ntabs; i++) {
        n += r->tabs[i].area && !strcmp(r->tabs[i].area, area);
    }
    return n;
}

// Is what the library brings (`rec`: what lib_register returned, null before it ran; `menu`: its menu record) over the figures it declared, or over the room that
// is left in a table? The declared figure is checked first; a figure left open is the table's own. false: fits; true: `err` says which figure and which limit.
// The tables are the ones the registry and the menu merge hold (ops.h, contrib.h, menutree.h); what other loaded images already take counts against them.
bool LibLoader::over_budget(const char *name, const U2a3Budget &b, const LibRecord *rec, const LibRecord *menu, char *err, int errlen) const
{
    Counts mine = { 0, 0, 0, 0, 0 }, others = { 0, 0, 0, 0, 0 };
    add_counts(&mine, rec);
    add_counts(&mine, menu);
    for (int i = 0; i < LIB_MAX; i++) {
        if (slot_[i].used) {
            add_counts(&others, &slot_[i].info.record);
            add_counts(&others, slot_[i].info.menu ? &slot_[i].info.menu->rec : 0);
        }
    }
    struct Row {
        const char *what;
        int n;
        uint32_t declared;
        int taken, cap;                       // cap 0: the table has no limit of its own that the loader can see
    };
    const Row rows[] = {
        { "operations", rec ? nn(rec->nops) : 0, b.ops, reg_->nops(), OPS_MAX },
        { "state values", rec ? nn(rec->nstates) : 0, b.states, reg_->nstates(), STATES_MAX },
        { "events", rec ? nn(rec->nevents) : 0, b.events, reg_->nevents(), EVENTS_MAX },
        { "menu nodes", mine.nodes, b.nodes, others.nodes, MENU_NODES_MAX },
        { "file actions", mine.file_actions, b.file_actions, others.file_actions, CONTRIB_ACTIONS_MAX },
        { "settings", mine.settings, b.settings, others.settings, CONTRIB_SETTINGS_MAX },
        { "tabs", mine.tabs, b.tabs, 0, 0 },
        { "polls", mine.polls, b.polls, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const Row &r = rows[i];
        if (r.declared != U2A3_BUDGET_NONE && (uint32_t)r.n > r.declared) {
            snprintf(err, errlen, "Library \"%s\" is refused: it brings %d %s, over its declared budget of %u.", name, r.n, r.what, (unsigned)r.declared);
            return true;
        }
        if (r.cap && r.taken + r.n > r.cap) {
            snprintf(err, errlen, "Library \"%s\" is refused: it brings %d %s, the table holds %d and %d are taken.", name, r.n, r.what, r.cap, r.taken);
            return true;
        }
    }
    // the tab strip of an area holds MENU_STRIP_TABS tabs, whoever brought them
    int nrt = rec ? nn(rec->ntabs) : 0, nmt = menu ? nn(menu->ntabs) : 0;
    for (int k = 0; k < nrt + nmt; k++) {
        const LibTabDecl *t = k < nrt ? &rec->tabs[k] : &menu->tabs[k - nrt];
        if (!t->area) {
            continue;
        }
        int in_area = tabs_in_area(rec, t->area) + tabs_in_area(menu, t->area), taken = 0;
        for (int i = 0; i < LIB_MAX; i++) {
            if (slot_[i].used) {
                taken += tabs_in_area(&slot_[i].info.record, t->area) + tabs_in_area(slot_[i].info.menu ? &slot_[i].info.menu->rec : 0, t->area);
            }
        }
        if (taken + in_area > MENU_STRIP_TABS) {
            snprintf(err, errlen, "Library \"%s\" is refused: it brings %d tabs to area %s, the tab strip holds %d and %d are taken.", name, in_area, t->area,
                     MENU_STRIP_TABS, taken);
            return true;
        }
    }
    return false;
}

// The names a file exports (libfmt.h U2a3ExportName), read from the header and its record alone, before anything runs: the number of entries (0: the file has none), or -1 when the
// record is malformed, lies outside the file, overlaps the needs or the image, holds a name twice, or belongs to an application.
int lib_read_exports(LibSource *src, const U2a3Header &h, U2a3ExportName *out)
{
    if (!(h.flags & U2A3_FLAG_EXPORTS)) {
        return 0;
    }
    uint32_t fsize = src->size(), bsz = 0, cnt = 0;
    if (h.flags & U2A3_FLAG_BUDGET) {
        if (!range_ok(U2A3_HEADER_SIZE, 4, fsize) || !src->read(U2A3_HEADER_SIZE, &bsz, 4) || bsz < U2A3_BUDGET_SIZE || bsz > U2A3_BUDGET_MAX || (bsz & 3)) {
            return -1;
        }
    }
    uint32_t eoff = U2A3_HEADER_SIZE + bsz;
    if (h.kind != U2A3_KIND_LIB || !range_ok(eoff, 4, fsize) || !src->read(eoff, &cnt, 4) || cnt < 1 || cnt > U2A3_EXPORTS_MAX ||
        !range_ok(eoff + 4, cnt * U2A3_EXPORT_NAME_SIZE, fsize) || !src->read(eoff + 4, out, cnt * U2A3_EXPORT_NAME_SIZE)) {
        return -1;
    }
    uint32_t end = eoff + 4 + cnt * U2A3_EXPORT_NAME_SIZE;
    if ((h.needs_count && h.needs_offset < end) || h.payload_offset < end) {
        return -1;
    }
    for (uint32_t i = 0; i < cnt; i++) {
        if (!field_ok(out[i].name, false) || out[i].major < 1) {
            return -1;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (!strcmp(out[i].name, out[j].name) && out[i].major == out[j].major) {
                return -1;
            }
        }
    }
    return (int)cnt;
}

// What lib_register returned as named tables (LibExportDecl): well formed, free of the names others hold, and at least what the file lists. Slot si is the one that loads.
bool LibLoader::exports_bad(int si, const LibRecord &rec, const U2a3ExportName *file, int nfile, char *why, int whylen) const
{
    int n = LIB_RECORD_COVERS(rec.struct_size, export_list) ? rec.nexport_list : 0;
    if (n < 0 || n > LIB_EXPORTS_MAX || (n && !rec.export_list)) {
        snprintf(why, whylen, "it returned %d exported tables (at most %d, and a count needs a list).", n, LIB_EXPORTS_MAX);
        return true;
    }
    for (int i = 0; i < n; i++) {
        const LibExportDecl &e = rec.export_list[i];
        if (!e.name || !field_ok(e.name, false) || e.major < 1 || !e.table || e.size < sizeof(uint32_t)) {
            snprintf(why, whylen, "export %d is malformed (a name of [a-z0-9_], a major from 1, a table of at least 4 bytes).", i);
            return true;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(rec.export_list[j].name, e.name) && rec.export_list[j].major == e.major) {
                snprintf(why, whylen, "it exports \"%s\" %u twice.", e.name, (unsigned)e.major);
                return true;
            }
        }
        for (int k = 0; k < LIB_MAX; k++) {
            const Slot &o = slot_[k];
            if (k == si || !o.used || o.info.kind != U2A3_KIND_LIB) {
                continue;
            }
            if (!strcmp(o.info.name, e.name)) {
                snprintf(why, whylen, "its export \"%s\" is the name of the library \"%s\".", e.name, o.info.name);
                return true;
            }
            for (int m = 0; m < o.info.record.nexport_list && LIB_RECORD_COVERS(o.info.record.struct_size, export_list); m++) {
                if (!strcmp(o.info.record.export_list[m].name, e.name) && o.info.record.export_list[m].major == e.major) {
                    snprintf(why, whylen, "\"%s\" %u is exported by \"%s\" already.", e.name, (unsigned)e.major, o.info.name);
                    return true;
                }
            }
        }
    }
    for (int f = 0; f < nfile; f++) {
        bool there = false;
        for (int i = 0; i < n && !there; i++) {
            there = !strcmp(rec.export_list[i].name, file[f].name) && rec.export_list[i].major == file[f].major;
        }
        if (!there) {
            snprintf(why, whylen, "its file lists the export \"%s\" %u and lib_register does not return it.", file[f].name, (unsigned)file[f].major);
            return true;
        }
    }
    return false;
}

int LibLoader::load(LibSource *src, int *handle, char *err, int errlen)
{
    *handle = -1;
    uint32_t fsize = src->size();
    U2a3Header h;
    if (fsize < sizeof(h) || !src->read(0, &h, sizeof(h))) {
        return fail(LIB_BAD_FILE, err, errlen, "Not a library or app file: too short to hold a header.");
    }
    if (h.magic != U2A3_MAGIC || h.header_size != U2A3_HEADER_SIZE) {
        return fail(LIB_BAD_FILE, err, errlen, "Not a library or app file: wrong magic or header size.");
    }
    if (h.format_version != U2A3_FORMAT_VERSION) {
        return fail(LIB_BAD_FILE, err, errlen, "Old file format %u, this base reads %u. Rebuild the file.", (unsigned)h.format_version,
                    (unsigned)U2A3_FORMAT_VERSION);
    }
    // the name first, so every later refusal can name what it refuses
    if (!field_ok(h.name, false) || !field_ok(h.capability, true)) {
        return fail(LIB_BAD_FILE, err, errlen, "The file has no valid name or capability.");
    }
    const char *name = h.name;

    if ((h.flags & ~(U2A3_FLAG_MENU | U2A3_FLAG_BUDGET | U2A3_FLAG_EXPORTS)) || (!(h.flags & U2A3_FLAG_MENU) && h.reserved)) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\" sets header flags (0x%X) or reserved bits (0x%X) this base does not know.", name,
                    (unsigned)h.flags, (unsigned)h.reserved);
    }
    if (h.abi_min > U2A_ABI_MAJOR || h.abi_max < U2A_ABI_MAJOR) {
        return fail(LIB_ABI, err, errlen, "\"%s\" was built for ABI %u..%u, this base is ABI %u.", name, (unsigned)h.abi_min,
                    (unsigned)h.abi_max, (unsigned)U2A_ABI_MAJOR);
    }
    if (h.arch != U2A3_ARCH_RISCV32) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\" is not built for this CPU.", name);
    }
    if (h.kind != U2A3_KIND_LIB && h.kind != U2A3_KIND_APP) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\" is neither a library nor an app.", name);
    }
    if (h.residence != U2A3_RES_TRANSIENT && h.residence != U2A3_RES_RESIDENT) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\" declares an unknown residence.", name);
    }
    // the shape of each kind (D10.3): a library never goes away and registers; a transient app only runs; a resident one may do both
    bool lib = h.kind == U2A3_KIND_LIB;
    if (lib && (h.residence != U2A3_RES_RESIDENT || h.register_offset == U2A3_NONE || h.main_offset != U2A3_NONE)) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": a library is resident and has lib_register, no app_main.", name);
    }
    if (!lib && h.main_offset == U2A3_NONE) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": an app has app_main.", name);
    }

    // sizes and ranges, all before the first byte of memory is spent
    uint32_t psize = h.payload_size;
    if (psize == 0 || h.bss_size > U2A3_IMAGE_MAX || psize > U2A3_IMAGE_MAX - h.bss_size) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the image is empty or larger than %u bytes.", name, (unsigned)U2A3_IMAGE_MAX);
    }
    uint32_t isize = psize + h.bss_size;
    if (h.align < 4 || h.align > U2A3_ALIGN_MAX || (h.align & (h.align - 1))) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the alignment %u is not a power of two from 4 to %u.", name, (unsigned)h.align,
                    (unsigned)U2A3_ALIGN_MAX);
    }
    if (!range_ok(h.payload_offset, psize, fsize) || (h.payload_offset & 3)) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the image lies outside the file.", name);
    }
    if (h.reloc_count > psize / 4 || !range_ok(h.reloc_offset, h.reloc_count * 4, fsize) || (h.reloc_offset & 3)) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the relocation table lies outside the file.", name);
    }
    if (h.needs_count > U2A3_NEEDS_MAX || !range_ok(h.needs_offset, h.needs_count * U2A3_NEED_SIZE, fsize)) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the list of dependencies lies outside the file.", name);
    }
    if (h.manifest_size && !range_ok(h.manifest_offset, h.manifest_size, fsize)) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the manifest lies outside the file.", name);
    }
    // the budget (libfmt.h U2a3Budget): directly behind the header; every figure it leaves open is the platform's limit
    U2a3Budget bud;
    memset(&bud, 0xFF, sizeof(bud));
    if (h.flags & U2A3_FLAG_BUDGET) {
        uint32_t bsz = 0;
        if (!range_ok(U2A3_HEADER_SIZE, 4, fsize) || !src->read(U2A3_HEADER_SIZE, &bsz, 4) || bsz < U2A3_BUDGET_SIZE || bsz > U2A3_BUDGET_MAX || (bsz & 3) ||
            !range_ok(U2A3_HEADER_SIZE, bsz, fsize) || !src->read(U2A3_HEADER_SIZE, &bud, sizeof(bud)) ||
            (h.needs_count && h.needs_offset < U2A3_HEADER_SIZE + bsz) || h.payload_offset < U2A3_HEADER_SIZE + bsz) {
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the budget record is malformed, or it overlaps the dependencies or the image.", name);
        }
        if (bud.heap_keep != U2A3_BUDGET_NONE && bud.heap_peak != U2A3_BUDGET_NONE && bud.heap_keep > bud.heap_peak) {
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the budget keeps %u bytes of heap and allows a peak of %u.", name, (unsigned)bud.heap_keep, (unsigned)bud.heap_peak);
        }
    }
    // the names it exports (libfmt.h U2a3ExportName): behind the budget record, or behind the header; read now, held against what lib_register returns
    U2a3ExportName fexp[U2A3_EXPORTS_MAX];
    int nfexp = lib_read_exports(src, h, fexp);
    if (nfexp < 0) {
        return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the list of exported names is malformed (a library has one to %d names, each [a-z0-9_] and a major from 1, none twice), or it overlaps the dependencies or the image.", name,
                    U2A3_EXPORTS_MAX);
    }
    if (bud.image != U2A3_BUDGET_NONE && isize > bud.image) {
        return fail(LIB_BUDGET, err, errlen, "Library \"%s\" is refused: its image is %u bytes with its bss, over its declared budget of %u bytes.", name,
                    (unsigned)isize, (unsigned)bud.image);
    }
    // the menu record (libmenu.h): read and checked here, kept by the slot once the load has succeeded, freed with any refusal below
    struct MenuGuard {
        DescRecord *d;
        uint8_t *b;
        MenuGuard() : d(0), b(0) { }
        ~MenuGuard() { delete d; delete[] b; }
    } menu;
    if (h.flags & U2A3_FLAG_MENU) {
        char mw[160];
        if (libmenu_read(src, fsize, h.reserved, &menu.d, &menu.b, mw, sizeof(mw), h.kind == U2A3_KIND_APP) != 0) {
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": %s.", name, mw);
        }
    }
    {   // what the menu record alone brings is known before any memory is spent: over the budget or the room of a table, the library is refused here
        char why[LIB_ERR_MAX];
        if (over_budget(name, bud, 0, menu.d ? &menu.d->rec : 0, why, sizeof(why))) {
            return fail(LIB_BUDGET, err, errlen, "%s", why);
        }
    }
    // the entry points are checked before any relocation, long before anything runs: an entry outside the image would jump into
    // whatever the heap holds there
    uint32_t entries[2] = { h.register_offset, h.main_offset };
    for (int i = 0; i < 2; i++) {
        if (entries[i] != U2A3_NONE && (entries[i] >= psize || (entries[i] & 3))) {
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the %s entry at offset 0x%X lies outside the image (%u bytes) or is not 4-aligned (the CPU has no C extension).", name,
                        i ? "app_main" : "lib_register", (unsigned)entries[i], (unsigned)psize);
        }
    }

    // D11: what it needs, by name and both versions
    char vw[16], vh[16];
    int deps[U2A3_NEEDS_MAX], ndeps = 0;                // the loaded images this one needs (T197 D8: counted as used while this one is loaded)
    for (int i = 0; i < h.needs_count; i++) {
        U2a3Need nd;
        if (!src->read(h.needs_offset + i * U2A3_NEED_SIZE, &nd, sizeof(nd)) || !field_ok(nd.name, false)) {
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": a dependency entry is unreadable.", name);
        }
        version_text(vw, nd.min_version);
        if (!strcmp(nd.name, "base")) {
            uint32_t have = U2A3_VERSION(U2A_ABI_MAJOR, base_minor_);
            if (have < nd.min_version) {
                version_text(vh, have);
                return fail(LIB_NEEDS, err, errlen, "\"%s\" needs base %s, this base is %s.", name, vw, vh);
            }
            continue;
        }
        if (!strcmp(nd.name, PRG_APP_NEED) || !strcmp(nd.name, CART_APP_NEED)) {
            continue;                                         // not a library: AppIndex decides where such an application is offered (libfmt.h)
        }
        // a table a loaded library exports under that name comes first, then a library of that name (D20)
        const LibExportDecl *ex = 0;
        uint32_t below = 0;
        const Slot *dep = provider(nd.name, nd.min_version, &ex, &below);
        if (!dep) {
            dep = find(nd.name);
            if (!dep) {
                if (below) {
                    version_text(vh, U2A3_VERSION(below, 0));
                    return fail(LIB_NEEDS, err, errlen, "\"%s\" needs \"%s\" %s, %s is loaded.", name, nd.name, vw, vh);
                }
                return fail(LIB_NEEDS, err, errlen, "\"%s\" needs \"%s\" %s, which is not loaded.", name, nd.name, vw);
            }
            if (dep->info.version < nd.min_version) {
                version_text(vh, dep->info.version);
                return fail(LIB_NEEDS, err, errlen, "\"%s\" needs \"%s\" %s, %s is loaded.", name, nd.name, vw, vh);
            }
        }
        deps[ndeps++] = (int)(dep - slot_);
    }

    // Q10: a library whose capability the core lacks is refused, with a message, before any memory is spent
    if (h.capability[0] && !host_->has_capability(h.capability)) {
        return fail(LIB_CAPABILITY, err, errlen, "Library \"%s\" needs the capability \"%s\", which the core \"%s\" does not have.", name,
                    h.capability, host_->core_name());
    }
    if (const Slot *twin = find(name)) {
        version_text(vh, twin->info.version);
        return fail(LIB_DUPLICATE, err, errlen, "\"%s\" is loaded already (%s).", name, vh);
    }
    {   // a name that a loaded library exports is not free for another library: find would always answer the export
        const LibExportDecl *ex = 0;
        uint32_t below = 0;
        if (const Slot *owner = provider(name, 0, &ex, &below)) {
            return fail(LIB_DUPLICATE, err, errlen, "\"%s\" is the name of a table that \"%s\" exports.", name, owner->info.name);
        }
    }
    int si = -1;
    for (int i = 0; i < LIB_MAX; i++) {
        if (!slot_[i].used) {
            si = i;
            break;
        }
    }
    if (si < 0) {
        return fail(LIB_NO_ROOM, err, errlen, "No room for \"%s\": %d images are loaded.", name, LIB_MAX);
    }

    // what the library says it keeps, with its image, has to fit the room of the heap before the heap is asked for the image (T239.3 D1)
    if (bud.heap_keep != U2A3_BUDGET_NONE && (uint64_t)isize + h.align + bud.heap_keep > host_->room()) {
        return fail(LIB_NO_ROOM, err, errlen, "Library \"%s\" is refused: it keeps %u bytes of heap and its image takes %u, the heap has room for %u.", name,
                    (unsigned)bud.heap_keep, (unsigned)(isize + h.align), (unsigned)host_->room());
    }
    // the image, from the heap, aligned
    uint8_t *raw = (uint8_t *)host_->alloc(isize + h.align);
    if (!raw) {
        return fail(LIB_NO_ROOM, err, errlen, "No room for \"%s\": the heap cannot hold %u bytes.", name, (unsigned)isize);
    }
    uint32_t skew = host_->addr_of(raw) & (h.align - 1);
    uint8_t *img = raw + (skew ? h.align - skew : 0);
    uint32_t base = host_->addr_of(img);

    // The payload goes in blocks smaller than a sector: FatFS copies through its own buffer then, and never reads whole sectors
    // straight into an address that follows the file position (the reason Gideon's loader does the same).
    const uint32_t block = 256;
    for (uint32_t done = 0; done < psize;) {
        uint32_t n = psize - done < block ? psize - done : block;
        if (!src->read(h.payload_offset + done, img + done, n)) {
            host_->release(raw);
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the image cannot be read.", name);
        }
        done += n;
    }
    memset(img + psize, 0, h.bss_size);

    // The relocation: word += base, for each listed word. Strictly ascending means no word is added to twice.
    uint32_t prev = 0;
    bool first = true;
    for (uint32_t done = 0; done < h.reloc_count;) {
        uint32_t tab[64];
        uint32_t n = h.reloc_count - done < 64 ? h.reloc_count - done : 64;
        if (!src->read(h.reloc_offset + done * 4, tab, n * 4)) {
            host_->release(raw);
            return fail(LIB_BAD_FILE, err, errlen, "\"%s\": the relocation table cannot be read.", name);
        }
        for (uint32_t k = 0; k < n; k++) {
            uint32_t off = tab[k];
            if ((off & 3) || psize < 4 || off > psize - 4) {
                host_->release(raw);
                return fail(LIB_BAD_FILE, err, errlen, "\"%s\": relocation entry %u at offset 0x%X lies outside the image (%u bytes) or is not word aligned.",
                            name, (unsigned)(done + k), (unsigned)off, (unsigned)psize);
            }
            if (!first && off <= prev) {
                host_->release(raw);
                return fail(LIB_BAD_FILE, err, errlen, "\"%s\": relocation entry %u at offset 0x%X is not in ascending order.", name,
                            (unsigned)(done + k), (unsigned)off);
            }
            first = false;
            prev = off;
            uint32_t w;
            memcpy(&w, img + off, 4);
            w += base;
            memcpy(img + off, &w, 4);
        }
        done += n;
    }
    host_->sync_code(img, isize);

    Slot &s = slot_[si];
    memset(&s, 0, sizeof(s));
    memcpy(s.info.name, h.name, U2A3_NAME_MAX);
    s.info.version = h.version;
    s.info.kind = h.kind;
    s.info.residence = h.residence;
    s.info.image_addr = base;
    s.info.image_size = isize;
    s.info.main_addr = h.main_offset == U2A3_NONE ? 0 : base + h.main_offset;
    s.raw = raw;
    s.raw_size = isize + h.align;
    s.heap_limit = bud.heap_peak;
    if (menu.d && menu.b) {
        uint32_t total = menu.b[8] | menu.b[9] << 8 | menu.b[10] << 16 | (uint32_t)menu.b[11] << 24;
        s.menu_cost = (uint32_t)sizeof(DescRecord) + total;
    }
    for (int i = 0; i < ndeps; i++) {
        s.dep[i] = deps[i];
        use(deps[i]);
    }
    s.ndep = ndeps;

    // the poll of the library: lib_register's record or the menu record carries it, not both (the one the other would silently replace)
    RegMark mark = reg_->mark();
    bool had_error = reg_->error()[0] != 0;
    LibRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.struct_size = sizeof(rec);
    if (h.register_offset != U2A3_NONE) {
        // D10.2: the library registers what it contributes under its own name; a failure takes back everything it got in
        LibHost::Entry e = { s.info.name, base + h.register_offset, base, img };
        cur_ = si;                                      // what lib_register takes from api->mem is the image's
        int rc = host_->call_register(e, &api_, &rec);
        cur_ = -1;
        int bad = LIB_OK;
        char why[LIB_ERR_MAX];
        if (s.heap_denied) {                            // what the library could not get is the reason, whatever lib_register made of it
            bad = fail(LIB_BUDGET, err, errlen, "Library \"%s\" is refused: lib_register asked for %u bytes of heap with %u held, over its declared heap peak of %u bytes.", name,
                       (unsigned)s.heap_want, (unsigned)s.heap_had, (unsigned)s.heap_limit);
        } else if (rc != 0) {
            bad = fail(LIB_REGISTER, err, errlen, "Library \"%s\": lib_register failed (%d).", name, rc);
        } else if (rec.struct_size != sizeof(rec)) {
            bad = fail(LIB_REGISTER, err, errlen, "Library \"%s\" was built against another record layout (%u bytes, this base %u).", name,
                       (unsigned)rec.struct_size, (unsigned)sizeof(rec));
        } else if (rec.nops < 0 || rec.nstates < 0 || rec.nevents < 0 || (rec.nops && !rec.ops) || (rec.nstates && !rec.states) ||
                   (rec.nevents && !rec.events)) {
            bad = fail(LIB_REGISTER, err, errlen, "Library \"%s\" returned a record with a count and no list.", name);
        } else if (rec.exports && rec.exports_size < sizeof(uint32_t)) {
            bad = fail(LIB_REGISTER, err, errlen, "Library \"%s\" returned a call table with no size.", name);
        } else if (exports_bad(si, rec, fexp, nfexp, why, sizeof(why))) {
            bad = fail(LIB_REGISTER, err, errlen, "Library \"%s\" is refused: %s", name, why);
        } else if (over_budget(name, bud, &rec, menu.d ? &menu.d->rec : 0, why, sizeof(why))) {
            bad = fail(LIB_BUDGET, err, errlen, "%s", why);
        } else {
            reg_->contributor_in(s.info.name, si);
            int r = OPE_OK;
            for (int i = 0; i < rec.nops && r == OPE_OK; i++) {
                r = reg_->add_op(rec.ops[i].name, rec.ops[i].args, rec.ops[i].nargs, rec.ops[i].fn);
            }
            for (int i = 0; i < rec.nstates && r == OPE_OK; i++) {
                r = reg_->add_state(rec.states[i].name, rec.states[i].get);
            }
            for (int i = 0; i < rec.nevents && r == OPE_OK; i++) {
                r = reg_->add_event(rec.events[i].name, rec.events[i].payload);
            }
            if (r != OPE_OK) {
                bad = fail(LIB_REGISTER, err, errlen, "Library \"%s\" is refused: %s", name, reg_->error());
            }
        }
        if (bad) {
            reg_->rollback(mark);
            if (!had_error) {
                reg_->clear_error();                    // the refusal was told to the caller; the registry holds nothing of it
            }
            discard(si);
            return bad;
        }
        s.info.record = rec;
    }
    // the poll: served from here (set_poll_sink) or by the merge at boot; the same rules either way. Two polls refuse the library at boot too.
    const LibPollDecl *pd = rec.poll.op && rec.poll.op[0] ? &rec.poll : 0;
    if (menu.d && menu.d->rec.poll.op && menu.d->rec.poll.op[0]) {
        if (pd) {
            reg_->rollback(mark);
            if (!had_error) {
                reg_->clear_error();
            }
            discard(si);
            return fail(LIB_REGISTER, err, errlen, "Library \"%s\" is refused: it declares a poll in lib_register and in its menu record; one is allowed.", name);
        }
        pd = &menu.d->rec.poll;
    }
    if (pd && polls_) {
        char why[120];
        if (!polls_->add(s.info.name, pd->op, pd->period_ms, why, sizeof(why))) {
            reg_->rollback(mark);
            if (!had_error) {
                reg_->clear_error();
            }
            discard(si);
            return fail(LIB_REGISTER, err, errlen, "Library \"%s\" is refused: %s", name, why);
        }
    }
    if (h.register_offset != U2A3_NONE) {
        char note[LIB_ERR_MAX];
        if (unserved_text(s.info.name, rec, note, sizeof(note))) {
            api_.log(note);                             // the record is kept, nothing serves it (libapi.h): the log must not say a plain "ok"
        }
    }

    s.info.menu = menu.d;                               // the slot owns the record from here (discard frees it)
    s.info.menu_bytes = menu.b;
    menu.d = 0;
    menu.b = 0;
    s.used = true;
    count_++;
    *handle = si;
    return LIB_OK;
}

int LibLoader::run(int handle, const char *action, const char *file, char *reply, int replylen, int *app_rc, char *err, int errlen, bool keep)
{
    if (app_rc) {
        *app_rc = 0;
    }
    if (handle < 0 || handle >= LIB_MAX || !slot_[handle].used) {
        return fail(LIB_STATE, err, errlen, "Nothing is loaded under handle %d.", handle);
    }
    Slot &s = slot_[handle];
    if (s.info.kind != U2A3_KIND_APP || !s.info.main_addr) {
        return fail(LIB_NOT_APP, err, errlen, "\"%s\" is not an app.", s.info.name);
    }
    char none[1] = "";
    AppCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.struct_size = sizeof(ctx);
    ctx.action = action ? action : "";
    ctx.file = file ? file : "";
    ctx.reply = reply && replylen > 0 ? reply : none;
    ctx.reply_len = reply && replylen > 0 ? replylen : 1;
    ctx.reply[0] = 0;
    LibHost::Entry e = { s.info.name, s.info.main_addr, s.info.image_addr, 0 };
    int prev = cur_;
    cur_ = handle;
    int rc = host_->call_main(e, &api_, &ctx);
    cur_ = prev;
    ctx.reply[ctx.reply_len - 1] = 0;                   // whatever the app wrote, the line ends inside the buffer
    if (rc == LibHost::LIB_HOST_CANNOT_RUN) {
        return fail(LIB_NOT_APP, err, errlen, "This base cannot run apps (\"%s\").", s.info.name);
    }
    if (app_rc) {
        *app_rc = rc;
    }
    if (s.info.residence == U2A3_RES_TRANSIENT && !keep) {
        return release(handle, err, errlen);            // the image, every block it still held and what it registered
    }
    return LIB_OK;
}

int LibLoader::release(int handle, char *err, int errlen)
{
    if (handle < 0 || handle >= LIB_MAX || !slot_[handle].used) {
        return fail(LIB_STATE, err, errlen, "Nothing is loaded under handle %d.", handle);
    }
    Slot &s = slot_[handle];
    if (s.info.residence == U2A3_RES_RESIDENT) {
        return fail(LIB_STATE, err, errlen, "\"%s\" is resident and stays loaded.", s.info.name);
    }
    retire(s);                                          // its poll, operations, state values and events go before the code they point into
    if (cur_ == handle) {
        cur_ = -1;
    }
    discard(handle);
    count_--;
    return LIB_OK;
}

void LibLoader::use(int handle)
{
    if (handle >= 0 && handle < LIB_MAX && slot_[handle].used) {
        slot_[handle].uses++;
    }
}

void LibLoader::unuse(int handle)
{
    if (handle >= 0 && handle < LIB_MAX && slot_[handle].used && slot_[handle].uses > 0) {
        slot_[handle].uses--;
    }
}

int LibLoader::uses(int handle) const
{
    return handle >= 0 && handle < LIB_MAX && slot_[handle].used ? slot_[handle].uses : 0;
}

int LibLoader::unload(int handle, char *err, int errlen)
{
    if (handle < 0 || handle >= LIB_MAX || !slot_[handle].used) {
        return fail(LIB_STATE, err, errlen, "Nothing is loaded under handle %d.", handle);
    }
    Slot &s = slot_[handle];
    if (s.info.kind != U2A3_KIND_LIB || s.uses > 0) {
        return fail(LIB_STATE, err, errlen, "\"%s\" is not an unused library.", s.info.name);
    }
    retire(s);
    if (cur_ == handle) {
        cur_ = -1;
    }
    discard(handle);
    count_--;
    return LIB_OK;
}

int LibLoader::finish(int handle, char *err, int errlen)
{
    return release(handle, err, errlen);
}
