// T180 D9 (#43): "Unzip here" in every core. One copy of unzip.u2a in "<dir of the .cor>/apps" (APP_SHARED_FOLDER) serves every core; a core's own
// "<core folder>/apps" wins on an equal name. What is real here: AppIndex::folders / scan over the FakeFs, the menu record of the file (packed by
// tools/u2a_pack.py from apps/unzip/app.toml), the merge into the core's file types, the LibLoader that loads the file and its service tables, and the
// code of the application (apps/unzip/src, glue.cc, app/sdk/u2p_compat.c) compiled for the Mac. What is not: that RISC-V instructions run - the image the
// loader places is a fixture whose app_main is not called, LibHost::call_main runs the Mac build of the same sources (tests/loader does the same for libraries).
//
//   test_unzip FIXDIR      (FIXDIR: unzip.u2a, unzip2.u2a, t.zip, manifest.txt and files/ - tests/coreapp/run.sh makes them)
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "cores.h"
#include "fakes.h"
#include "test_core.h"
#include "bundle.h"
#include "ops_glue.h"
#include "appindex.h"
#include "libload.h"

const CoreDesc *const core_table[] = { &testcore::desc, 0 };
extern "C" int app_main(const LibApi *api, AppCtx *ctx);               // apps/unzip/glue.cc, compiled for the Mac
extern "C" int lib_register(const LibApi *api, LibRecord *out);        // apps/miniz/miniz_lib.cc: the library, the same source the board image is built from
#include "zip_lib.h"

// S2: a second application on the library. It carries no miniz: its only zip code is the table it is handed. Mac build of this function; the board image of the
// same shape is what the size check in ci/trxapps.sh looks at.
static std::string g_listing;
static const LibApi *g_zl_api;
static SvcFile *g_zl_file;
static size_t zl_rd(void *, mz_uint64 off, void *buf, size_t n)
{
    return g_zl_api->fs->read(g_zl_file, off, buf, (uint32_t)n) ? n : 0;
}
static void *zl_alloc(void *, size_t a, size_t b) { return g_zl_api->mem->malloc((uint32_t)(a * b)); }
static void zl_free(void *, void *p) { g_zl_api->mem->free(p); }
static void *zl_realloc(void *, void *p, size_t a, size_t b) { return g_zl_api->mem->realloc(p, (uint32_t)(a * b)); }
static int ziplist_main(const LibApi *api, AppCtx *ctx)
{
    uint32_t sz = 0;
    const ZipLib *zt = (const ZipLib *)api->lib->find(ZIPLIB_NAME, ZIPLIB_VERSION, &sz);
    if (!zt || !ZIPLIB_HAS(zt, file_stat)) return 2;
    g_zl_api = api;
    g_zl_file = api->fs->open(ctx->file, false);
    if (!g_zl_file) return 3;
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    z.m_pAlloc = zl_alloc; z.m_pFree = zl_free; z.m_pRealloc = zl_realloc; z.m_pRead = zl_rd;
    int rc = 4;
    if (zt->reader_init(&z, api->fs->fsize(g_zl_file), 0)) {
        g_listing.clear();
        for (mz_uint i = 0; i < zt->get_num_files(&z); i++) {
            mz_zip_archive_file_stat st;
            if (zt->file_stat(&z, i, &st)) { g_listing += st.m_filename; g_listing += "\n"; }
        }
        zt->reader_end(&z);
        rc = 0;
    }
    api->fs->close(g_zl_file);
    return rc;
}

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

struct Log : LibLog {
    std::vector<std::string> lines;
    void line(const char *t) override { lines.push_back(t); }
};

// the loader's host: runs the Mac build of the application where the board would call the image's app_main
struct Host : DescLibHost {
    std::vector<void *> live;
    explicit Host(const CoreDesc *d) : DescLibHost(d) { }
    ~Host() { for (size_t i = 0; i < live.size(); i++) free(live[i]); }
    void *alloc(uint32_t n) override { void *p = malloc(n); live.push_back(p); return p; }
    void release(void *p) override
    {
        for (size_t i = 0; i < live.size(); i++) { if (live[i] == p) { live.erase(live.begin() + i); free(p); return; } }
    }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    int call_register(const Entry &e, const LibApi *api, LibRecord *rec) override
    {
        if (!strcmp(e.name, "ringlib")) {                                  // T239.3 D20: the library that exports "ring" (its file lists the name)
            static const uint32_t table[2] = { sizeof(table), 0 };
            static const LibExportDecl list[1] = { { "ring", 1, table, sizeof(table) } };
            rec->export_list = list;
            rec->nexport_list = 1;
        }
        return !strcmp(e.name, "miniz") ? lib_register(api, rec) : 0;
    }
    int call_main(const Entry &e, const LibApi *api, AppCtx *ctx) override { return !strcmp(e.name, "ziplist") ? ziplist_main(api, ctx) : app_main(api, ctx); }
};

