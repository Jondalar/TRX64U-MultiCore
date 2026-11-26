// T197 P3 / S3: Unzip in the host's File Browser, through the same functions the board runs (host_core_flow_zip.cc: hc_zip_job over the loader of coreapp/src). What is real: the job and
// its pages, AppIndex over the folders of the card, the LibLoader that loads unzip.u2a (a fixture image made by the real packer, tests/coreapp/gen_unzip.py) and binds its need miniz>=1.0 to the
// slot of hostlibslot.cc, the code of apps/unzip and apps/miniz (miniz is linked into the host app, no library file is loaded; the Mac build of the same sources runs where the board jumps into the image). What is not: RISC-V instructions, the FileManager (a map of byte vectors).
//
//   test_hostzip FIXDIR      (FIXDIR: unzip.u2a, t.zip, manifest.txt and files/ - run_hostzip.sh makes them)
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <fstream>
#include <sstream>
#include "check.h"
#include "fakes.h"
#include "host_core_flow_zip.h"
#include "host_core_job.h"
#include "keyboard.h"

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
static std::string lowered(std::string s) { for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]); return s; }

// the loader's host: runs the Mac build of the application where the board would call the image's app_main
struct Host : LibHost {
    std::vector<void *> live;
    uint32_t room_ = 0xFFFFFFFFu;
    ~Host() { for (size_t i = 0; i < live.size(); i++) free(live[i]); }
    void *alloc(uint32_t n) override { void *p = malloc(n); live.push_back(p); return p; }
    void release(void *p) override
    {
        for (size_t i = 0; i < live.size(); i++) { if (live[i] == p) { live.erase(live.begin() + i); free(p); return; } }
    }
    uint32_t addr_of(const void *p) override { return (uint32_t)(uintptr_t)p; }
    void sync_code(void *, uint32_t) override { }
    bool has_capability(const char *) override { return false; }
    const char *core_name() override { return "host"; }
    int call_register(const Entry &, const LibApi *, LibRecord *) override { return 0; }
    int call_main(const Entry &, const LibApi *api, AppCtx *ctx) override { return app_main(api, ctx); }
    uint32_t room() override { return room_; }
};

// the screen of the rig: every page the flow draws, the keys it is given
struct Rig {
    std::deque<int> keys;
    std::vector<std::string> pages;          // title + "|" + lines
    std::string note;
    int idles = 0;
    int bars = 0;                            // pages with a progress bar
};
static void r_draw(void *c, const HcPage &pg, bool)
{
    Rig *r = (Rig *)c;
    std::string s = pg.title;
    for (int i = 0; i < HC_PAGE_LINES; i++) { if (pg.line[i][0]) { s += "|"; s += pg.line[i]; } }
    r->pages.push_back(s);
    if (pg.bar >= 0) r->bars++;
}
static int r_getch(void *c)
{
    Rig *r = (Rig *)c;
    if (r->keys.empty()) return -1;
    int k = r->keys.front();
    r->keys.pop_front();
    return k;
}
static void r_idle(void *c)
{
    Rig *r = (Rig *)c;
    if (++r->idles > 200000) { printf("FAIL the flow waits for a key the script does not have\n"); exit(1); }
}
static void r_sleep(void *, int) { }
static void r_note(void *c, const char *t, int) { ((Rig *)c)->note = t; }
static void r_log(void *, const char *l) { printf("  log: %s\n", l); }

static HcFlowEnv make_env(Rig *r)
{
    HcFlowEnv e;
    memset(&e, 0, sizeof(e));
    e.draw = r_draw;
    e.getch_nowait = r_getch;
    e.getch = r_getch;
    e.idle = r_idle;
    e.sleep_ms = r_sleep;
    e.note = r_note;
    e.log = r_log;
    e.ctx = r;
    return e;
}

// runs the job to its end, stepping it as the screen's loop does
static bool run_job(HcZipEnv &z, const char *path)
{
    HcJob *j = hc_zip_job(z, path);
    HcJobIn in;
    in.key = -1;
    int steps = 0;
    while (!j->step(in)) {
        if (++steps > 100000) { printf("FAIL the job did not end\n"); exit(1); }
    }
    bool ok = j->ok;
    delete j;
    return ok;
}

