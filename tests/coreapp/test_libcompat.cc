// T262, SDK 0.5.0 and 0.5.1 (base 3.5, 3.6), kept for base API 4.0 (T239.3 P2-6, D57, D58): a program built against the table of base 3.4 or 3.5 still runs on this base, and the additions are told by what the table carries.
//   S1 an application image of abi 4 that needs `base >= 3.4` loads on this base; one that needs `base >= 4.1` is refused by a base of 4.0, with the sentence that names both; an image of
//      major 3 is refused (no fallback, D57), and a file of kind lib is refused by load (D58: base 4.0 loads no library file)
//   S2 the program's code, compiled against the 3.4 headers (the frozen struct layouts of this file, written from them), calls api->ctx through the old layout: every member it
//      knows is where it was and does what it did; the new table is the old one with one member appended (the old size is a prefix, the old members have the same offsets)
//   S3 the new member is found by SVC_HAS and absent in a table of the old size
//   S4 the LibRecord did not change (a record of the size a 3.4 library writes is the whole of it), the node kinds of 3.4 keep their values and the new one is appended; a record of
//      another size, written by a library the app is linked with (LibLoader::link), is refused
#include <string>
#include <vector>
#define private public                          // the test reads the LibApi the loader hands libraries
#include "libload.h"
#undef private
#include "fakes.h"
#include "check.h"
#include "ops.h"
#include "ops_glue.h"
#include "svc_ctx.h"

const CoreDesc *const core_table[] = { 0 };

struct Host : LibHost {
    void *alloc(uint32_t n) override { return malloc(n); }
    void release(void *p) override { free(p); }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "Model Core"; }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }
};

// The call table `ctx` as base 3.4 declared it (svc_ctx.h of SDK 0.4.7): a library built then knows exactly these members, in this order.
struct SvcCtx34 {
    uint32_t size;
    uint32_t (*state)(void);
    void (*set_paused)(bool on);
    bool (*notice)(const char *text, bool only_if_free);
    const char *(*media_dir)(void);
    bool (*target_path)(const char *name, const char *ext, char *out, int n);
    bool (*core_folder)(const char *sub, char *out, int n);
    bool (*job_step)(void);
    bool (*job_cancelled)(void);
    void (*job_progress)(int pct, const char *path);
    bool (*media_source)(const char *slot, char *buf, int len);
    void (*mount_notify)(const char *slot);
};
// The record as base 3.4 had it: the last member is tab_layouts, and the size is the size of this base's record (nothing was appended in 3.5).
static const size_t record34_size = offsetof(LibRecord, tab_layouts) + sizeof(((const LibRecord *)0)->tab_layouts);

struct MemSource : LibSource {
    std::vector<uint8_t> d;
    uint32_t size() override { return (uint32_t)d.size(); }
    bool read(uint32_t off, void *dst, uint32_t n) override
    {
        if (off > d.size() || n > d.size() - off) { return false; }
        memcpy(dst, d.data() + off, n);
        return true;
    }
};
static void put32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; i++) { v.push_back((uint8_t)(x >> (8 * i))); } }
static void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void putname(std::vector<uint8_t> &v, const char *s) { for (int i = 0; i < 16; i++) { v.push_back(i < (int)strlen(s) ? s[i] : 0); } }
// An application image (the shape tests/coreapp/test_wblib.cc builds) with one need: `base` at the given version; abi_major is what the file was built for, `kind` its kind.
static std::vector<uint8_t> image(const char *name, uint32_t need_major, uint32_t need_minor, uint16_t abi_major = 4, uint8_t kind = U2A3_KIND_APP)
{
    const uint32_t needs_off = U2A3_HEADER_SIZE, pay_off = U2A3_HEADER_SIZE + U2A3_NEED_SIZE, pay_len = 64;
    const bool app = kind == U2A3_KIND_APP;
    std::vector<uint8_t> v;
    put32(v, U2A3_MAGIC); put16(v, U2A3_HEADER_SIZE); put16(v, 3); put16(v, abi_major); put16(v, abi_major);
    v.push_back(kind); v.push_back(app ? U2A3_RES_TRANSIENT : U2A3_RES_RESIDENT); v.push_back(U2A3_ARCH_RISCV32); v.push_back(1);
    put32(v, 0); putname(v, name); put32(v, U2A3_VERSION(1, 0)); putname(v, "");
    put32(v, pay_off); put32(v, pay_len); put32(v, 32); put32(v, 16); put32(v, app ? U2A3_NONE : 0); put32(v, app ? 0 : U2A3_NONE);
    put32(v, pay_off + pay_len); put32(v, 0); put32(v, needs_off); put32(v, 0); put32(v, 0); put32(v, 0);
    putname(v, "base"); put32(v, U2A3_VERSION(need_major, need_minor));
    v.resize(v.size() + pay_len, 0);
    return v;
}

