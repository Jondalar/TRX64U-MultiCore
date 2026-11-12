// T148 D13 (finish plan wave 1 item 19): the core's folder, the .cor reopen and the libraries run at t+320 ms, before the USB task has found the
// stick; the hand-over path names /Usb0. The start waits for the medium of that path (medium_wait), at most START_MEDIUM_WAIT_MS. The
// medium is a FakeFs that is "gone" until the model's clock passes `up_at`; the clock only advances in delay_ms, so no real time is spent.
#include <string>
#include "check.h"
#include "fakes.h"
#include "mediumwait.h"
#include "corefolder.h"

struct Clock : MediumEnv {
    FakeFs *fs;
    uint32_t t = 0, up_at;
    int delays = 0;
    Clock(FakeFs *f, uint32_t up) : fs(f), up_at(up) { fs->gone = up != 0; }
    uint32_t now_ms() override { return t; }
    void delay_ms(uint32_t ms) override { t += ms; delays++; if (delays > 5000) { printf("FAIL runaway wait\n"); exit(1); } if (t >= up_at) fs->gone = false; }
};

static const char *COR = "/Usb0/cores/amiga1200.cor";

static void ctx_for(CoreCtx *c, FakeFs *fs, const char *path)
{
    memset(c, 0, sizeof(*c));
    c->fs = fs;
    strncpy(c->cor_path, path, sizeof(c->cor_path) - 1);
}

int main()
{
    // the stick comes up 2900 ms after the start: the folder is made, the old .cfg moves in, the .cor opens afterwards
    {
        FakeFs fs;
        fs.put_size(COR, 100);
        fs.put_text("/Usb0/cores/amiga1200.cfg", "[Amiga]\nx=1\n");
        Clock env(&fs, 2900);
        CoreCtx c;
        ctx_for(&c, &fs, COR);
        start_core_folder(&c, &env);
        uint64_t sz; bool dir;
        CHECK(fs.stat("/Usb0/cores/amiga1200", &sz, &dir) && dir);
        CHECK(fs.stat("/Usb0/cores/amiga1200/roms", &sz, &dir) && dir);
        CHECK(fs.stat("/Usb0/cores/amiga1200/saves", &sz, &dir) && dir);
        CHECK_STR(fs.text("/Usb0/cores/amiga1200/amiga1200.cfg").c_str(), "[Amiga]\nx=1\n");
        CHECK(env.t >= 2900 && env.t < 3100);
        CoreFile *f = fs.open(COR, false);
        CHECK(f != 0);                                                       // the bundle read that follows finds the medium up
        if (f) fs.close(f);
    }
    // a medium that is up at once costs no wait
    {
        FakeFs fs;
        fs.put_size(COR, 100);
        Clock env(&fs, 0);
        CoreCtx c;
        ctx_for(&c, &fs, COR);
        start_core_folder(&c, &env);
        CHECK_EQ(env.delays, 0);
        uint64_t sz; bool dir;
        CHECK(fs.stat("/Usb0/cores/amiga1200/saves", &sz, &dir) && dir);
    }
    // /SD and /flash are there when the application is: never waited for, even when nothing lists
    {
        FakeFs fs;
        fs.gone = true;
        Clock env(&fs, 0xFFFFFFFFu);
        fs.gone = true;
        MediumWait w;
        medium_wait(&fs, "/SD/cores/a.cor", &env, START_MEDIUM_WAIT_MS, &w);
        CHECK(w.present && !w.waited); CHECK_EQ(env.delays, 0);
        medium_wait(&fs, "/flash/trx64u/a.cfg", &env, START_MEDIUM_WAIT_MS, &w);
        CHECK(w.present && !w.waited); CHECK_EQ(env.delays, 0);
        medium_wait(&fs, "/Usbfoo/x", &env, START_MEDIUM_WAIT_MS, &w);
        CHECK(w.present && !w.waited);                                       // "/Usb" without a digit is no USB root
    }
    // a stick that never comes: the wait ends at the limit, nothing is made, nothing crashes (the steps tell in their own words)
    {
        FakeFs fs;
        fs.put_size(COR, 100);
        Clock env(&fs, 0xFFFFFFFFu);
        fs.gone = true;
        CoreCtx c;
        ctx_for(&c, &fs, COR);
        MediumWait w;
        medium_wait(&fs, COR, &env, START_MEDIUM_WAIT_MS, &w);
        CHECK(w.waited && !w.present); CHECK(w.ms >= START_MEDIUM_WAIT_MS && w.ms < START_MEDIUM_WAIT_MS + 200);
        start_core_folder(&c, &env);
        fs.gone = false;
        uint64_t sz; bool dir;
        CHECK(!fs.stat("/Usb0/cores/amiga1200", &sz, &dir));
    }
    // two-digit and upper case roots
    {
        FakeFs fs;
        fs.put_size("/USB12/cores/a.cor", 1);
        Clock env(&fs, 500);
        MediumWait w;
        medium_wait(&fs, "/USB12/cores/a.cor", &env, START_MEDIUM_WAIT_MS, &w);
        CHECK(w.waited && w.present && w.ms >= 500);
    }
    DONE("test_mediumwait");
}
