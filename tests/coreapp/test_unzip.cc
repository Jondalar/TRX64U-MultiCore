// T180 D9 (#43): "Unzip here" in every core. One copy of unzip.u2a in "<dir of the .cor>/apps" (APP_SHARED_FOLDER) serves every core; a core's own
// "<core folder>/apps" wins on an equal name. What is real here: AppIndex::folders / scan over the FakeFs, the menu record of the file (packed by
// tools/u2a_pack.py from apps/unzip/app.toml), the merge into the core's file types, the LibLoader that loads the file and binds its needs to the slot table, and the
// code of the application (apps/unzip/src, glue.cc, app/sdk/u2p_compat.c) compiled for the Mac, with miniz (apps/miniz), as the image of unzip.u2a carries it (T239.3 D60: the sub-app has its own miniz, no core app links one). What is not: that RISC-V instructions run - the image the
// loader places is a fixture whose app_main is not called, LibHost::call_main runs the Mac build of the same sources (tests/loader does the same for libraries).
//
//   test_unzip FIXDIR      (FIXDIR: unzip.u2a, unzip2.u2a, plain.u2a, ringapp.u2a, libfile.u2a, t.zip, manifest.txt and files/ - tests/coreapp/run.sh makes them)
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
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }   // no library file is loaded (D58)
    int call_main(const Entry &e, const LibApi *api, AppCtx *ctx) override { return app_main(api, ctx); }
};

