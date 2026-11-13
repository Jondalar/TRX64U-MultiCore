// T203 (#69): the start log. The scenarios run the calls hostui.cc direct_phase makes, in its order, against a fake clock, a fake key matrix and a
// model of the SD card: startrec_init, startrec_cfg, the samples through startrec_key (the call menukeys_sample ends with), directboot_decide,
// startrec_decide, startfile_store. S1-S5 here; S6 (the bug report) is in test_report.cc.
#include "startfile.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t g_ms;
static uint32_t clk(void) { return g_ms; }

// ---- the SD card as a model ----
static std::map<std::string, std::string> g_files;
static bool g_sd = true, g_ro = false;
static int g_saves;
static bool m_mkdir(void *, const char *) { return g_sd; }
static int m_load(void *, const char *p, char *b, int max)
{
    auto it = g_files.find(p);
    if (it == g_files.end()) { return -1; }
    int n = (int)it->second.size() < max ? (int)it->second.size() : max;
    memcpy(b, it->second.data(), n);
    return n;
}
static bool m_save(void *, const char *d, const char *n, const void *b, int len)
{
    if (!g_sd || g_ro) { return false; }
    g_saves++;
    g_files[std::string(d) + "/" + n] = std::string((const char *)b, len);
    return true;
}
static const StartLogFs FS = { m_mkdir, m_load, m_save, 0 };
static std::string file() { return g_files[STARTFILE_DIR "/" STARTFILE_NAME]; }
static bool has(const std::string &t, const char *s) { return t.find(s) != std::string::npos; }
static int blocks(const std::string &t) { int n = 0; size_t p = 0; while ((p = t.find("=== start #", p)) != std::string::npos) { n++; p++; } return n; }

// A start as direct_phase takes it. fire_at/cbm_at: from when the key is down (ms, 0xFFFFFFFF = never); cfg_at: when boot.cfg is readable.
struct Run { uint32_t fire_at, cbm_at, cfg_at; int direct; };
static DbReason run_start(const Run &r)
{
    const uint32_t WIN = 500;
    startrec_init(clk);
    g_ms = 0;
    startrec_window_open(WIN);
    DirectIn in;
    memset(&in, 0, sizeof(in));
    bool cfg = false;
    auto sample = [&]() {
        startrec_key(g_ms >= r.fire_at ? 0xEF : 0xFF, g_ms >= r.cbm_at ? 0xDF : 0xFF);   // low bit 4 = fire, low bit 5 = C=
        if (g_ms >= r.fire_at) { in.key_fire = true; }
        if (g_ms >= r.cbm_at) { in.key_cbm = true; }
    };
    while (!cfg && g_ms < 1500) {                          // the cfg wait loop
        if (g_ms >= r.cfg_at) { cfg = true; startrec_cfg(3, 1, r.direct, 5); break; }
        sample();
        g_ms += 20;
    }
    in.cfg_loaded = cfg;
    in.direct = cfg ? r.direct : 0;
    in.n = cfg ? 3 : 0;
    in.def = cfg ? 1 : -1;
    in.dstate = DD_OK;
    sample();
    while (g_ms < WIN && !in.key_cbm && !in.key_fire) {
        g_ms += 10;
        sample();
    }
    startrec_window_close();
    DbReason why = directboot_decide(in);
    startrec_decide(in, why);
    return why;
}
static bool store() { return startfile_store(&FS, "abc1234", "2026-11-11", "2026-11-11 14:05:09"); }
static void fresh() { g_files.clear(); g_sd = true; g_ro = false; g_saves = 0; }

