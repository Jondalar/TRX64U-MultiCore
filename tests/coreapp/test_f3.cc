// T180 D3, D6, D7 (S4, S5, S6): the core app's F3 Library in a core that is not the C64 (the Atari descriptor).
//  D3  no [Recent] and no [Favorites] row at the media level, no "Add to Favorites" in a sheet, running a file leaves no "Recent n" in the .cfg.
//  D6  the file operations (SPACE marks, Mark all / none, Copy to, Move to, clipboard, Paste here, Rename, Delete, New folder) work here: the common
//      part of the application registers them, the descriptor carries none (GitHub #20: Neo Geo answered "no operation files.rename").
//  D7  in the F3 browser the bare "+" / "-" page the list (a page is the rows the list shows, clamped at both ends); the volume there is SHIFT only
//      ('{' / '}' on the C64 keyboard, '_' on a USB keyboard); elsewhere in the menu the T160 keys are unchanged.
#include <string>
#include "check.h"
#include "sio_model.h"
#include "menuctl.h"
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
    std::string left(int i) { char l[300], r[100]; bool a; m->row(i, l, 300, r, 100, &a); return l; }      // 300: a FAT long name (255) and its slash
    std::string value(int i) { char l[100], r[100]; bool a; m->row(i, l, 100, r, 100, &a); return r; }
    int find_row(const char *s) { for (int i = 0; i < m->rows(); i++) if (left(i) == s) return i; return -1; }
    bool go_to(const char *s) { int r = find_row(s); if (r < 0) { printf("FAIL no row %s\n", s); g_fails++; return false; } for (int n = 0; n < 700 && m->selected() > r; n++) m->key(KEY_UP); for (int n = 0; n < 700 && m->selected() < r; n++) m->key(KEY_DOWN); return true; }
    void enter(const char *s) { if (go_to(s)) m->key(KEY_RETURN); }
    void run_job() { int n = 0; while (m->tick() && ++n < 400) { } }
    void type(const char *t) { for (; *t; t++) m->key((unsigned char)*t); }
    bool has(const char *p) { uint64_t sz; bool d; return fs.stat(p, &sz, &d) && !d; }
    bool has_dir(const char *p) { uint64_t sz; bool d; return fs.stat(p, &sz, &d) && d; }
    void to_folder(const char *a, const char *b)                   // the Library at /usb0/games
    {
        m->key(KEY_F3);
        for (int i = 0; i < 12 && strcmp(m->title(), "/"); i++) m->key(KEY_LEFT);
        enter(a);
        if (b) enter(b);
    }
};

static std::string lower_name(std::string s) { for (auto &ch : s) ch = (char)tolower((unsigned char)ch); return s; }     // FakeFs keys are lower case
static std::vector<uint8_t> bytes(size_t n, uint8_t v) { return std::vector<uint8_t>(n, v); }