// a heap with a size: the loader window as the board has it (room() = what is left); a load that does not fit gets null
struct HeapHost : Host {
    uint32_t cap, used;
    std::map<void *, uint32_t> sz;
    HeapHost(const CoreDesc *d, uint32_t c) : Host(d), cap(c), used(0) { }
    void *alloc(uint32_t n) override
    {
        if (used + n > cap) return 0;
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
    Bytes u1 = slurp(dir + "/unzip.u2a"), u2 = slurp(dir + "/unzip2.u2a"), zip = slurp(dir + "/t.zip");
    CHECK(u1.size() > 100 && u2.size() > 100 && zip.size() > 100);
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
        fs.put("/sd/cores/apps/libfile.u2a", slurp(dir + "/libfile.u2a"));
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
        CHECK_EQ(idx.count(), 1);                                       // unzip; the library file is no application and is not listed (D58)
        CHECK(idx.find("libfile") == 0);
        bool d58 = false;
        for (size_t i = 0; i < log.lines.size(); i++) {
            d58 |= log.lines[i].find("/sd/cores/apps/libfile.u2a: \"libfile\" is a library file; base 4.0 loads no library file") != std::string::npos;
        }
        CHECK(d58);
        const AppEntry *e = idx.find("unzip");
        CHECK(e != 0);
        if (e) { CHECK_STR(e->path, "/sd/cores/apps/unzip.u2a"); CHECK_EQ(e->version, U2A3_VERSION(1, 0)); CHECK_EQ(e->nneeds, 0); }   // the base is no entry of needs; unzip needs nothing else (D60)

        // the file types: ".zip" gets the three actions of apps/unzip/app.toml
        OpRegistry *reg = ops_of(&c);
        ops_register_base(*reg);
        Host host(&testcore::desc);
        LibLoader ld(&host, reg);
        // unzip needs the base only: nothing to link, nothing to load
        CHECK(e && idx.available(*e, &ld));
        {
            char err[LIB_ERR_MAX] = "";
            CHECK(e && idx.needs_linked(*e, err, sizeof(err)));
        }
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

        // S4 / D9 (e): the .zip actions are listed whenever the need is linked, whatever the room is (the memory is checked at the press, S6); fits() knows the need
        // exactly: the image and APP_WORK_BYTES, no library bytes (nothing is loaded for a need).
        {
            uint32_t need = e->image_bytes + APP_WORK_BYTES;
            for (int pass = 0; pass < 2; pass++) {
                uint32_t room = pass == 0 ? need - 1 : need;
                AppIndex idx2;
                FakeFs fs2;
                fs2.put("/sd/cores/apps/unzip.u2a", u1);
                static char f2[APP_FOLDERS_MAX][APP_PATH_MAX];
                CoreCtx c2;
                ctx_for(&c2, &fs2, &io, "/sd/cores/vc20.cor");
                int nf2 = AppIndex::folders(&c2, f2, APP_FOLDERS_MAX);
                idx2.scan(&fs2, f2, nf2, &log);
                idx2.set_room(room, &ld);
                CoreDesc merged2;
                static MenuMerge mm2;
                bundle_merge(&c2, &testcore::desc, 0, &ld, &log, &merged2, &mm2, &idx2);
                int zip_actions = 0;
                for (int i = 0; i < merged2.nfiletypes; i++) { if (!strcmp(merged2.filetypes[i].ext, ".zip")) zip_actions = merged2.filetypes[i].nactions; }
                printf("S4 pass %d: room %u (need %u): %d zip action(s)\n", pass, (unsigned)room, (unsigned)need, zip_actions);
                CHECK_EQ(zip_actions, 3);
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
            CHECK(idx.prepare_open(&fs, &ld, *e, err, sizeof(err)));     // what AppHost::open_now does first: the room, then the needs; nothing is loaded
            CHECK_EQ(ld.count(), 0);
            int rc = ld.load(&src, &h, err, sizeof(err));                // the need is the base
            fs.close(cf);
            if (rc != LIB_OK) printf("load: %d %s\n", rc, err);
            CHECK_EQ(rc, LIB_OK);
            CHECK_EQ(ld.count(), 1);
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
            CHECK_EQ(ld.count(), 0);                                      // a transient application is gone; no library was ever loaded
        }
    }

    // ---- S6 (T197 D8): memory short at the press. A model heap gives the loader window; the same AppIndex functions the board calls (prepare_open) run over the
    // real loader. Base 4.0 loads no library for a need, so the room is the image and APP_WORK_BYTES and nothing is evicted.
    // (Dropped with D58: library eviction, library use counts, a library that declares a poll/menu/corehooks, a library imported by a library.)
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/cores/apps/plain.u2a", slurp(dir + "/plain.u2a"));
        CoreCtx c;
        ctx_for(&c, &fs, &io, "/sd/cores/vc20.cor");
        static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
        int nf = AppIndex::folders(&c, folders, APP_FOLDERS_MAX);
        Log log;
        AppIndex idx;
        idx.scan(&fs, folders, nf, &log);
        CHECK_EQ(idx.count(), 2);
        const AppEntry *uz = idx.find("unzip"), *pl = idx.find("plain");
        CHECK(uz != 0 && pl != 0);
        if (uz && pl) {
            OpRegistry *reg = ops_of(&c);
            ops_register_base(*reg);
            char err[LIB_ERR_MAX];
            uint32_t need = uz->image_bytes + APP_WORK_BYTES;
            {                                                               // a heap one byte short: refused with the figures; exactly enough: opens, nothing loaded
                HeapHost hh(&testcore::desc, need - 1);
                LibLoader ld(&hh, reg);
                CHECK(!idx.fits(*uz, ld.room(), &ld));
                CHECK(!idx.prepare_open(&fs, &ld, *uz, err, sizeof(err)));
                CHECK(strstr(err, "Not enough memory for unzip (needs ") != 0);
                hh.cap = need;
                CHECK(idx.fits(*uz, ld.room(), &ld));
                CHECK(idx.prepare_open(&fs, &ld, *uz, err, sizeof(err)));
                CHECK_EQ(ld.count(), 0);
                printf("S6: unzip needs %u bytes, refused one byte short, opens at the exact room\n", (unsigned)need);
            }
            {                                                               // the message has the numbers
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
            // the refusal: needs (rounded up) is always above free (rounded down), also when the bytes differ by one
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

    // ---- a need no slot answers (T239.3 D58): the app is not available, needs_linked and prepare_open name the need, and the loader refuses the file (LIB_NEEDS)
    // (Dropped with D58: the provider of a need being a library file of the apps folder, found by an export name.)
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
        HeapHost hh(&testcore::desc, 1u << 24);
        LibLoader ld(&hh, reg);
        CHECK(ra && !idx.available(*ra, &ld));
        CHECK(ra && !idx.fits(*ra, 1u << 24, &ld));
        CHECK(ra && !idx.needs_linked(*ra, err, sizeof(err)));
        CHECK_STR(err, "ringapp needs \"ring\" 2.0, which this core's app is not linked with.");
        CHECK(ra && !idx.prepare_open(&fs, &ld, *ra, err, sizeof(err)));
        CHECK(strstr(err, "\"ring\" 2.0") != 0);
        CoreFile *f = ra ? fs.open(ra->path, false) : 0;
        CHECK(f != 0);
        if (f) {
            CoreFileSource src(f);
            int h = -1;
            CHECK_EQ(ld.load(&src, &h, err, sizeof(err)), LIB_NEEDS);
            CHECK(strstr(err, "ring") != 0);
            CHECK_EQ(ld.count(), 0);
            fs.close(f);
        }
        printf("D58: ringapp is not available, its need ring 2.0 is named\n");
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