int main()
{
    // S1: fire held from t=240 ms -> menu, the block names fire seen at its sample, the countdown stopped
    {
        fresh();
        DbReason w = run_start({ 240, 0xFFFFFFFF, 0, 1 });
        CHECK(w == DB_KEY_FIRE);
        CHECK(store());
        std::string f = file();
        CHECK(has(f, "=== start #1 | host app abc1234 built 2026-11-11 | clock 2026-11-11 14:05:09 ==="));
        CHECK(has(f, "fire: low in 1 samples, first at sample 25 (t=240 ms)"));
        CHECK(has(f, "C=: never seen"));
        CHECK(has(f, "decision: Menu: fire on port 1 was held (countdown stopped)"));
        CHECK(has(f, "raw port 1 first 0xFF last 0xEF"));
        CHECK(!has(f, "started by"));
        printf("--- example block ---\n%s---\n", f.c_str());
    }
    // S2: no key, direct boot on -> START, started by direct start
    {
        fresh();
        CHECK(run_start({ 0xFFFFFFFF, 0xFFFFFFFF, 0, 1 }) == DB_START);
        CHECK(store());
        std::string f = file();
        CHECK(has(f, "decision: START the auto core"));
        CHECK(has(f, "started by: direct start"));
        CHECK(has(f, "fire: never seen") && has(f, "C=: never seen"));
        CHECK(has(f, "boot.cfg read: cores 3, default 1, direct 1, countdown 5\n"));
    }
    // S3: boot.cfg readable only at 1200 ms (after the 500 ms window), fire held throughout -> the late read and the decision are in the block
    {
        fresh();
        DbReason w = run_start({ 0, 0xFFFFFFFF, 1200, 1 });
        CHECK(w == DB_KEY_FIRE);
        CHECK(store());
        std::string f = file();
        CHECK(has(f, "t=  1200 ms boot.cfg read: cores 3, default 1, direct 1, countdown 5 (LATE: after the key window ended)"));
        CHECK(has(f, "fire: low in ") && has(f, "first at sample 1 (t=0 ms)"));
        CHECK(has(f, "decision: Menu: fire on port 1 was held"));
        // never readable: the block says so, a held key still wins (#69)
        fresh();
        CHECK(run_start({ 0, 0xFFFFFFFF, 99999, 1 }) == DB_KEY_FIRE);
        CHECK(store());
        CHECK(has(file(), "boot.cfg: not read before the decision"));
    }
    // S4: menu shown (direct=0), the countdown runs out later -> "started by: countdown" with its time, in the same block
    {
        fresh();
        CHECK(run_start({ 0xFFFFFFFF, 0xFFFFFFFF, 0, 0 }) == DB_OFF);
        CHECK(store());
        CHECK(!has(file(), "started by"));
        CHECK(has(file(), "decision: Direct boot is off (countdown runs)"));
        g_ms = 5530;
        startrec_event("started by: %s", "countdown");
        CHECK(store());
        std::string f = file();
        CHECK(blocks(f) == 1);
        CHECK(has(f, "t=  5530 ms started by: countdown"));
        // a late boot.cfg read after the decision is a line of its own
        fresh();
        run_start({ 0xFFFFFFFF, 0xFFFFFFFF, 99999, 1 });
        g_ms = 3000;
        startrec_cfg(3, 1, 1, 5);
        CHECK(has(startrec_body(), "t=  3000 ms boot.cfg read AFTER the decision"));
    }
    // S5: 11 starts keep the last 10, in order; no SD / read-only SD: no error, nothing written, the block stays and goes out when the card is back
    {
        fresh();
        for (int i = 0; i < 11; i++) {
            run_start({ 0xFFFFFFFF, 0xFFFFFFFF, 0, 1 });
            CHECK(store());
        }
        std::string f = file();
        CHECK(blocks(f) == 10);
        CHECK(!has(f, "=== start #1 |") && has(f, "=== start #2 |") && has(f, "=== start #11 |"));
        CHECK((int)f.size() <= STARTFILE_MAX);
        // the cap holds with long blocks too
        fresh();
        for (int i = 0; i < 40; i++) {
            run_start({ 0, 0, 1200, 1 });
            for (int k = 0; k < 4; k++) { startrec_event("direct start failed: a long reason to fill the block up to its limit %d", k); }
            CHECK(store());
            CHECK((int)file().size() <= STARTFILE_MAX);
        }
        CHECK(blocks(file()) <= 10);
        // no SD card
        fresh();
        g_sd = false;
        run_start({ 240, 0xFFFFFFFF, 0, 1 });
        CHECK(!store());
        CHECK(g_files.empty() && g_saves == 0);
        // read-only card
        g_sd = true;
        g_ro = true;
        CHECK(!store());
        CHECK(g_files.empty());
        // the card comes back: the same block goes out as start #1
        g_ro = false;
        CHECK(store());
        CHECK(blocks(file()) == 1 && has(file(), "=== start #1 |"));
        // a second store of the same start replaces its block, it is not repeated
        CHECK(store());
        CHECK(blocks(file()) == 1);
    }
    // review fixes: a headerless 8 KB file, a full body keeps "started by", startrec_cfg tells whether it added a line
    {
        fresh();
        g_files[STARTFILE_DIR "/" STARTFILE_NAME] = std::string(STARTFILE_MAX, 'x');
        run_start({ 0xFFFFFFFF, 0xFFFFFFFF, 0, 1 });
        CHECK(store());
        CHECK(blocks(file()) == 1 && file().compare(0, 11, "=== start #") == 0 && (int)file().size() <= STARTFILE_MAX && !has(file(), "xxxx"));
        // a body filled with events: the "started by" line still gets in
        fresh();
        run_start({ 0xFFFFFFFF, 0xFFFFFFFF, 0, 0 });
        for (int i = 0; i < 60; i++) { startrec_event("filler event number %d with some text to take room", i); }
        startrec_event("started by: countdown");
        CHECK(strlen(startrec_body()) < STARTREC_BODY);
        CHECK(has(startrec_body(), "started by: countdown"));
        CHECK(store() && has(file(), "started by: countdown"));
        // cfg: false before the decision / second call, true only for a line added after the decision
        startrec_init(clk);
        CHECK(!startrec_cfg(3, 1, 1, 5));
        CHECK(!startrec_cfg(3, 1, 1, 5));
        startrec_init(clk);
        DirectIn in2;
        memset(&in2, 0, sizeof(in2));
        startrec_decide(in2, DB_KEY_FIRE);
        CHECK(startrec_cfg(3, 1, 1, 5));
        CHECK(!startrec_cfg(3, 1, 1, 5));
    }
    printf("test_startfile: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
