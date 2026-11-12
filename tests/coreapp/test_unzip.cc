// T180 D9 (#43): "Unzip here" in every core. One copy of unzip.u2a in "<dir of the .cor>/apps" (APP_SHARED_FOLDER) serves every core; a core's own
// "<core folder>/apps" wins on an equal name. What is real here: AppIndex::folders / scan over the FakeFs, the menu record of the file (packed by
// tools/u2a_pack.py from apps/unzip/app.toml), the merge into the core's file types, the LibLoader that loads the file and its service tables, and the
// code of the application (apps/unzip/src, glue.cc, app/sdk/u2p_compat.c) compiled for the Mac. What is not: that RISC-V instructions run - the image the
// loader places is a fixture whose app_main is not called, LibHost::call_main runs the Mac build of the same sources (tests/loader does the same for libraries).
//
//   test_unzip FIXDIR      (FIXDIR: unzip.u2a, unzip2.u2a, t.zip, manifest.txt and files/ - tests/coreapp/run.sh makes them)
#include <string>
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
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }
    int call_main(const Entry &, const LibApi *api, AppCtx *ctx) override { return app_main(api, ctx); }
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
        CHECK_EQ(idx.count(), 1);
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
            int rc = ld.load(&src, &h, err, sizeof(err));
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
            CHECK(g_msg.find("Extracted " + std::to_string(ok) + " file(s) to this folder") != std::string::npos);
            if (absent) CHECK(g_msg.find("Failed: " + std::to_string(absent)) != std::string::npos);
            CHECK(fs.files.find("/sd/games/t.zip") != fs.files.end());
            CHECK(fs.files.find("/sd/t.zip") == fs.files.end());          // nothing next to the wrong folder
            CHECK_EQ(fs.open_files, 0);                                   // every file the application opened is closed again
            ld.release(h, err, sizeof(err));
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