// A register function of a linked library that writes a record of another layout (a size that is not this base's).
static int reg_old_layout(const LibApi *, LibRecord *rec)
{
    rec->struct_size = (uint32_t)record34_size - 4;
    return 0;
}
static int reg_plain(const LibApi *, LibRecord *) { return 0; }

static int g_paused_calls, g_notices, g_mounts;
static std::string g_slot;

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    // S1: the needs
    {
        FakeFs fs; Host host; OpRegistry reg; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.fs = &fs;
        LibLoader ld(&host, &reg, U2A_BASE_MINOR);
        ld.add_services();
        ld.set_ctx(&c);
        MemSource old;
        old.d = image("oldapp", 3, 4);                               // built against 3.4: base 4.0 is newer
        int h = -1;
        char err[LIB_ERR_MAX] = "";
        CHECK_EQ(ld.load(&old, &h, err, LIB_ERR_MAX), 0);
        CHECK_EQ(ld.api_.abi_major, 4);
        CHECK_EQ(ld.api_.abi_minor, U2A_BASE_MINOR);
        MemSource fresh;
        fresh.d = image("newapp", 3, 6);                             // built against 3.6 (SDK 0.5.1): this base takes it
        int h2 = -1;
        CHECK_EQ(ld.load(&fresh, &h2, err, LIB_ERR_MAX), 0);
        MemSource newest;
        newest.d = image("base40app", 4, 0);                         // built against 4.0: this base is that
        int h6 = -1;
        CHECK_EQ(ld.load(&newest, &h6, err, LIB_ERR_MAX), 0);
        MemSource future;
        future.d = image("base41app", 4, 1);                         // needs 4.1: this base is 4.0 and says so
        int h3 = -1;
        err[0] = 0;
        CHECK_EQ(ld.load(&future, &h3, err, LIB_ERR_MAX), LIB_NEEDS);
        CHECK(strstr(err, "needs base 4.1, this base is 4.0") != 0);
        MemSource major3;
        major3.d = image("major3app", 3, 4, 3);                      // built for base API 3: refused, nothing of major 3 runs (D57)
        err[0] = 0;
        CHECK_EQ(ld.load(&major3, &h3, err, LIB_ERR_MAX), LIB_ABI);
        CHECK(strstr(err, "built for base API 3") != 0 && strstr(err, "rebuild it") != 0);
        MemSource libfile;
        libfile.d = image("libfile", 3, 4, 4, U2A3_KIND_LIB);        // a library file: refused whatever it needs (D58)
        err[0] = 0;
        CHECK_EQ(ld.load(&libfile, &h3, err, LIB_ERR_MAX), LIB_BAD_FILE);
        CHECK(strstr(err, "\"libfile\" is a library file: base 4.0 loads no library file; a library is linked into the core's app through its TOML B.") != 0);
        CHECK_EQ(ld.count(), 3);                                     // the refusals left nothing behind
    }
    // S2, S3: the library's view of the table
    {
        FakeFs fs; Host host; OpRegistry reg; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.fs = &fs;
        c.paused = false;
        LibLoader ld(&host, &reg, U2A_BASE_MINOR);
        ld.add_services();
        ld.set_ctx(&c);
        const SvcCtx34 *old = (const SvcCtx34 *)ld.api_.ctx;        // the library's code: a pointer of the old layout to the table the base hands it
        CHECK(old->size >= sizeof(SvcCtx34));                        // the check of a 3.4 library: the table has at least what it knows
        CHECK(old->state && old->set_paused && old->notice && old->media_dir && old->target_path && old->core_folder && old->job_step && old->job_cancelled &&
              old->job_progress && old->media_source && old->mount_notify);
        const SvcCtx *now = ld.api_.ctx;
        CHECK(old->state == now->state && old->mount_notify == now->mount_notify && old->media_source == now->media_source);      // the same members at the same offsets
        CHECK_EQ(offsetof(SvcCtx, mount_notify), offsetof(SvcCtx34, mount_notify));
        CHECK_EQ(offsetof(SvcCtx, ask_list), sizeof(SvcCtx34));      // the new member is the next one: the old size is a prefix
        CHECK_EQ(now->size, sizeof(SvcCtx));
        old->set_paused(true);                                        // an old library's calls do what they did
        CHECK(c.paused);
        CHECK((old->state() & CTX_PAUSED) != 0);
        CHECK(old->notice("hello", true) && !old->notice("again", true));
        CHECK_STR(c.notice, "hello");
        CHECK(SVC_HAS(now, ask_list) && SVC_HAS(now, browse_at) && SVC_HAS(now, browse_sheet));   // a library of this base finds the members ...
        CHECK_EQ(offsetof(SvcCtx, browse_at), offsetof(SvcCtx, ask_list) + sizeof(now->ask_list));      // 3.6 appends after 3.5's last member
        CHECK_EQ(offsetof(SvcCtx, browse_sheet), offsetof(SvcCtx, browse_at) + sizeof(now->browse_at));
        SvcCtx as35 = *now;
        as35.size = offsetof(SvcCtx, browse_at);                      // the table a 3.5 library knows: ask_list is its last member
        CHECK(SVC_HAS(&as35, ask_list) && !SVC_HAS(&as35, browse_at) && !SVC_HAS(&as35, browse_sheet));
        SvcCtx shrunk = *now;
        shrunk.size = sizeof(SvcCtx34);                               // ... and a table of the old size does not have it
        CHECK(!SVC_HAS(&shrunk, ask_list));
        CHECK(SVC_HAS(&shrunk, mount_notify));
        // S4: the record
        CHECK_EQ(record34_size, sizeof(LibRecord));                  // the record did not grow in 3.5: a library of 3.4 writes the whole of it
        LibRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.struct_size = (uint32_t)record34_size;
        CHECK(LIB_RECORD_COVERS(rec.struct_size, tab_layouts));
        CHECK_EQ((int)LN_GROUP, 4);                                  // the kinds of 3.4 keep their values; the new one is appended
        CHECK_EQ((int)LN_LIST, 5);
    }
    // S4b: a library the app is linked with is held to the record of this base: a record of another size is refused through link(); a record of this size is taken
    {
        FakeFs fs; Host host; OpRegistry reg; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.fs = &fs;
        LibLoader ld(&host, &reg, U2A_BASE_MINOR);
        ld.add_services();
        ld.set_ctx(&c);
        char err[LIB_ERR_MAX] = "";
        CHECK_EQ(ld.link("oldrec", reg_old_layout, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE, 0, 0, err, LIB_ERR_MAX), LIB_REGISTER);
        CHECK(strstr(err, "Library \"oldrec\" was built against another record layout") != 0);
        CHECK_EQ(ld.handle_of("oldrec"), -1);                          // nothing of it stays
        err[0] = 0;
        CHECK_EQ(ld.link("plainrec", reg_plain, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE, 0, 0, err, LIB_ERR_MAX), LIB_OK);
        CHECK(ld.handle_of("plainrec") >= 0);
        CHECK_EQ(ld.info(ld.handle_of("plainrec"))->record.struct_size, sizeof(LibRecord));
    }
    DONE("test_libcompat");
}
