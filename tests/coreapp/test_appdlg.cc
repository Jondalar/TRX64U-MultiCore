// T104.3 D4: the dialogs an application asks for. A worker thread (the application's task) calls each one and sleeps; this thread plays the menu task:
// it calls service(), reads view() and feeds keys. Every answer is what the keys said, and the cancels answer what an application without a screen
// would be answered.
#include "check.h"
#include "hostos.h"
#include "fakes.h"
#include "appdlg.h"
#include "keyboard.h"
#include <atomic>
#include <unistd.h>

static HostOs os;

// Runs `fn` on a worker thread, drives the dialog with `keys` (-1 ends the list) and returns when the worker has its answer.
struct Run {
    std::thread t;
    std::atomic<bool> done{false};
    template <class F> explicit Run(F fn) : t([this, fn] { fn(); done = true; }) { }
    ~Run() { if (t.joinable()) { t.join(); } }
};

static bool wait_open(AppDlg &d, int ms = 3000)
{
    for (int i = 0; i < ms; i++) {
        d.service();
        if (d.open()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

static void drive(AppDlg &d, Run &r, const int *keys)
{
    CHECK(wait_open(d));
    for (const int *k = keys; *k >= 0; k++) {
        d.take_key(*k);
    }
    for (int i = 0; i < 300 && !r.done; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!r.done) {
        d.abandon();                                    // a defect must be a red check, not a worker that sleeps for ever
    }
    CHECK(r.done || true);
    for (int i = 0; i < 3000 && !r.done; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(r.done);
}

int main()
{
    FakeFs fs;
    fs.put_text("/usb0/cores/trx64u/a.crt", "A");
    fs.put_text("/usb0/cores/trx64u/b.crt", "B");
    fs.put_text("/usb0/cores/trx64u/c.txt", "C");
    fs.put_text("/usb0/cores/trx64u/0note.txt", "N");
    fs.put_text("/usb0/readme.txt", "R");
    fs.put_text("/usb0/games/x.crt", "X");
    fs.mkdir("/usb0/empty");
    AppDlg d(&os, &fs);

    // a message: any of RETURN, SPACE, the left arrow ends it
    {
        Run r([&] { d.message("Unzip", "Not enough memory."); });
        CHECK(wait_open(d));
        const DlgView &v = d.view();
        CHECK_EQ(v.kind, DK_MESSAGE);
        CHECK_STR(v.title, "Unzip");
        CHECK_STR(v.text, "Not enough memory.");
        const int k[] = { KEY_RETURN, -1 };
        drive(d, r, k);
        CHECK(!d.open());
    }
    // yes / no: the focus moves, RETURN takes it; Y and N are direct; the left arrow is "no"
    {
        int a = -1, b = -1, c = -1;
        { Run r([&] { a = d.buttons("T", "q", SVC_BUTTON_YES | SVC_BUTTON_NO); }); const int k[] = { KEY_RETURN, -1 }; drive(d, r, k); }
        { Run r([&] { b = d.buttons("T", "q", SVC_BUTTON_YES | SVC_BUTTON_NO); }); const int k[] = { KEY_RIGHT, KEY_RETURN, -1 }; drive(d, r, k); }
        { Run r([&] { c = d.buttons("T", "q", SVC_BUTTON_YES | SVC_BUTTON_NO); }); const int k[] = { 0x60, -1 }; drive(d, r, k); }
        CHECK_EQ(a, SVC_BUTTON_YES);
        CHECK_EQ(b, SVC_BUTTON_NO);
        CHECK_EQ(c, 0);
        bool y = false, n = true;
        { Run r([&] { y = d.confirm("T", "q"); }); const int k[] = { 'y', -1 }; drive(d, r, k); }
        { Run r([&] { n = d.confirm("T", "q"); }); const int k[] = { 'n', -1 }; drive(d, r, k); }
        CHECK(y && !n);
    }
    // a timed question (the prompts of the diag core, T175): an answer in time is the answer; with none the question closes by itself after the seconds given and answers
    // DLG_TIMEOUT, the seconds left count down on the view; a question with no time stays
    {
        int a = -1, b = -1, c = -1;
        { Run r([&] { a = d.buttons_timed("T", "q", SVC_BUTTON_YES | SVC_BUTTON_NO, 30); }); CHECK(wait_open(d)); CHECK_EQ(d.view().remaining_s, 30); const int k[] = { 'y', -1 }; drive(d, r, k); }
        CHECK_EQ(a, SVC_BUTTON_YES);
        {
            auto t0 = std::chrono::steady_clock::now();
            Run r([&] { b = d.buttons_timed("T", "q", SVC_BUTTON_YES | SVC_BUTTON_NO, 2); });
            CHECK(wait_open(d));
            CHECK_EQ(d.view().remaining_s, 2);
            bool saw1 = false;
            for (int i = 0; i < 5000 && !r.done; i++) {
                d.service();
                saw1 |= d.view().remaining_s == 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            CHECK(r.done);
            CHECK(saw1);
            CHECK(ms >= 1900 && ms < 3500);
            CHECK(!d.open());
        }
        CHECK_EQ(b, DLG_TIMEOUT);
        {
            Run r([&] { c = d.buttons_timed("T", "q", SVC_BUTTON_YES | SVC_BUTTON_NO, 0); });
            CHECK(wait_open(d));
            for (int i = 0; i < 1200; i++) {
                d.service();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            CHECK(d.open() && !r.done);
            CHECK_EQ(d.view().remaining_s, 0);
            const int k[] = { 'n', -1 };
            drive(d, r, k);
        }
        CHECK_EQ(c, SVC_BUTTON_NO);
    }
    // a list: the cursor, the window past the first screen, a letter, the cancel
    {
        static const char *items[] = { "Alpha", "Beta", "Gamma", "Delta", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "Last" };
        int a = -2, b = -2, c = -2, e = -2;
        { Run r([&] { a = d.choose("Pick", items, 18, 0, 0); }); const int k[] = { KEY_DOWN, KEY_DOWN, KEY_RETURN, -1 }; drive(d, r, k); }
        { Run r([&] { b = d.choose("Pick", items, 18, 0, 0); }); const int k[] = { KEY_END, KEY_RETURN, -1 }; drive(d, r, k); }
        { Run r([&] { c = d.choose("Pick", items, 18, 0, "abcdefghijklmnopq"); }); const int k[] = { 'c', -1 }; drive(d, r, k); }
        { Run r([&] { e = d.choose("Pick", items, 18, 5, 0); }); const int k[] = { KEY_BREAK, -1 }; drive(d, r, k); }
        CHECK_EQ(a, 2);
        CHECK_EQ(b, 17);
        CHECK_EQ(c, 2);
        CHECK_EQ(e, -1);
        // the window follows the cursor: on the last row of 18 the window starts at 18 - 14 = 4
        Run r([&] { d.choose("Pick", items, 18, 17, 0); });
        CHECK(wait_open(d));
        const DlgView &v = d.view();
        CHECK_EQ(v.sel, 17);
        CHECK_EQ(v.rows, DLG_ITEMS_MAX);
        CHECK_EQ(v.first, 18 - DLG_ITEMS_MAX);
        CHECK_STR(v.row[v.rows - 1], "Last");
        d.take_key(KEY_RETURN);
    }
    // text: typed, corrected, taken; ESC clears first, then cancels; the buffer is only written on RETURN
    {
        char buf[16] = "old";
        bool ok = false, ok2 = true, ok3 = true;
        { Run r([&] { ok = d.text("Name", buf, sizeof(buf)); }); const int k[] = { KEY_BACK, KEY_BACK, KEY_BACK, 'N', 'e', 'w', 'X', KEY_BACK, KEY_RETURN, -1 }; drive(d, r, k); }
        CHECK(ok);
        CHECK_STR(buf, "New");
        char buf2[16] = "keep";
        { Run r([&] { ok2 = d.text("Name", buf2, sizeof(buf2)); }); const int k[] = { 'z', KEY_ESCAPE, KEY_ESCAPE, -1 }; drive(d, r, k); }
        CHECK(!ok2);
        CHECK_STR(buf2, "keep");
        bool ok4 = false;
        { Run r([&] { ok4 = d.text("Name", buf2, sizeof(buf2)); }); const int k[] = { KEY_ESCAPE, 'q', KEY_RETURN, -1 }; drive(d, r, k); }   // ESC cleared "keep" first
        CHECK(ok4);
        CHECK_STR(buf2, "q");
        char buf3[4] = "";
        { Run r([&] { ok3 = d.text("Name", buf3, sizeof(buf3)); }); const int k[] = { 'a', 'b', 'c', 'd', 'e', KEY_RETURN, -1 }; drive(d, r, k); }
        CHECK(ok3);
        CHECK_STR(buf3, "abc");                         // as long as the buffer
    }
    // the pickers
    {
        char out[200] = "";
        bool ok = false;
        // from /usb0/cores/trx64u: ".." is first, then the .crt files only; the second file
        { Run r([&] { ok = d.pick_file("/usb0/cores/trx64u", ".crt", out, sizeof(out)); }); const int k[] = { KEY_DOWN, KEY_DOWN, KEY_RETURN, -1 }; drive(d, r, k); }
        CHECK(ok);
        CHECK_STR(out, "/usb0/cores/trx64u/b.crt");
        // up with the left arrow, into games, the file
        ok = false;
        { Run r([&] { ok = d.pick_file("/usb0/cores/trx64u/a.crt", ".crt", out, sizeof(out)); });
          CHECK(wait_open(d));
          CHECK_STR(d.view().input, "/usb0/cores/trx64u");              // a start that is a file is its folder
          d.take_key(0x60);                                          // up: /usb0/cores
          d.take_key(0x60);                                          // up: /usb0
          const DlgView &v = d.view();
          CHECK_STR(v.input, "/usb0");
          CHECK_STR(v.row[0], "..");
          CHECK_STR(v.row[1], "cores/");
          CHECK_STR(v.row[3], "games/");
          d.take_key(KEY_DOWN); d.take_key(KEY_DOWN); d.take_key(KEY_DOWN); d.take_key(KEY_RETURN);   // games/
          CHECK_STR(d.view().input, "/usb0/games");
          d.take_key(KEY_DOWN); d.take_key(KEY_RETURN);                // x.crt
          for (int i = 0; i < 3000 && !r.done; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        CHECK(ok);
        CHECK_STR(out, "/usb0/games/x.crt");
        // a file picker without an extension lists every file; cancelled leaves the buffer alone
        strcpy(out, "untouched");
        ok = true;
        { Run r([&] { ok = d.pick_file("/usb0/cores/trx64u", 0, out, sizeof(out)); }); const int k[] = { KEY_BREAK, -1 }; drive(d, r, k); }
        CHECK(!ok);
        CHECK_STR(out, "untouched");
        // a folder picker: "[ use this folder ]" first
        ok = false;
        { Run r([&] { ok = d.pick_folder("/usb0/games", out, sizeof(out)); }); const int k[] = { KEY_RETURN, -1 }; drive(d, r, k); }
        CHECK(ok);
        CHECK_STR(out, "/usb0/games");
        ok = false;
        { Run r([&] { ok = d.pick_folder("/usb0", out, sizeof(out)); });
          CHECK(wait_open(d));
          const DlgView &v = d.view();
          CHECK_STR(v.row[0], "[ use this folder ]");
          CHECK_STR(v.row[1], "..");
          CHECK_STR(v.row[2], "cores/");
          CHECK_STR(v.row[3], "empty/");
          CHECK_STR(v.row[4], "games/");
          CHECK_EQ(v.nitems, 5);                                      // folders only: no file in the list
          d.take_key(KEY_DOWN); d.take_key(KEY_DOWN); d.take_key(KEY_DOWN); d.take_key(KEY_RETURN);  // empty/
          d.take_key(KEY_RETURN);                                      // use this folder
          for (int i = 0; i < 3000 && !r.done; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        CHECK(ok);
        CHECK_STR(out, "/usb0/empty");
    }
    // the bar does not wait and does not take keys; a question on top of it is asked and answered, the bar stays
    {
        void *h = d.progress_open("Unzipping", 4);
        CHECK(d.service());
        CHECK(d.active() && !d.open());
        CHECK(d.view().bar && d.view().bar_total == 4 && d.view().bar_value == 0);
        d.progress_set(h, 1, "a.txt");
        d.progress_set(h, 1, 0);
        d.service();
        CHECK_EQ(d.view().bar_value, 2);
        CHECK_STR(d.view().bar_text, "a.txt");
        CHECK(!d.take_key(KEY_RETURN));                                // no question: the key is not the dialog's
        bool y = false;
        { Run r([&] { y = d.confirm("T", "q"); }); const int k[] = { 'y', -1 }; drive(d, r, k); }
        CHECK(y);
        CHECK(d.view().bar);
        d.progress_close(h);
        d.service();
        CHECK(!d.view().bar && !d.active());
    }
    // two askers: the second waits for the first, both get their own answer
    {
        int a = -1, b = -1;
        static const char *two[] = { "x", "y" };
        Run r1([&] { a = d.choose("one", two, 2, 0, 0); });
        CHECK(wait_open(d));
        Run r2([&] { b = d.choose("two", two, 2, 0, 0); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK_STR(d.view().title, "one");                              // the second has not got the screen
        d.take_key(KEY_DOWN); d.take_key(KEY_RETURN);
        CHECK(wait_open(d));
        CHECK_STR(d.view().title, "two");
        d.take_key(KEY_RETURN);
        for (int i = 0; i < 3000 && !(r1.done && r2.done); i++) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK_EQ(a, 1);
        CHECK_EQ(b, 0);
    }
    // the application ends with a question open: it is answered "cancelled" and nothing stays on the screen
    {
        int a = 99;
        static const char *one[] = { "x" };
        Run r([&] { a = d.choose("gone", one, 1, 0, 0); });
        CHECK(wait_open(d));
        d.abandon();
        for (int i = 0; i < 3000 && !r.done; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        if (!r.done) {                                  // a worker that would sleep for ever: say so and stop, the destructor would wait for it
            CHECK(r.done);
            printf("test_appdlg: %d checks, %d failed\n", g_checks, g_fails);
            fflush(stdout);
            _exit(1);
        }
        CHECK_EQ(a, -1);
        CHECK(!d.open());
    }
    // without a screen every question answers at once
    {
        AppDlg h(&os, &fs);
        h.set_headless(true);
        char b[8] = "x";
        static const char *one[] = { "x" };
        CHECK(!h.confirm("T", "q") && !h.text("T", b, sizeof(b)) && h.choose("T", one, 1, 0, 0) == -1 && h.buttons("T", "q", SVC_BUTTON_OK) == 0);
    }
    DONE("test_appdlg");
}