// a heap with a size: the loader window as the board has it (room() = what is left); a load that does not fit gets null
struct HeapHost : Host {
    uint32_t cap, used;
    int allocs;                                                            // how many blocks were ever given out: a library loaded twice shows
    std::map<void *, uint32_t> sz;
    HeapHost(const CoreDesc *d, uint32_t c) : Host(d), cap(c), used(0), allocs(0) { }
    void *alloc(uint32_t n) override
    {
        if (used + n > cap) return 0;
        allocs++;
        void *p = Host::alloc(n);
        sz[p] = n; used += n;
        return p;
    }
    void release(void *p) override { used -= sz[p]; sz.erase(p); Host::release(p); }
    uint32_t room() override { return cap - used; }
};

static std::string g_msg;                                              // the last dialog the application raised
static void d_message(const char *, const char *text) { g_msg = text; }
static void *d_open(const char *, int) { return (void *)1; }
static void d_set(void *, int, const char *) { }
static void d_close(void *) { }

static void ctx_for(CoreCtx *c, FakeFs *fs, FakeIo *io, const char *cor)
{
    memset(c, 0, sizeof(*c));
    c->desc = &testcore::desc;
    c->fs = fs;
    c->io = io;
    snprintf(c->cor_path, sizeof(c->cor_path), "%s", cor);
}

