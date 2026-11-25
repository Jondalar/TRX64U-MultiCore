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
extern const CoreDesc atari800_desc;          // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

struct Rig {
    SioModel io; FakeFs fs; PxRec px; Host host; CoreCtx c; Settings *st; Video *vid; MenuCtl *m;
    int first = 0;                                                   // what the screen does with the window (trx_window, margin 2) and tells the menu (note_scroll)
    uint32_t seen = 0;
    void screen(int rows_shown)
    {
        if (m->scroll_rev() != seen) { first = m->scroll_first(); seen = m->scroll_rev(); }
        int n = m->rows(), sel = m->selected(), mg = (rows_shown - 1) / 2 < 2 ? (rows_shown - 1) / 2 : 2;
        if (first > sel - mg) first = sel - mg;
        if (sel > first + rows_shown - 1 - mg) first = sel - rows_shown + 1 + mg;
        if (first > n - rows_shown) first = n - rows_shown;
        if (first < 0) first = 0;
        m->note_scroll(first);
    }
    void key(int k) { m->key(k); screen(12); }
    bool run_job() { int n = 0; while (m->tick() && ++n < 200000) { } return n < 200000; }
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
    r.screen(12);
    for (int i = 0; i < rows + 3 && r.m->selected() < rows - 1; i++) r.key(KEY_PAGEDOWN);
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
    char pos[40];
    r.m->pos_text(pos, sizeof(pos), r.first, 12);
    printf("  header at the end: '%s'\n", pos);
    char want[40];
    snprintf(want, sizeof(want), cut ? "%d-%d of %d+" : "%d-%d of %d", r.first, shown, shown);     // the window's rows are numbered from ".." = 0: its first entry is `first`
    CHECK_STR(pos, want);                                           // (".." and the line are not counted)
    // ux-review R9: `-` from the last page moves the window by a page and lands as the host does (margin 2 below the first row of a window that is not at the top)
    int fl = r.first;
    r.key(KEY_PAGEUP);
    printf("  page back: window %d -> %d, row %d (%d in the window)\n", fl, r.first, r.m->selected(), r.m->selected() - r.first);
    CHECK_EQ(r.first, fl - 12);
    CHECK_EQ(r.m->selected() - r.first, 9);                          // rows - 1 - margin: the row it stood on (11) is held inside the margin
    r.m->pos_text(pos, sizeof(pos), r.first, 12);
    snprintf(want, sizeof(want), cut ? "%d-%d of %d+" : "%d-%d of %d", r.first, r.first + 11, shown);
    CHECK_STR(pos, want);
    for (int i = 0; i < rows + 3 && r.m->selected() > 0; i++) r.key(KEY_PAGEUP);
    CHECK_EQ(r.m->selected(), 0);
    CHECK_STR(r.left(0).c_str(), "../");
}

// GitHub #93: "mark all" on n files, copy to a folder: every marked file arrives, or the end message says how many were not taken (never silent)
static void copy_all(const CoreDesc *d, int n, uint32_t heap)
{
    ListGrow::free_heap() = 0;
    Rig r(d);
    fill(r.fs, "/usb0/big", n, false);
    r.fs.mkdir("/usb0/dest");
    static uint32_t h;
    h = heap;
    if (heap) ListGrow::free_heap() = []() -> uint32_t { return h; };
    r.m->open();
    r.m->set_page_rows(12);
    r.to_folder("usb0/", "big/");
    r.go_to("../");
    r.m->key(KEY_RIGHT);
    r.enter("Mark all");
    printf("copy-all n=%d heap=%u: marked %d, cut %d\n", n, (unsigned)heap, r.c.lib.nmarked, r.c.lib.marked_cut);
    if (!heap) { CHECK_EQ(r.c.lib.nmarked, n); CHECK_EQ(r.c.lib.marked_cut, 0); }
    r.go_to("Game 0000 (USA).sfc.zip");
    r.m->key(KEY_RETURN);
    r.enter("Copy to...");
    CHECK(r.m->picking());
    r.m->key(KEY_LEFT);                                              // the pick shows /usb0
    for (int i = 0; i < 12 && r.left(r.m->selected()) != "dest/"; i++) r.m->key(KEY_DOWN);
    CHECK_STR(r.left(r.m->selected()).c_str(), "dest/");
    r.m->key(KEY_RETURN);
    while (r.m->selected() > 0) r.m->key(KEY_UP);
    r.m->key(KEY_RETURN);
    CHECK(r.run_job());
    int got = 0;
    for (int i = 0; i < n; i++) {
        char p[300];
        snprintf(p, sizeof(p), "/usb0/dest/Game %04d (USA).sfc.zip", i);
        uint64_t sz; bool dd;
        got += r.fs.stat(p, &sz, &dd) ? 1 : 0;
    }
    const char *st = r.m->status();
    printf("  arrived %d of %d, message '%s'\n", got, n, st);
    if (!heap) { CHECK_EQ(got, n); CHECK(!strstr(st, "not taken")); }
    else {
        int not_taken = -1;
        const char *q = strstr(st, ", ");
        while (q && sscanf(q, ", %d more not taken", &not_taken) != 1) q = strstr(q + 1, ", ");
        CHECK(not_taken > 0);                                        // the budget cut it and the message says how many
        CHECK_EQ(got + not_taken, n);
    }
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
    copy_all(&gd, 784, 0);
    copy_all(&gd, 784, 600000);
    picker(&gd, 784, 0);
    picker(&gd, 3000, 0);
    picker(&gd, 3000, 600000);
    DONE("library_n");
}