int main()
{
    static CoreDesc gd;                                            // what the application runs: the descriptor and the host's volume item (T164)
    static Setting gs[SETTINGS_MAX];
    CHECK(!volume_extend(&atari800_desc, &gd, gs));
    Rig r(&gd);
    r.fs.put("/usb0/games/a.atr", bytes(300, 1));
    r.fs.put("/usb0/games/b.ATR", bytes(300, 2));
    r.fs.put("/usb0/games/notes.txt", bytes(10, 3));
    r.fs.put("/usb0/games/p.xex", bytes(10, 4));
    r.fs.put("/usb0/games/z.bin", bytes(10, 5));
    r.m->open();

    // ---- D3: the media level has the media and nothing else; no Favorites in any sheet
    r.m->key(KEY_F3);
    CHECK_STR(r.m->title(), "/");
    CHECK(r.find_row("usb0/") >= 0);
    CHECK(r.find_row("[Recent]/") < 0 && r.find_row("[Favorites]/") < 0);
    CHECK(r.find_row("[Recent]") < 0 && r.find_row("[Favorites]") < 0);
    for (int i = 0; i < r.m->rows(); i++) CHECK(r.left(i)[0] != '[');       // no list of the Library at all until a home folder is set
    r.enter("usb0/"); r.enter("games/");
    CHECK_STR(r.m->title(), "/usb0/games");
    r.go_to("a.atr");
    r.m->key(KEY_RETURN);                                          // the sheet of a file
    CHECK(r.m->chooser());
    CHECK(r.find_row("Add to Favorites") < 0 && r.find_row("Remove from Favorites") < 0);
    CHECK(r.find_row("Rename") >= 0 && r.find_row("Delete") >= 0 && r.find_row("Copy to...") >= 0 && r.find_row("Move to...") >= 0);
    r.m->key(KEY_ESCAPE);
    {   // running or mounting a file keeps no list of "Recent n" in the .cfg
        r.go_to("a.atr");
        r.m->key(KEY_RETURN);
        r.m->key(KEY_RETURN);                                      // the first action of the file's type
        CHECK(r.fs.text("/usb0/cores/atari800.cfg").find("Recent") == std::string::npos);
        CHECK(r.fs.text("/usb0/cores/atari800.cfg").find("Favorite") == std::string::npos);
        r.m->key(KEY_ESCAPE);
    }

    // ---- D6: the file operations in a core that carries none of its own
    OpResult tmp;
    CHECK(ops_call(&r.c, "files.rename", 0, 0, &tmp) != OPE_NOT_FOUND);       // the operation exists in this core (the registry knows the name)
    CHECK(ops_call(&r.c, "files.mkdir", 0, 0, &tmp) != OPE_NOT_FOUND);
    CHECK(ops_call(&r.c, "files.copy_job", 0, 0, &tmp) != OPE_NOT_FOUND);

    // Rename: the sheet's Rename, the name typed over the old one
    r.to_folder("usb0/", "games/");
    r.go_to("notes.txt");
    r.m->key(KEY_RETURN);
    r.enter("Rename");
    CHECK(r.m->entering());
    for (int i = 0; i < 20; i++) r.m->key(KEY_BACK);
    r.type("memo.txt");
    r.m->key(KEY_RETURN);
    CHECK(r.has("/usb0/games/memo.txt") && !r.has("/usb0/games/notes.txt"));
    CHECK(r.find_row("memo.txt") >= 0 && r.find_row("notes.txt") < 0);
    CHECK(strstr(r.m->status(), "no operation") == 0 && !r.m->status_bad());

    // #38: a name of 80 characters is typed (the old one deleted), 3 deleted again, ".adf" appended; the row scrolls, the tail and the cursor show
    {
        std::string base;
        for (int i = 0; i < 80; i++) base += (char)('q' + i % 10);                    // sorts after the files the later steps use
        r.go_to("memo.txt");
        r.m->key(KEY_RETURN);
        r.enter("Rename");
        CHECK(r.m->entering());
        for (int i = 0; i < 20; i++) r.m->key(KEY_BACK);
        r.type((base + "xyz").c_str());
        for (int i = 0; i < 3; i++) r.m->key(KEY_BACK);
        r.type(".adf");
        std::string row;
        for (int i = 0; i < r.m->rows(); i++) row += r.value(i) + "|";
        CHECK(row.find(base + ".adf") == std::string::npos);                       // the whole name does not fit the row ...
        CHECK(row.find("..") != std::string::npos);                                // ... its tail does, behind ".."
        CHECK(row.find(base.substr(base.size() - 40) + ".adf_") != std::string::npos);   // with the cursor
        r.m->key(KEY_RETURN);
        CHECK(r.has(("/usb0/games/" + base + ".adf").c_str()) && !r.has("/usb0/games/memo.txt"));
        CHECK(!r.m->status_bad());
        // the longest FAT name, 255 characters, is taken whole; the 256th is not typed
        std::string big;
        for (int i = 0; i < 255; i++) big += (char)('A' + i % 26);
        big[0] = 'z';                                                              // sorts last
        r.go_to("z.bin");
        r.m->key(KEY_RETURN);
        r.enter("Rename");
        for (int i = 0; i < 70; i++) r.m->key(KEY_BACK);
        r.type((big + "Q").c_str());
        r.m->key(KEY_RETURN);
        CHECK(r.has(("/usb0/games/" + big).c_str()) && !r.has("/usb0/games/z.bin"));

        // T180 D8: a cursor in the entry. CRSR left / right and HOME / END move it, typed characters go in at it, DEL takes the one before it; CRSR left
        // is not "cancel" (<- / ESC is). The name is the 80-character one from above: the row scrolls with the cursor, and shows it where it is.
        r.go_to((base + ".adf").c_str());
        r.m->key(KEY_RETURN);
        r.enter("Rename");
        CHECK(r.m->entering());
        r.m->key(KEY_HOME);
        r.type("A");                                                               // at the start
        r.m->key(KEY_RIGHT); r.m->key(KEY_RIGHT);
        r.type("B");                                                               // two further, in the middle
        auto draw = [&r]() { for (int i = 0; i < r.m->rows(); i++) r.value(i); };    // the window follows the cursor as the screen is drawn, once per key
        r.m->key(KEY_END);
        draw();
        for (int i = 0; i < 4; i++) { r.m->key(KEY_LEFT); draw(); }              // before ".adf"
        CHECK(r.m->entering());                                                    // CRSR left did not cancel
        std::string row2;
        for (int i = 0; i < r.m->rows(); i++) row2 += r.value(i) + "|";
        CHECK(row2.find("_.adf") != std::string::npos);                            // the cursor is drawn where it stands, not at the end
        r.m->key(KEY_BACK);                                                        // the last character of the 80
        r.m->key(KEY_RETURN);
        const std::string moved = "A" + base.substr(0, 2) + "B" + base.substr(2, 77) + ".adf";
        CHECK(r.has(("/usb0/games/" + moved).c_str()) && !r.has(("/usb0/games/" + base + ".adf").c_str()));
        CHECK(!r.m->status_bad());
        r.go_to(moved.c_str());                                                    // and back to the name the later steps use
        r.m->key(KEY_RETURN);
        r.enter("Rename");
        for (int i = 0; i < 100; i++) r.m->key(KEY_BACK);
        r.type((base + ".adf").c_str());
        r.m->key(KEY_RETURN);
        CHECK(r.has(("/usb0/games/" + base + ".adf").c_str()) && !r.has(("/usb0/games/" + moved).c_str()));
    }

    // New folder: the folder sheet's "Create folder here"
    r.fs.mkdir("/usb0/games/sub");
    r.m->key(KEY_F3);
    r.go_to("../");
    r.m->key(KEY_RIGHT);                                           // the sheet of the folder the Library is in (RIGHT on a folder row enters it, GitHub #90)
    CHECK(r.m->chooser());
    r.enter("Create folder here");
    CHECK(r.m->entering());
    r.type("dest1");
    r.m->key(KEY_RETURN);
    CHECK(r.has_dir("/usb0/games/dest1"));
    CHECK(r.find_row("dest1/") >= 0);

    // SPACE marks and goes on; Mark all / Mark none
    r.go_to("a.atr");
    r.m->key(KEY_SPACE);
    r.m->key(KEY_SPACE);                                           // a.atr and b.ATR
    CHECK_EQ(r.c.lib.nmarked, 2);
    r.go_to("../");
    r.m->key(KEY_RIGHT);
    r.enter("Mark none");
    CHECK_EQ(r.c.lib.nmarked, 0);
    r.go_to("../");
    r.m->key(KEY_RIGHT);
    r.enter("Mark all");
    CHECK(r.c.lib.nmarked >= 4);                                   // every file and folder here (never "..")
    r.go_to("../");
    r.m->key(KEY_RIGHT);
    r.enter("Mark none");
    CHECK_EQ(r.c.lib.nmarked, 0);

    // Copy to: both marked files, into dest1 (the picker's row 0 takes the folder it shows, DOWN DOWN ... enters a folder)
    r.go_to("a.atr");
    r.m->key(KEY_SPACE); r.m->key(KEY_SPACE);
    r.go_to("a.atr");
    r.m->key(KEY_RETURN);
    r.enter("Copy to...");
    CHECK(r.m->picking());
    for (int i = 0; i < 6 && r.left(r.m->selected()) != "dest1/"; i++) r.m->key(KEY_DOWN);
    CHECK_STR(r.left(r.m->selected()).c_str(), "dest1/");
    r.m->key(KEY_RETURN);                                          // into dest1
    while (r.m->selected() > 0) r.m->key(KEY_UP);                  // row 0 takes the folder the picker shows
    r.m->key(KEY_RETURN);
    r.run_job();
    CHECK(r.has("/usb0/games/dest1/a.atr") && r.has("/usb0/games/dest1/b.ATR"));
    CHECK(r.has("/usb0/games/a.atr") && r.has("/usb0/games/b.ATR"));      // a copy leaves the originals
    CHECK_EQ(r.fs.files["/usb0/games/dest1/a.atr"].size(), 300u);

    // Move to: the two marked files of the folder dest1 go to dest2
    r.fs.mkdir("/usb0/games/dest2");
    r.m->key(KEY_F3);
    r.enter("dest1/");
    r.go_to("a.atr");
    r.m->key(KEY_SPACE); r.m->key(KEY_SPACE);
    r.go_to("a.atr");
    r.m->key(KEY_RETURN);
    r.enter("Move to...");
    CHECK(r.m->picking());
    r.m->key(KEY_LEFT);                                            // the picker shows /usb0/games
    for (int i = 0; i < 8 && r.left(r.m->selected()) != "dest2/"; i++) r.m->key(KEY_DOWN);
    CHECK_STR(r.left(r.m->selected()).c_str(), "dest2/");
    r.m->key(KEY_RETURN);                                          // into dest2
    while (r.m->selected() > 0) r.m->key(KEY_UP);
    r.m->key(KEY_RETURN);
    r.run_job();
    CHECK(r.has("/usb0/games/dest2/a.atr") && r.has("/usb0/games/dest2/b.ATR"));
    CHECK(!r.has("/usb0/games/dest1/a.atr") && !r.has("/usb0/games/dest1/b.ATR"));

    // clipboard and Paste here: copy to the clipboard, open another folder, paste there
    r.m->key(KEY_F3);
    for (int i = 0; i < 3 && strcmp(r.m->title(), "/usb0/games"); i++) r.m->key(KEY_LEFT);
    r.m->key(KEY_F3);
    r.go_to("p.xex");
    r.m->key(KEY_RETURN);
    r.enter("Copy to clipboard");
    CHECK_EQ(r.c.lib.nclip, 1);
    r.fs.mkdir("/usb0/games/dest1/inner");
    r.enter("dest1/");
    r.go_to("../");
    r.m->key(KEY_RIGHT);                                           // the ".." sheet: Paste here puts the clipboard into the folder the Library is in
    r.enter("Paste here");
    r.run_job();
    CHECK(r.has("/usb0/games/dest1/p.xex") && r.has("/usb0/games/p.xex"));

    // ---- GitHub #42 (T180 D8 P4): a folder without a subfolder, a renamed clipboard source, a name that is taken
    {
        Rig q(&gd);
        q.fs.put("/usb0/flat/x.bin", bytes(10, 1));
        q.fs.put("/usb0/flat/arc.tar.gz", bytes(20, 2));
        q.fs.put("/usb0/flat/README", bytes(5, 3));
        q.fs.mkdir("/usb0/other");
        q.m->open();
        // 1: New folder where no folder row exists: the sheet of ".." (RIGHT) has it
        q.to_folder("usb0/", "flat/");
        CHECK(q.find_row("../") >= 0 && q.find_row("sub/") < 0);
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        CHECK(q.m->chooser());
        CHECK(q.find_row("Create folder here") >= 0 && q.find_row("Paste here") < 0);      // nothing on the clipboard yet
        q.enter("Create folder here");
        CHECK(q.m->entering());
        q.type("fresh");
        q.m->key(KEY_RETURN);
        CHECK(q.has_dir("/usb0/flat/fresh") && !q.m->status_bad());
        CHECK(q.find_row("fresh/") >= 0);
        // 3: Paste here in the same folder: "name 1.ext", "name 2.ext"; the original stays; no overwrite
        q.go_to("x.bin");
        q.m->key(KEY_RETURN);
        q.enter("Copy to clipboard");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        CHECK(q.find_row("Paste here") >= 0);                                              // the clipboard has something now
        q.enter("Paste here");
        q.run_job();
        CHECK(q.has("/usb0/flat/x 1.bin") && q.has("/usb0/flat/x.bin") && !q.m->status_bad());
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Paste here");
        q.run_job();
        CHECK(q.has("/usb0/flat/x 2.bin"));
        CHECK_EQ(q.fs.files["/usb0/flat/x 2.bin"].size(), 10u);
        // the name has no extension, or several: the number goes before the last one only
        q.go_to("README");
        q.m->key(KEY_RETURN);
        q.enter("Copy to clipboard");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Paste here");
        q.run_job();
        CHECK(q.has("/usb0/flat/readme 1") || q.has("/usb0/flat/README 1"));
        q.go_to("arc.tar.gz");
        q.m->key(KEY_RETURN);
        q.enter("Copy to clipboard");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Paste here");
        q.run_job();
        CHECK(q.has("/usb0/flat/arc.tar 1.gz"));
        // a folder is copied as "name 1", content and all
        q.fs.put("/usb0/flat/fresh/in.dat", bytes(7, 4));
        q.m->key(KEY_F3);
        q.go_to("fresh/");
        q.m->key(KEY_RIGHT);                                       // RIGHT enters fresh; the ".." sheet there acts on fresh
        CHECK_STR(q.m->title(), "/usb0/flat/fresh");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Copy to clipboard");
        q.go_to("../");
        q.m->key(KEY_RETURN);                                      // up to /usb0/flat
        CHECK_STR(q.m->title(), "/usb0/flat");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Paste here");
        q.run_job();
        CHECK(q.has_dir("/usb0/flat/fresh 1") && q.has("/usb0/flat/fresh 1/in.dat") && q.has("/usb0/flat/fresh/in.dat"));
        // 2: copy to the clipboard, rename the source, paste elsewhere: the clipboard follows the rename
        q.go_to("x.bin");
        q.m->key(KEY_RETURN);
        q.enter("Copy to clipboard");
        q.go_to("x.bin");
        q.m->key(KEY_RETURN);
        q.enter("Rename");
        for (int i = 0; i < 20; i++) q.m->key(KEY_BACK);
        q.type("y.bin");
        q.m->key(KEY_RETURN);
        CHECK(q.has("/usb0/flat/y.bin") && !q.has("/usb0/flat/x.bin"));
        CHECK_STR(q.c.lib.clip[0], "/usb0/flat/y.bin");
        q.to_folder("usb0/", "other/");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Paste here");
        q.run_job();
        CHECK(q.has("/usb0/other/y.bin") && !q.m->status_bad());
        // a source that is really gone says so (not "cannot read")
        q.fs.remove("/usb0/flat/y.bin");
        q.go_to("../");
        q.m->key(KEY_RIGHT);
        q.enter("Paste here");
        q.run_job();
        CHECK(strstr(q.m->status(), "gone") != 0);
    }

    // Delete: the marked files of dest2, after the question
    r.m->key(KEY_LEFT);                                            // out of dest1, to /usb0/games
    CHECK_STR(r.m->title(), "/usb0/games");
    r.enter("dest2/");
    r.go_to("a.atr");
    r.m->key(KEY_SPACE); r.m->key(KEY_SPACE);
    r.go_to("a.atr");
    r.m->key(KEY_RETURN);
    r.enter("Delete");
    r.m->key(KEY_RETURN);                                          // YES
    r.run_job();
    CHECK(!r.has("/usb0/games/dest2/a.atr") && !r.has("/usb0/games/dest2/b.ATR"));
    CHECK(r.has("/usb0/games/a.atr"));                             // the files of another folder stay

    // ---- #38: files with a FAT long name (80 and 255 characters) in the list, the sheet, a mount, Rename, Copy to, Move to and Delete, each with the whole name
    {
        std::string n80, n255;
        for (int i = 0; i < 76; i++) n80 += (char)('a' + i % 26);
        n80 += ".atr";                                                             // 80 characters
        for (int i = 0; i < 251; i++) n255 += (char)('a' + (i * 7) % 26);   // lower case: the fake file system keeps keys that way
        n255 += ".atr";                                                            // 255: the longest FAT name
        CHECK_EQ(n80.size(), 80u);
        CHECK_EQ(n255.size(), 255u);
        Rig l(&gd);
        l.fs.put(("/usb0/longs/" + n80).c_str(), bytes(1024, 5));
        l.fs.put(("/usb0/longs/" + n255).c_str(), bytes(1024, 6));
        l.fs.put("/usb0/longs/short.atr", bytes(1024, 7));
        l.fs.mkdir("/usb0/longs/dst");
        l.fs.mkdir("/usb0/longs/dst2");
        const std::string tree_file = n255;                                        // a folder with such a file in it: copying it walks the folder, its frames hold whole paths
        l.fs.put(("/usb0/longs/tree/" + tree_file).c_str(), bytes(300, 9));
        l.m->open();
        l.m->key(KEY_F3);
        l.enter("usb0/"); l.enter("longs/");
        // listed whole: the row has the full name, the cursor's path is the full path
        CHECK(l.find_row(n80.c_str()) >= 0);
        CHECK(l.find_row(n255.c_str()) >= 0);
        l.go_to(n255.c_str());
        l.m->key(KEY_RETURN);                                                      // the cursor's path is the whole path: the clipboard takes it from there
        l.enter("Copy to clipboard");
        CHECK_EQ(l.c.lib.nclip, 1);
        CHECK_STR(l.c.lib.clip[0], ("/usb0/longs/" + n255).c_str());
        for (int pass = 0; pass < 2; pass++) {                                    // what the screen shows of it: cut with ".." inside, the extension kept
            const std::string &nm = pass ? n255 : n80;
            MenuCtl::BrowseRow br;
            l.m->browse_row(l.find_row(nm.c_str()), &br);
            std::string shown = br.name;
            CHECK(shown.size() < 64 && shown.find("..") != std::string::npos && shown.substr(shown.size() - 4) == ".atr");
            CHECK(shown.substr(0, 20) == nm.substr(0, 20));
        }
        // opened: the sheet of the file, its first action mounts it with the whole path (the 255 name: a path of 267 characters)
        for (int pass = 0; pass < 2; pass++) {
            const std::string &nm = pass ? n255 : n80;
            l.to_folder("usb0/", "longs/");
            l.go_to(nm.c_str());
            l.m->key(KEY_RETURN);
            CHECK(l.m->chooser());
            l.m->key(KEY_RETURN);                                                  // the first action of an .atr
            CHECK(!l.m->status_bad());
            const char *spec = a8_slot_spec(&l.c, 0);
            CHECK(spec != 0);
            if (spec) {
                CHECK_STR(spec, ("/usb0/longs/" + nm).c_str());
            }
            l.m->key(KEY_ESCAPE);
        }
        // renamed: the prefill is the whole name, the new one is the old one with another extension
        {
            l.to_folder("usb0/", "longs/");
            l.go_to(n255.c_str());
            l.m->key(KEY_RETURN);
            l.enter("Rename");
            CHECK(l.m->entering());
            for (int i = 0; i < 4; i++) l.m->key(KEY_BACK);                        // ".atr" off the prefilled whole name
            l.type(".xfd");
            l.m->key(KEY_RETURN);
            std::string renamed = n255.substr(0, 251) + ".xfd";
            CHECK(l.has(("/usb0/longs/" + renamed).c_str()) && !l.has(("/usb0/longs/" + n255).c_str()));
            n255 = renamed;
            CHECK(l.find_row(n255.c_str()) >= 0);
        }
        // copied (both marked), moved and deleted, each by the whole name
        l.go_to(n80.c_str());
        l.m->key(KEY_SPACE);
        l.go_to(n255.c_str());
        l.m->key(KEY_SPACE);
        l.go_to("tree/");
        l.m->key(KEY_SPACE);
        CHECK_EQ(l.c.lib.nmarked, 3);
        l.go_to(n80.c_str());
        l.m->key(KEY_RETURN);
        l.enter("Copy to...");
        CHECK(l.m->picking());
        for (int i = 0; i < 6 && l.left(l.m->selected()) != "dst/"; i++) l.m->key(KEY_DOWN);
        CHECK_STR(l.left(l.m->selected()).c_str(), "dst/");
        l.m->key(KEY_RETURN);
        while (l.m->selected() > 0) l.m->key(KEY_UP);
        l.m->key(KEY_RETURN);
        l.run_job();
        CHECK(l.has(("/usb0/longs/dst/" + n80).c_str()) && l.has(("/usb0/longs/dst/" + n255).c_str()));
        CHECK(l.has(("/usb0/longs/" + n80).c_str()) && l.has(("/usb0/longs/" + n255).c_str()));
        CHECK_EQ(l.fs.files["/usb0/longs/dst/" + lower_name(n255)].size(), 1024u);
        CHECK(l.has(("/usb0/longs/dst/tree/" + tree_file).c_str()));               // the folder's file, copied by its whole name through the walk
        CHECK_EQ(l.fs.files["/usb0/longs/dst/tree/" + lower_name(tree_file)].size(), 300u);
        l.m->key(KEY_F3);
        l.enter("dst/");
        l.go_to(n80.c_str());
        l.m->key(KEY_SPACE);
        l.go_to(n255.c_str());
        l.m->key(KEY_SPACE);
        l.go_to(n80.c_str());
        l.m->key(KEY_RETURN);
        l.enter("Move to...");
        CHECK(l.m->picking());
        l.m->key(KEY_LEFT);
        for (int i = 0; i < 8 && l.left(l.m->selected()) != "dst2/"; i++) l.m->key(KEY_DOWN);
        CHECK_STR(l.left(l.m->selected()).c_str(), "dst2/");
        l.m->key(KEY_RETURN);
        while (l.m->selected() > 0) l.m->key(KEY_UP);
        l.m->key(KEY_RETURN);
        l.run_job();
        CHECK(l.has(("/usb0/longs/dst2/" + n80).c_str()) && l.has(("/usb0/longs/dst2/" + n255).c_str()));
        CHECK(!l.has(("/usb0/longs/dst/" + n80).c_str()) && !l.has(("/usb0/longs/dst/" + n255).c_str()));
        l.m->key(KEY_LEFT);
        l.enter("dst2/");
        l.go_to(n80.c_str());
        l.m->key(KEY_SPACE);
        l.go_to(n255.c_str());
        l.m->key(KEY_SPACE);
        l.go_to(n80.c_str());
        l.m->key(KEY_RETURN);
        l.enter("Delete");
        l.m->key(KEY_RETURN);                                                      // YES
        l.run_job();
        CHECK(!l.has(("/usb0/longs/dst2/" + n80).c_str()) && !l.has(("/usb0/longs/dst2/" + n255).c_str()));
        CHECK(l.has(("/usb0/longs/" + n80).c_str()) && l.has(("/usb0/longs/" + n255).c_str()));     // the first folder's files stay
    }

    // ---- D7 (S6): "+" / "-" page the F3 list, the volume there is SHIFT only
    {
        Rig p(&gd);
        for (int i = 0; i < 23; i++) { char n[40]; snprintf(n, sizeof(n), "/usb0/pg/f%02d.txt", i); p.fs.put(n, bytes(4, 1)); }
        p.m->open();
        p.m->key(KEY_F3);
        p.enter("usb0/"); p.enter("pg/");
        CHECK_EQ(p.m->rows(), 24);                                 // ".." and 23 files: three pages of eight
        p.m->set_page_rows(8);                                     // what the screen tells: the rows its list shows
        int vol = p.st->find(VOLUME_KEY);
        CHECK(vol >= 0);
        int v0 = vol >= 0 ? p.st->value(vol) : -1;
        p.m->key(KEY_HOME);
        CHECK_EQ(p.m->selected(), 0);
        p.m->key('+');
        CHECK_EQ(p.m->selected(), 8);
        p.m->key('+');
        CHECK_EQ(p.m->selected(), 16);                             // the last page
        p.m->key('+');
        CHECK_EQ(p.m->selected(), 23);                             // clamped at the end, still on the last page
        p.m->key('+');
        CHECK_EQ(p.m->selected(), 23);
        p.m->key('-');
        CHECK_EQ(p.m->selected(), 15);                             // one page up
        p.m->key('-'); p.m->key('-'); p.m->key('-');
        CHECK_EQ(p.m->selected(), 0);                              // clamped at the top
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0);            // the bare keys did not touch the volume
        p.m->key(KEY_PAGEDOWN);
        CHECK_EQ(p.m->selected(), 8);                              // PageDown is the same page
        p.m->key(KEY_PAGEUP);
        CHECK_EQ(p.m->selected(), 0);
        // the volume on the C64 keyboard's keys and the USB SHIFT+- key; the selection stays
        p.m->key('{');
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0 + 1);
        p.m->key('}');
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0);
        p.m->key('}');
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0 - 1);
        p.m->key('{');
        p.m->key('_');                                             // USB SHIFT+-
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0 - 1);
        CHECK_EQ(p.m->selected(), 0);
        // elsewhere in the menu the T160 keys stay: on Home the bare "+" and "-" are the volume
        p.m->key(KEY_F1);
        p.m->key('+');
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0);
        p.m->key('-');
        CHECK_EQ(vol >= 0 ? p.st->value(vol) : -1, v0 - 1);
    }
    DONE("test_f3");
}
