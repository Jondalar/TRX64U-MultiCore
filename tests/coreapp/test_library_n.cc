// GitHub #93 (T209 "File Browser, n entries", the core application's side): the F3 Library lists a folder of any length, the copy/move folder pick the same.
// 784 and 3000 files are paged to the end; with a small heap budget (ListGrow::free_heap) the list stops where the budget stops it and says so on a last line
// that takes no focus. No negative control: not a flash or persistence operation (T174 D5.4).
#include <string>
#include "check.h"
#include "sio_model.h"
#include "menuctl.h"
#include "listgrow.h"
#include "cores.h"
#include "keyboard.h"
#include "volume.h"
#include "ops_glue.h"
#include "atari_media.h"

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

struct Rig {
    SioModel io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    explicit Rig(const CoreDesc *d)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/usb0/cores/atari800.cfg", 0);
        c.settings = st;
        vid = new Video(d->video, &px, &io);
        c.video = vid;
        m = new MenuCtl(&c, &host);
        st->load();
    }
    std::string left(int i) { char l[300], r[100]; bool a; m->row(i, l, 300, r, 100, &a); return l; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    bool go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return false; } for (int n = 0; n < 4000 && m->selected() > r; n++) m->key(KEY_UP); for (int n = 0; n < 4000 && m->selected() < r; n++) m->key(KEY_DOWN); return true; }
    void enter(const char *s) { if (go_to(s)) m->key(KEY_RETURN); }
    void to_folder(const char *a, const char *b)
    {
        m->key(KEY_F3);
        for (int i = 0; i < 12 && strcmp(m->title(), "/"); i++) m->key(KEY_LEFT);
        enter(a);
        if (b) enter(b);
    }
};

static void fill(FakeFs &fs, const char *dir, int n, bool folders)
{
    for (int i = 0; i < n; i++) {
        char nm[300];
        snprintf(nm, sizeof(nm), "%s/Game %04d (USA).sfc.zip", dir, i);
        if (folders) fs.mkdir(nm); else fs.put(nm, std::vector<uint8_t>(10, 1));
    }
}

// the Library at /usb0/big with n entries: paged to the end with PAGEDOWN, walked with DOWN, back to the top with PAGEUP; `heap` 0: no budget
static void library(const CoreDesc *d, int n, uint32_t heap)
{
    ListGrow::free_heap() = 0;
    Rig r(d);
    fill(r.fs, "/usb0/big", n, false);
    static uint32_t h;
    h = heap;
    if (heap) ListGrow::free_heap() = []() -> uint32_t { return h; };
    r.m->open();
    r.m->set_page_rows(12);
    r.to_folder("usb0/", "big/");
    CHECK_STR(r.m->title(), "/usb0/big");
    int rows = r.m->rows();
    bool cut = heap != 0;
    int shown = rows - 1 - (cut ? 1 : 0);                           // without ".." and the information line
    printf("library n=%d heap=%u: rows %d (entries %d%s)\n", n, (unsigned)heap, rows, shown, cut ? " + information line" : "");
    if (!cut) CHECK_EQ(rows, n + 1);
    else CHECK(shown < n && shown >= ListGrow::START - 1);
    for (int i = 0; i < rows + 3 && r.m->selected() < rows - 1; i++) r.m->key(KEY_PAGEDOWN);
    int last = shown;                                               // ".." is row 0, the last entry row `shown`
    CHECK_EQ(r.m->selected(), last);
    if (!cut) CHECK_STR(r.left(r.m->selected()).c_str(), (std::string("Game ") + (char)('0' + (n - 1) / 1000 % 10) + (char)('0' + (n - 1) / 100 % 10) + (char)('0' + (n - 1) / 10 % 10) + (char)('0' + (n - 1) % 10) + " (USA).sfc.zip").c_str());
    r.m->key(KEY_DOWN);
    CHECK_EQ(r.m->selected(), last);                                // the information line takes no focus
    if (cut) {
        CHECK_STR(r.left(rows - 1).c_str(), "... more not shown");
        CHECK(r.left(last).find("Game ") == 0);
    }
    printf("  at the end: row %d '%s'%s\n", r.m->selected(), r.left(r.m->selected()).c_str(), cut ? ", line: '... more not shown'" : "");
    for (int i = 0; i < rows + 3 && r.m->selected() > 0; i++) r.m->key(KEY_PAGEUP);
    CHECK_EQ(r.m->selected(), 0);
    CHECK_STR(r.left(0).c_str(), "../");
}

// the copy to ... folder pick at /usb0/big with n sub-folders: the pick lists them all, or says that it does not
static void picker(const CoreDesc *d, int n, uint32_t heap)
{
    ListGrow::free_heap() = 0;
    Rig r(d);
    r.fs.put("/usb0/a.atr", std::vector<uint8_t>(300, 1));
    fill(r.fs, "/usb0/big", n, true);
    static uint32_t h;
    h = heap;
    if (heap) ListGrow::free_heap() = []() -> uint32_t { return h; };
    r.m->open();
    r.to_folder("usb0/", 0);
    r.go_to("a.atr");
    r.m->key(KEY_RETURN);
    r.enter("Copy to...");
    CHECK(r.m->picking());
    for (int i = 0; i < 12 && r.left(r.m->selected()) != "big/"; i++) r.m->key(KEY_DOWN);
    CHECK_STR(r.left(r.m->selected()).c_str(), "big/");
    r.m->key(KEY_RETURN);                                           // the pick shows /usb0/big
    int rows = r.m->rows();                                         // row 0 takes the folder the pick shows, then ".." and the folders
    bool cut = heap != 0;
    printf("pick n=%d heap=%u: rows %d, status '%s'\n", n, (unsigned)heap, rows, r.m->status());
    if (!cut) { CHECK_EQ(rows, n + 2); CHECK(!strstr(r.m->status(), "not shown")); }
    else { CHECK(rows < n + 2 && rows > ListGrow::START); CHECK(strstr(r.m->status(), "more entries are not shown") != 0); }
    for (int i = 0; i < rows + 3 && r.m->selected() < rows - 1; i++) r.m->key(KEY_PAGEDOWN);
    CHECK_EQ(r.m->selected(), rows - 1);
    printf("  pick at the end: row %d of %d '%s'\n", r.m->selected(), rows, r.left(r.m->selected()).c_str());
    CHECK(r.left(r.m->selected()).find("game ") == 0);                 // (the fake medium keeps folder names lower case)
}

int main()
{
    static CoreDesc gd;
    static Setting gs[SETTINGS_MAX];
    CHECK(!volume_extend(&atari800_desc, &gd, gs));
    library(&gd, 784, 0);
    library(&gd, 3000, 0);
    library(&gd, 3000, 600000);                                     // 512 -> 1024 entries fit (139264 <= 150000), 1024 -> 2048 does not (278528)
    picker(&gd, 784, 0);
    picker(&gd, 3000, 0);
    picker(&gd, 3000, 600000);
    DONE("library_n");
}