static std::string lowered(std::string s) { for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]); return s; }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    std::string dir = argv[1];
    Bytes u1 = slurp(dir + "/unzip.u2a"), u2 = slurp(dir + "/unzip2.u2a"), zip = slurp(dir + "/t.zip"), mz = slurp(dir + "/miniz.u2a"), zl = slurp(dir + "/ziplist.u2a");
    CHECK(u1.size() > 100 && u2.size() > 100 && zip.size() > 100 && mz.size() > 100 && zl.size() > 100);
    std::vector<std::string> names;
    {
        std::ifstream m((dir + "/manifest.txt").c_str());
        std::string l;
        while (std::getline(m, l)) { if (!l.empty()) names.push_back(l); }
    }
    CHECK(names.size() >= 5);

    FakeIo io;
    // ---- a core that has no copy of its own: the shared folder is the only place Unzip is
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/cores/apps/miniz.u2a", mz);
        fs.put("/sd/cores/apps/ziplist.u2a", zl);
        fs.put("/sd/games/t.zip", zip);
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        bool shared = false;
        for (int i = 0; i < nf; i++) { if (!strcmp(folders[i], "/sd/cores/apps")) shared = true; }
        CHECK(shared);
        CHECK_STR(folders[0], "/sd/cores/vc20/apps");                   // the core's own comes first
        Log log;
        AppIndex idx;
        idx.scan(&fs, folders, nf, &log);
        CHECK_EQ(idx.count(), 2);                                       // unzip and ziplist; the library is no application
        CHECK_EQ(idx.nlibs(), 1);
        CHECK(idx.find("miniz") == 0 && idx.find_lib("miniz") != 0);
        CHECK(idx.find_lib("miniz", U2A3_VERSION(2, 0)) == 0);           // too old for a 2.0
        const AppEntry *e = idx.find("unzip");
        CHECK(e != 0);
        if (e) { CHECK_STR(e->path, "/sd/cores/apps/unzip.u2a"); CHECK_EQ(e->version, U2A3_VERSION(1, 0)); }

        // the file types: ".zip" gets the three actions of apps/unzip/app.toml
        OpRegistry *reg = ops_of(&c);
        ops_register_base(*reg);
        Host host(&testcore::desc);
        LibLoader ld(&host, reg);
        CoreDesc merged;
        static MenuMerge mm;
        int refused = bundle_merge(&c, &testcore::desc, 0, &ld, &log, &merged, &mm, &idx);
        for (int i = 0; i < mm.nerr; i++) printf("refused: %s\n", mm.err[i]);
        CHECK_EQ(refused, 0);
        const FileType *zt = 0;
        for (int i = 0; i < merged.nfiletypes; i++) { if (!strcmp(merged.filetypes[i].ext, ".zip")) zt = &merged.filetypes[i]; }
        CHECK(zt != 0);
        bool here = false, folder = false, one = false;
        if (zt) {
            for (int i = 0; i < zt->nactions; i++) {
                here |= !strcmp(zt->actions[i].label, "Unzip here");
                folder |= !strcmp(zt->actions[i].label, "Unzip to folder");
                one |= !strcmp(zt->actions[i].label, "Extract one file");
            }
        }
        CHECK(here); CHECK(folder); CHECK(one);

        // S4 / D9 (e): the .zip actions are listed whenever unzip and miniz are in the folders, whatever the room is (the memory is checked at the press, S6);
        // fits() still knows the need exactly; a library missing from the folders hides the actions.
        {
            uint32_t need = e->image_bytes + idx.find_lib("miniz")->image_bytes + APP_WORK_BYTES;
            for (int pass = 0; pass < 3; pass++) {
                uint32_t room = pass == 0 ? need - 1 : need;
                AppIndex idx2;
                FakeFs fs2;
                fs2.put("/sd/cores/apps/unzip.u2a", u1);
                if (pass < 2) fs2.put("/sd/cores/apps/miniz.u2a", mz);
                static char f2[APP_FOLDERS_MAX][APP_PATH_MAX];
                CoreCtx c2;
                ctx_for(&c2, &fs2, &io, "/sd/cores/vc20.cor");
                int nf2 = AppIndex::folders(&c2, f2, APP_FOLDERS_MAX);
                idx2.scan(&fs2, f2, nf2, &log);
                idx2.set_room(room, &ld);                                // nothing is loaded in `ld` yet (the loader's service tables are one per process)
                CoreDesc merged2;
                static MenuMerge mm2;
                bundle_merge(&c2, &testcore::desc, 0, &ld, &log, &merged2, &mm2, &idx2);
                int zip_actions = 0;
                for (int i = 0; i < merged2.nfiletypes; i++) { if (!strcmp(merged2.filetypes[i].ext, ".zip")) zip_actions = merged2.filetypes[i].nactions; }
                printf("S4 pass %d: room %u (need %u): %d zip action(s)\n", pass, (unsigned)room, (unsigned)need, zip_actions);
                if (pass < 2) CHECK_EQ(zip_actions, 3); else CHECK_EQ(zip_actions, 0);
                const AppEntry *e2 = idx2.find("unzip");
                CHECK(e2 != 0);
                if (e2) CHECK_EQ(idx2.fits(*e2, room, &ld), pass == 1);
            }
        }

        // running it: load the file the index found, "here" on the zip in /sd/games
        SvcDlg dlg;
        memset(&dlg, 0, sizeof(dlg));
        dlg.size = sizeof(dlg);
        dlg.message = d_message; dlg.progress_open = d_open; dlg.progress_set = d_set; dlg.progress_close = d_close;
        ld.set_ctx(&c);
        ld.set_dlg(&dlg);
        CoreFile *cf = e ? fs.open(e->path, false) : 0;
        CHECK(cf != 0);
        if (cf) {
            CoreFileSource src(cf);
            int h = -1;
            char err[240];
            int rc = ld.load(&src, &h, err, sizeof(err));                // the library is not in memory yet: the loader says what is missing
            CHECK_EQ(rc, LIB_NEEDS);
            CHECK(strstr(err, "miniz") != 0);
            CHECK_EQ(ld.count(), 0);
            CHECK(idx.load_needs(&fs, &ld, *e, err, sizeof(err)));       // what AppHost::open_now does first
            CHECK_EQ(ld.count(), 1);
            CHECK(ld.handle_of("miniz") >= 0);
            rc = ld.load(&src, &h, err, sizeof(err));
            fs.close(cf);
            if (rc != LIB_OK) printf("load: %d %s\n", rc, err);
            CHECK_EQ(rc, LIB_OK);
            // a file the zip also holds, there before: Unzip writes over it without asking (what it does today; the report says so)
            fs.put("/sd/games/" + names[0], Bytes(5, 0x55));
            char reply[200] = "";
            int app_rc = -1;
            g_msg.clear();
            rc = ld.run(h, "here", "/sd/games/t.zip", reply, sizeof(reply), &app_rc, err, sizeof(err));
            if (rc != LIB_OK) printf("run: %d %s\n", rc, err);
            CHECK_EQ(rc, LIB_OK);
            CHECK_EQ(app_rc, 0);
            printf("dialog: %s\n", g_msg.c_str());
            int ok = 0, absent = 0;
            for (size_t i = 0; i < names.size(); i++) {
                Bytes want = slurp(dir + "/files/" + names[i]);
                std::string path = "/sd/games/" + names[i];
                bool toolong = path.size() >= 256;                      // unzip.c joins into 256 bytes: refused, never cut
                if (toolong) {
                    absent++;
                    CHECK(fs.files.find(lowered(path)) == fs.files.end());
                    CHECK(fs.files.find(lowered(path.substr(0, 255))) == fs.files.end());
                } else {
                    std::map<std::string, Bytes>::iterator f = fs.files.find(lowered(path));
                    CHECK(f != fs.files.end());
                    if (f != fs.files.end()) { CHECK(f->second == want); ok += f->second == want; }
                }
            }
            CHECK(ok >= 4);
            CHECK(g_msg.find("Extracted " + std::to_string(ok) + " files into /sd/games") != std::string::npos);
            if (absent) CHECK(g_msg.find("Failed: " + std::to_string(absent)) != std::string::npos);
            CHECK(fs.files.find("/sd/games/t.zip") != fs.files.end());
            CHECK(fs.files.find("/sd/t.zip") == fs.files.end());          // nothing next to the wrong folder
            CHECK_EQ(fs.open_files, 0);                                   // every file the application opened is closed again
            ld.release(h, err, sizeof(err));
            CHECK_EQ(ld.count(), 1);                                      // the library stays (resident)

            // S2: a second application on the same loaded library lists the entries through the table; it holds no miniz of its own
            const AppEntry *ze = idx.find("ziplist");
            CHECK(ze != 0);
            if (ze) {
                CHECK(idx.load_needs(&fs, &ld, *ze, err, sizeof(err)));
                CHECK_EQ(ld.count(), 1);                                  // miniz was there: nothing loaded twice
                CoreFile *zf = fs.open(ze->path, false);
                CoreFileSource zsrc(zf);
                int zh = -1;
                CHECK_EQ(ld.load(&zsrc, &zh, err, sizeof(err)), LIB_OK);
                fs.close(zf);
                g_listing.clear();
                CHECK_EQ(ld.run(zh, "", "/sd/games/t.zip", reply, sizeof(reply), &app_rc, err, sizeof(err)), LIB_OK);
                CHECK_EQ(app_rc, 0);
                for (size_t i = 0; i < names.size(); i++) CHECK(g_listing.find(names[i] + "\n") != std::string::npos);
                CHECK_EQ(fs.open_files, 0);
            }
        }
    }

    // ---- S6 / S7 (T197 D8): memory short at the press. A model heap gives the loader window; the same AppIndex functions the board calls (prepare_open,
    // use counts are the loader's) run over the real loader.
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/cores/apps/miniz.u2a", mz);
        fs.put("/sd/cores/apps/ziplist.u2a", zl);
        fs.put("/sd/cores/apps/otherlib.u2a", slurp(dir + "/otherlib.u2a"));
        fs.put("/sd/cores/apps/plain.u2a", slurp(dir + "/plain.u2a"));
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        Log log;
        AppIndex idx;
        idx.scan(&fs, folders, nf, &log);
        CHECK_EQ(idx.nlibs(), 2);
        const AppEntry *uz = idx.find("unzip"), *pl = idx.find("plain");
        CHECK(uz != 0 && pl != 0);
        const LibFile *mzf = idx.find_lib("miniz"), *olf = idx.find_lib("otherlib");
        CHECK(mzf != 0 && olf != 0);
        if (uz && pl && mzf && olf) {
            OpRegistry *reg = ops_of(&c);
            ops_register_base(*reg);
            char err[LIB_ERR_MAX];
            // the heap holds exactly unzip + miniz + work, plus otherlib: after otherlib is loaded and miniz is not, unzip does not fit until otherlib is evicted
            uint32_t need = uz->image_bytes + mzf->image_bytes + APP_WORK_BYTES;
            uint32_t cap = need + olf->image_bytes - 1;
            {
                HeapHost hh(&testcore::desc, cap);
                LibLoader ld(&hh, reg);
                // (a) otherlib (count 0) is evicted, then unzip loads
                CHECK(idx.load_needs(&fs, &ld, *pl, err, sizeof(err)));            // nothing to load for plain
                CoreFile *of = fs.open(olf->path, false);
                CoreFileSource osrc(of);
                int oh = -1;
                CHECK_EQ(ld.load(&osrc, &oh, err, sizeof(err)), LIB_OK);
                fs.close(of);
                CHECK(!idx.fits(*uz, ld.room(), &ld));
                CHECK(idx.prepare_open(&fs, &ld, *uz, err, sizeof(err)));
                CHECK(ld.handle_of("otherlib") < 0);
                CHECK(ld.handle_of("miniz") >= 0);
                printf("S6a: otherlib evicted, miniz loaded, room %u\n", (unsigned)ld.room());
                // (d) the count: taken by the loader when unzip loads, back to 0 when it is released
                CoreFile *uf = fs.open(uz->path, false);
                CoreFileSource usrc(uf);
                int uh = -1;
                CHECK_EQ(ld.load(&usrc, &uh, err, sizeof(err)), LIB_OK);
                fs.close(uf);
                CHECK_EQ(ld.uses(ld.handle_of("miniz")), 1);
                // (b) in use: never evicted, even when plain needs the room
                CHECK_EQ(ld.unload(ld.handle_of("miniz"), err, sizeof(err)), LIB_STATE);
                CHECK(ld.handle_of("miniz") >= 0);
                // (c) still too small after eviction: nothing loads, the message has the numbers
                {
                    HeapHost tiny(&testcore::desc, pl->image_bytes + APP_WORK_BYTES - 1);
                    LibLoader ld2(&tiny, reg);
                    CHECK(!idx.prepare_open(&fs, &ld2, *pl, err, sizeof(err)));
                    char want[160];
                    snprintf(want, sizeof(want), "Not enough memory for plain (needs %u KB, %u KB free).", (unsigned)((pl->image_bytes + APP_WORK_BYTES + 1023) / 1024),
                             (unsigned)(tiny.room() / 1024));
                    CHECK_STR(err, want);
                    CHECK_EQ(ld2.count(), 0);
                    printf("S6c: %s\n", err);
                }
                CHECK_EQ(ld.release(uh, err, sizeof(err)), LIB_OK);
                CHECK_EQ(ld.uses(ld.handle_of("miniz")), 0);                       // (d)
            }
            // (b) refusal while in use: a heap that holds unzip + miniz and nothing else; plain would need more than is left, miniz is held by unzip
            {
                HeapHost hh(&testcore::desc, mzf->image_bytes + pl->image_bytes + APP_WORK_BYTES - 1);
                LibLoader ld(&hh, reg);
                CHECK(idx.load_needs(&fs, &ld, *uz, err, sizeof(err)));            // miniz is in; unzip itself is not loaded in this model
                ld.use(ld.handle_of("miniz"));                                     // a loaded application counts it (the loader does that at load)
                uint32_t room_before = ld.room();
                CHECK(pl->image_bytes + APP_WORK_BYTES > room_before);             // plain does not fit beside the held miniz
                CHECK(!idx.prepare_open(&fs, &ld, *pl, err, sizeof(err)));
                CHECK(strstr(err, "Not enough memory for plain") != 0);
                CHECK(ld.handle_of("miniz") >= 0);                                 // in use: kept
                CHECK_EQ(ld.room(), room_before);
                // the application ended: miniz has count 0, the same open now clears it away and plain loads
                ld.unuse(ld.handle_of("miniz"));
                CHECK(idx.prepare_open(&fs, &ld, *pl, err, sizeof(err)));
                CHECK(ld.handle_of("miniz") < 0);
                printf("S6b/S7: in use kept, freed after the end, then evicted\n");
            }
            // a library that declares a poll, or a file action, is never evicted: unzip is refused, by name and with the figures, and the library stays loaded
            for (int which = 0; which < 2; which++) {
                const char *nm = which ? "menulib" : "pollib";
                fs.put(std::string("/sd/cores/apps/") + nm + ".u2a", slurp(dir + "/" + nm + ".u2a"));
                HeapHost hh(&testcore::desc, 1u << 24);
                LibLoader ld(&hh, reg);
                CoreFile *lf = fs.open((std::string("/sd/cores/apps/") + nm + ".u2a").c_str(), false);
                CoreFileSource lsrc(lf);
                int lh = -1;
                CHECK_EQ(ld.load(&lsrc, &lh, err, sizeof(err)), LIB_OK);
                fs.close(lf);
                CHECK(lh >= 0 && ld.contributes(lh));
                hh.cap = hh.used + uz->image_bytes + mzf->image_bytes + APP_WORK_BYTES - 1;
                CHECK(!idx.prepare_open(&fs, &ld, *uz, err, sizeof(err)));
                CHECK(strstr(err, "Not enough memory for unzip (needs ") != 0);
                CHECK(ld.handle_of(nm) >= 0);
                printf("S6e: %s with a %s is not evicted; %s\n", nm, which ? "file action" : "poll", err);
            }
            // a library that contributes nothing is evictable (S6a above), and the loader says so
            {
                HeapHost hh(&testcore::desc, 1u << 24);
                LibLoader ld(&hh, reg);
                CoreFile *lf = fs.open(olf->path, false);
                CoreFileSource lsrc(lf);
                int lh = -1;
                CHECK_EQ(ld.load(&lsrc, &lh, err, sizeof(err)), LIB_OK);
                fs.close(lf);
                CHECK(!ld.contributes(lh));
            }
        }
    }

    // ---- T239.3 D20: an application that needs an export name ("ring>=1") is served by the file of the apps folder that exports it. The same AppIndex functions the
    // board calls: find_lib_for (through need_bytes and load_needs), handle_providing and the eviction of prepare_open, over the real loader.
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/ringapp.u2a", slurp(dir + "/ringapp.u2a"));
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        Log log;
        AppIndex idx;
        idx.scan(&fs, folders, nf, &log);
        const AppEntry *ra = idx.find("ringapp");
        CHECK(ra != 0 && ra->nneeds == 1);
        OpRegistry *reg = ops_of(&c);
        ops_register_base(*reg);
        char err[LIB_ERR_MAX];
        {                                                                   // no file exports "ring": not available, and the refusal names the need
            HeapHost hh(&testcore::desc, 1u << 24);
            LibLoader ld(&hh, reg);
            CHECK(ra && !idx.available(*ra, &ld));
            CHECK(ra && !idx.load_needs(&fs, &ld, *ra, err, sizeof(err)));
            CHECK(strstr(err, "needs the library \"ring\" 1.0, which is not in the apps folder") != 0);
        }
        fs.put("/sd/cores/apps/ringlib.u2a", slurp(dir + "/ringlib.u2a"));
        fs.put("/sd/cores/apps/otherlib.u2a", slurp(dir + "/otherlib.u2a"));
        idx.scan(&fs, folders, nf, &log);
        ra = idx.find("ringapp");
        const LibFile *rl = idx.find_lib("ringlib"), *ol = idx.find_lib("otherlib");
        CHECK(ra != 0 && rl != 0 && ol != 0 && idx.find_lib("ring") == 0);   // the export name is no file name: only find_lib_for sees it
        CHECK(rl && rl->nexports == 1 && !strcmp(rl->exports[0].name, "ring"));
        if (ra && rl && ol) {
            uint32_t need = ra->image_bytes + rl->image_bytes + APP_WORK_BYTES;
            {                                                               // counted in the fit: the exporter's bytes are part of the need
                HeapHost hh(&testcore::desc, 1u << 24);
                LibLoader ld(&hh, reg);
                CHECK(idx.available(*ra, &ld));
                CHECK(idx.fits(*ra, need, &ld));
                CHECK(!idx.fits(*ra, need - 1, &ld));
                // found and loaded by the export name; the loader then answers handle_providing and the application's need is met
                CHECK(idx.prepare_open(&fs, &ld, *ra, err, sizeof(err)));
                int h = ld.handle_of("ringlib");
                CHECK(h >= 0 && ld.handle_providing("ring", U2A3_VERSION(1, 0)) == h);
                CHECK(idx.fits(*ra, ld.room(), &ld));                      // in memory now: its bytes are gone from the room, not counted twice
                printf("D20: ringapp found ringlib by the name ring and loaded it\n");
            }
            {                                                               // prepare_open evicts what nothing needs, never the exporter the application asks for
                HeapHost hh(&testcore::desc, need + ol->image_bytes - 1);
                LibLoader ld(&hh, reg);
                int lh = -1;
                for (int k = 0; k < 2; k++) {
                    CoreFile *f = fs.open((k ? ol : rl)->path, false);
                    CoreFileSource src(f);
                    CHECK_EQ(ld.load(&src, &lh, err, sizeof(err)), LIB_OK);
                    fs.close(f);
                }
                int rh = ld.handle_of("ringlib");
                CHECK(rh >= 0 && ld.handle_of("otherlib") >= 0);
                CHECK(!idx.fits(*ra, ld.room(), &ld));
                int before = hh.allocs;
                CHECK(idx.prepare_open(&fs, &ld, *ra, err, sizeof(err)));
                CHECK(ld.handle_of("otherlib") < 0);                        // evicted
                CHECK_EQ(ld.handle_of("ringlib"), rh);                      // the exporter stays ...
                CHECK_EQ(hh.allocs, before);                                // ... and was not evicted and loaded again (a freed slot would be taken by the same handle)
                CHECK_EQ(ld.count(), 1);
                printf("D20: otherlib evicted, ringlib kept (it provides ring)\n");
            }
        }
    }

    // ---- T197 D8 review fixes: a resident application and a library imported by a library keep their library; the refusal numbers
    {
        FakeFs fs;
        const char *names[] = { "unzip", "miniz", "ziplist", "otherlib", "plain", "outerlib", "resapp" };
        for (int i = 0; i < 7; i++) {
            fs.put(std::string("/sd/cores/apps/") + names[i] + ".u2a", slurp(dir + "/" + names[i] + ".u2a"));
        }
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        AppIndex idx;
        idx.scan(&fs, folders, nf, 0);
        const AppEntry *ra = idx.find("resapp"), *pl = idx.find("plain");
        const LibFile *olf = idx.find_lib("outerlib");
        CHECK(ra != 0 && pl != 0 && olf != 0);
        if (ra && pl && olf) {
            OpRegistry *reg = ops_of(&c);
            ops_register_base(*reg);
            char err[LIB_ERR_MAX];
            // (1) a resident application: its hold is the loader's, so it survives the end of the AppHost's session and a later open evicts around it
            {
                HeapHost hh(&testcore::desc, 1u << 20);
                LibLoader ld(&hh, reg);
                CHECK(idx.prepare_open(&fs, &ld, *ra, err, sizeof(err)));
                CoreFile *f = fs.open(ra->path, false);
                CoreFileSource src(f);
                int rh = -1;
                CHECK_EQ(ld.load(&src, &rh, err, sizeof(err)), LIB_OK);
                fs.close(f);
                int mh = ld.handle_of("miniz");
                CHECK_EQ(ld.uses(mh), 1);
                CHECK_EQ(ld.release(rh, err, sizeof(err)), LIB_STATE);             // resident: end_application does not free it, so nothing gives the hold back
                CHECK_EQ(ld.uses(mh), 1);
                CHECK_EQ(ld.unload(mh, err, sizeof(err)), LIB_STATE);              // not evictable under it
                // reopen of the resident application: it is found loaded, its library is still there and still held (no second count, none lost)
                CHECK_EQ(ld.handle_of("resapp"), rh);
                CHECK_EQ(ld.uses(ld.handle_of("miniz")), 1);
                // evicting pass for a bigger app on a tight heap leaves it alone
                CHECK(idx.prepare_open(&fs, &ld, *pl, err, sizeof(err)));
                CHECK(ld.handle_of("miniz") >= 0);
            }
            // (2) a library imported by a library: outerlib holds miniz while loaded; unloading outerlib frees it
            {
                HeapHost hh(&testcore::desc, 1u << 20);
                LibLoader ld(&hh, reg);
                CHECK(idx.load_needs(&fs, &ld, *idx.find("ziplist"), err, sizeof(err)));       // miniz
                CoreFile *f = fs.open(olf->path, false);
                CoreFileSource src(f);
                int oh = -1;
                CHECK_EQ(ld.load(&src, &oh, err, sizeof(err)), LIB_OK);
                fs.close(f);
                int mh = ld.handle_of("miniz");
                CHECK_EQ(ld.uses(mh), 1);
                CHECK_EQ(ld.unload(mh, err, sizeof(err)), LIB_STATE);              // imported by outerlib
                CHECK_EQ(ld.unload(oh, err, sizeof(err)), LIB_OK);
                CHECK_EQ(ld.uses(ld.handle_of("miniz")), 0);
                CHECK_EQ(ld.unload(ld.handle_of("miniz"), err, sizeof(err)), LIB_OK);
                // eviction under room pressure: outerlib (unused) goes first, then miniz it alone held, in one prepare_open
                CoreFile *g = fs.open(idx.find_lib("miniz")->path, false);
                CoreFileSource gs(g);
                int mh2 = -1;
                CHECK_EQ(ld.load(&gs, &mh2, err, sizeof(err)), LIB_OK);
                fs.close(g);
                f = fs.open(olf->path, false);
                CoreFileSource src2(f);
                CHECK_EQ(ld.load(&src2, &oh, err, sizeof(err)), LIB_OK);
                fs.close(f);
                uint32_t cap = hh.used + pl->image_bytes + APP_WORK_BYTES - 1;       // plain does not fit beside the two libraries
                hh.cap = cap;
                CHECK(idx.prepare_open(&fs, &ld, *pl, err, sizeof(err)));
                CHECK(ld.handle_of("outerlib") < 0 && ld.handle_of("miniz") < 0);
                printf("D8: lib imported by lib kept while the importer is loaded, both evicted when idle\n");
            }
            // (3) the refusal: needs (rounded up) is always above free (rounded down), also when the bytes differ by one
            for (uint32_t over = 1; over <= 1024; over += 511) {
                HeapHost tiny(&testcore::desc, pl->image_bytes + APP_WORK_BYTES - over);
                LibLoader ld(&tiny, reg);
                CHECK(!idx.prepare_open(&fs, &ld, *pl, err, sizeof(err)));
                unsigned nk = 0, fk = 0;
                CHECK_EQ(sscanf(strstr(err, "(needs ") + 7, "%u KB, %u KB free", &nk, &fk), 2);
                CHECK(nk > fk);
            }
        }
    }

    // ---- no shared folder on the medium: a core does not find Unzip (the control for the above)
    {
        FakeFs fs;
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        AppIndex idx;
        idx.scan(&fs, folders, nf, 0);
        CHECK_EQ(idx.count(), 0);
    }

    // ---- a copy in the core's own folder wins over the shared one (version 2 against 1), and the other core still gets the shared one
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/cores/vc20/apps/unzip.u2a", u2);
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        AppIndex idx;
        idx.scan(&fs, folders, nf, 0);
        const AppEntry *e = idx.find("unzip");
        CHECK(e != 0);
        if (e) { CHECK_STR(e->path, "/sd/cores/vc20/apps/unzip.u2a"); CHECK_EQ(e->version, U2A3_VERSION(2, 0)); }
        CoreCtx c2;
        ctx_for(&c2, &fs, &io, "/sd/cores/amiga1200.cor");
        nf = AppIndex::folders(&c2, folders, APP_FOLDERS_MAX);
        AppIndex idx2;
        idx2.scan(&fs, folders, nf, 0);
        e = idx2.find("unzip");
        CHECK(e != 0);
        if (e) { CHECK_STR(e->path, "/sd/cores/apps/unzip.u2a"); CHECK_EQ(e->version, U2A3_VERSION(1, 0)); }
    }

    // ---- a start without a .cor (JTAG, ue2emu): every root's "cores/apps" is in the list
    {
        FakeFs fs;
        CoreCtx c;
        ctx_for(&c, &fs, &io, "");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        bool sd = false, usb1 = false;
        for (int i = 0; i < nf; i++) { sd |= !strcmp(folders[i], "/sd/cores/apps"); usb1 |= !strcmp(folders[i], "/usb1/cores/apps"); }
        CHECK(sd); CHECK(usb1);
    }
    DONE("test_unzip");
}