static bool any_page(const Rig &r, const char *needle)
{
    for (size_t i = 0; i < r.pages.size(); i++) { if (r.pages[i].find(needle) != std::string::npos) return true; }
    return false;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    std::string dir = argv[1];
    Bytes u1 = slurp(dir + "/unzip.u2a"), zip = slurp(dir + "/t.zip");
    CHECK(u1.size() > 100 && zip.size() > 100);
    std::vector<std::string> names;
    {
        std::ifstream m((dir + "/manifest.txt").c_str());
        std::string l;
        while (std::getline(m, l)) { if (!l.empty()) names.push_back(l); }
    }
    CHECK(names.size() >= 5);

    // ---- S3 a: "into a folder": the zip on the card, the applications in /SD/cores/apps, F then the application's own "Extracted" page
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/games/t.zip", zip);
        Host host;
        Rig r;
        HcFlowEnv e = make_env(&r);
        HcZipEnv z = { &e, &fs, &host, 0 };
        r.keys.push_back('f');                                           // the question: into a folder
        r.keys.push_back(KEY_RETURN);                                    // the application's page "Extracted N files into ..."
        int runs0 = hc_zip_runs();
        bool ok = run_job(z, "/SD/games/t.zip");
        CHECK(ok);
        CHECK_EQ(hc_zip_runs(), runs0 + 1);
        CHECK(any_page(r, "F   unpack into a folder") && any_page(r, "H   unpack here") && any_page(r, "O   extract one file"));   // the three actions
        CHECK(any_page(r, "Extracted "));
        CHECK(r.bars > 0);                                               // the bar the application drew
        int ok_files = 0;
        for (size_t i = 0; i < names.size(); i++) {
            Bytes want = slurp(dir + "/files/" + names[i]);
            std::string path = "/sd/games/t/" + names[i];
            if (path.size() >= 256) { CHECK(fs.files.find(lowered(path)) == fs.files.end()); continue; }
            std::map<std::string, Bytes>::iterator f = fs.files.find(lowered(path));
            CHECK(f != fs.files.end());
            if (f != fs.files.end()) { CHECK(f->second == want); ok_files++; }
        }
        CHECK(ok_files >= 4);
        CHECK(fs.files.find("/sd/games/t.zip") != fs.files.end());     // the zip stays
        CHECK_EQ(fs.open_files, 0);
        CHECK(r.note.empty());
        hc_zip_rig_free(z);
    }

    // ---- S3 b: "here", twice in the same run of the rig (nothing stays loaded between the two)
    {
        FakeFs fs;
        fs.put("/usb0/cores/apps/unzip.u2a", u1);                         // the zip's own medium holds the applications
        fs.put("/usb0/z/t.zip", zip);
        Host host;
        Rig r;
        HcFlowEnv e = make_env(&r);
        HcZipEnv z = { &e, &fs, &host, 0 };
        r.keys.push_back('H');
        r.keys.push_back(KEY_RETURN);
        CHECK(run_job(z, "/USB0/z/t.zip"));
        CHECK(fs.files.find(lowered("/usb0/z/" + names[0])) != fs.files.end());
        CHECK(fs.files.find("/usb0/z/t/readme.txt") == fs.files.end());
        // again
        size_t live = host.live.size();
        r.keys.push_back('h');
        r.keys.push_back(KEY_RETURN);
        CHECK(run_job(z, "/USB0/z/t.zip"));
        CHECK_EQ(host.live.size(), live);                                 // the application came and went, nothing stays in the heap
        hc_zip_rig_free(z);
    }

    // ---- S3 c: "one file": O, the list of the archive (the application's choose), RETURN on the first, the page
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/games/t.zip", zip);
        Host host;
        Rig r;
        HcFlowEnv e = make_env(&r);
        HcZipEnv z = { &e, &fs, &host, 0 };
        r.keys.push_back('o');
        r.keys.push_back(KEY_DOWN);
        r.keys.push_back(KEY_RETURN);                                     // the second entry of the list
        r.keys.push_back(KEY_RETURN);                                     // the page "Extracted 1 file into ..."
        CHECK(run_job(z, "/SD/games/t.zip"));
        CHECK(any_page(r, "> "));                                         // the list with its mark
        CHECK(any_page(r, "Extracted 1 file into /SD/games"));
        int n = 0;
        for (size_t i = 0; i < names.size(); i++) { if (fs.files.find(lowered("/sd/games/" + names[i])) != fs.files.end()) n++; }
        CHECK_EQ(n, 1);
        // ESC in the list: nothing is extracted
        FakeFs fs2;
        fs2.put("/sd/cores/apps/unzip.u2a", u1);
        fs2.put("/sd/games/t.zip", zip);
        HcZipEnv z2 = { &e, &fs2, &host, 0 };
        size_t files0 = fs2.files.size();
        r.keys.push_back('o');
        r.keys.push_back(KEY_ESCAPE);
        run_job(z2, "/SD/games/t.zip");
        CHECK_EQ(fs2.files.size(), files0);
        hc_zip_rig_free(z2);
        hc_zip_rig_free(z);
    }

    // ---- S3 d: a key that is none of the three: nothing runs, nothing is written
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/games/t.zip", zip);
        Host host;
        Rig r;
        HcFlowEnv e = make_env(&r);
        HcZipEnv z = { &e, &fs, &host, 0 };
        r.keys.push_back(KEY_ESCAPE);
        int runs0 = hc_zip_runs();
        CHECK(!run_job(z, "/SD/games/t.zip"));
        CHECK_EQ(hc_zip_runs(), runs0);
        CHECK_EQ(fs.files.size(), 3u);
        CHECK(r.note.find("Nothing was changed") != std::string::npos);
        hc_zip_rig_free(z);
    }

    // ---- S3 e: the refusals are pages that say why, before anything is written: no application, a heap too small
    {
        for (int pass = 0; pass < 2; pass++) {
            FakeFs fs;
            if (pass != 0) fs.put("/sd/cores/apps/unzip.u2a", u1);
            fs.put("/sd/games/t.zip", zip);
            Host host;
            if (pass == 1) host.room_ = 1000;
            Rig r;
            HcFlowEnv e = make_env(&r);
            HcZipEnv z = { &e, &fs, &host, 0 };
            r.keys.push_back(KEY_RETURN);                                 // the key that leaves the refusal page
            size_t before = fs.files.size();
            CHECK(!run_job(z, "/SD/games/t.zip"));
            CHECK_EQ(fs.files.size(), before);
            CHECK(r.pages.size() >= 1 && r.pages[0].find("Cannot unzip t.zip") == 0);
            printf("refusal %d: %s\n", pass, r.pages.empty() ? "" : r.pages[0].c_str());
            if (pass == 0) CHECK(any_page(r, "Unzip is not installed.") && any_page(r, "Copy unzip.u2a from the release"));
            else CHECK(any_page(r, "not enough memory") && any_page(r, "It needs more of the heap than is free now."));
            hc_zip_rig_free(z);
        }
    }
    // ---- S3 f: a file that is no zip: the application says so on its page, the flow ends red and writes nothing
    {
        FakeFs fs;
        fs.put("/sd/cores/apps/unzip.u2a", u1);
        fs.put("/sd/games/bad.zip", Bytes(500, 0x41));
        Host host;
        Rig r;
        HcFlowEnv e = make_env(&r);
        HcZipEnv z = { &e, &fs, &host, 0 };
        r.keys.push_back('h');
        r.keys.push_back(KEY_RETURN);                                     // "Cannot read this file as a ZIP."
        r.keys.push_back(KEY_RETURN);                                     // the page that says it did not finish
        CHECK(!run_job(z, "/SD/games/bad.zip"));
        CHECK(any_page(r, "Cannot read this file as a ZIP") && any_page(r, "Unzip did not finish"));
        CHECK_EQ(fs.files.size(), 3u);
        hc_zip_rig_free(z);
    }
    DONE("test_hostzip");
}
