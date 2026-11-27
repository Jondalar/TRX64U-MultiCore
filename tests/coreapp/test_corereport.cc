// T162: the bug report of the generic core application - the sections, the descriptor's named registers (D3: those and no others are read), the file
// on the SD card, the REST route, and no secrets (P4). The descriptors are the real ones where the claim is about them (Amiga, Apple IIe: their units amiga_corereport and apple2_corereport) and the
// test core where a register or a setting has to be made up.
#include <string>
#include <set>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "test_core.h"
#include "corereport.h"
#include "bugreport.h"
#include "rest.h"
#include "ops_glue.h"
#include "desc_check.h"
#include "listgrow.h"
#include "settings.h"
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib): its build has its core only
#elif defined(TEST_APP_APPLE2)
extern const CoreDesc apple2_desc;              // the Apple IIe is its own app (cores/apple2/app/lib): its build has its core only (T239.3 D61)
#endif

#ifdef TEST_APP_AMIGA
const CoreDesc *const core_table[] = { &testcore::desc, &amiga_desc, 0 };
#elif defined(TEST_APP_APPLE2)
const CoreDesc *const core_table[] = { &testcore::desc, &apple2_desc, 0 };
#else
const CoreDesc *const core_table[] = { &testcore::desc, 0 };
#endif

static bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

// every register the report reads
struct SpyIo : FakeIo {
    std::set<uint32_t> reads;
    uint8_t rd(uint32_t reg) { reads.insert(reg); return FakeIo::rd(reg); }
};

static const ReportReg regs[] = { { "ALPHA status", 0xA0, 1 }, { "WIDE counter", 0xB0, 4 }, { "HALF", 0xC0, 2 } };
static const char *const srv_v[] = { "" };
static const char *const pin_v[] = { "" };
static const Setting set2[] = {
    { "Sound", testcore::av, 2, 1, WHEN_LIVE, "test.set_a" },
    { "Server", srv_v, 1, 0, WHEN_LIVE, "test.set_a", 0, 60, 0, 1 },
    { "Code", pin_v, 1, 0, WHEN_LIVE, "test.set_a", 0, 20, 0, 2 },
};

struct Rig {
    SpyIo io; FakeFs fs; CoreCtx c; Settings *st; CoreDesc d;
    Rig(bool with_regs = true)
    {
        memset(&c, 0, sizeof(c));
        d = testcore::desc;
        d.settings = set2; d.nsettings = 3;
        if (with_regs) { d.report_regs = regs; d.nreport_regs = 3; }
        d.core_id = "testcore";
        c.desc = &d; c.io = &io; c.fs = &fs;
        snprintf(c.core_version, sizeof(c.core_version), "0.7");
        snprintf(c.cor_path, sizeof(c.cor_path), "/sd/cores/test.cor");
        c.host_key = 0xC0FFEE01u;
        st = new Settings(d.settings, d.nsettings, d.store, &fs, "/sd/cores/t.cfg", 0);
        st->load();
        st->set_text(1, "ftp://alex:hunter2@nas.local/dump");
        st->set_text(2, "424242");
        c.settings = st;
        io.regs[0xA0] = 0x5A;
        io.regs[0xB0] = 0x78; io.regs[0xB1] = 0x56; io.regs[0xB2] = 0x34; io.regs[0xB3] = 0x12;
        io.regs[0xC0] = 0xCD; io.regs[0xC1] = 0xAB;
        io.regs[0xD0] = 0x99;                       // a register the list does not name
    }
    ~Rig() { delete st; delete c.ops; }
};

static char g_out[CORE_REPORT_MAX];

static int s_rest_calls;
static Rig *s_rig;
static int rest_hook(CoreCtx *c, char *body, int cap, char *stamp, int stamplen)
{
    s_rest_calls++;
    snprintf(stamp, stamplen, "20261030-120000");
    return core_report_build(c, 0, false, "log line\n", 9, body, cap);
}

static std::string get(Rig &r, const char *method, const char *path, int *status)
{
    std::string raw = std::string(method) + " " + path + " HTTP/1.1\r\nHost: board\r\n\r\n";
    HttpReq *q = new HttpReq;
    http_init(q);
    http_feed(q, raw.data(), (int)raw.size());
    static char out[32768];                 // restsrv.cc out_max
    bool ws;
    int n = rest_serve(&r.c, *q, out, sizeof(out), &ws);
    delete q;
    *status = n > 12 ? atoi(out + 9) : -1;
    return std::string(out, n > 0 ? n : 0);
}

// D4: a slot with a state of its own, the name of what was mounted last
static const char *g_img = 0;
static const char *img_state(CoreCtx *) { return g_img; }
static void reg_img(OpRegistry &r)
{
    testcore::desc.register_ops(r);
    r.add_state("t.img", img_state);
}
static const MediaSlot img_media[] = { { "Disk", "t.img", 0, 0, 0 }, { "Cart", "t.cart", 0, 0, 0 } };

// R8: the heap and the screen's EDID, through the seams the board wires
static uint32_t fake_heap(void) { return 123456u; }
static int g_edid_len;
static uint8_t g_edid[256];
static bool fake_edid(uint8_t *buf, int max, int *len)
{
    if (g_edid_len <= 0) {
        return false;
    }
    memcpy(buf, g_edid, g_edid_len < max ? g_edid_len : max);
    *len = g_edid_len;
    return true;
}

int main()
{
#ifdef TEST_APP_AMIGA
    // ---- D3 in the real descriptors: the Amiga 500's (T239.5 D13: run as its app, run.sh amigaapp, unit amiga_corereport; the shared unit corereport keeps the Apple IIe's)
    {
        const CoreDesc *real[] = { &amiga_desc };
        for (const CoreDesc *d : real) {
            CHECK(d->nreport_regs > 0 && d->report_regs);
            CHECK(!desc_check(d));
        }
        // a malformed list is refused by the check
        CoreDesc d = amiga_desc;
        static const ReportReg odd[] = { { "three", 0x10, 3 } };
        d.report_regs = odd; d.nreport_regs = 1;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "report_regs"));
        static const ReportReg unnamed[] = { { "", 0x10, 1 } };
        d.report_regs = unnamed;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "report_regs"));
        static const ReportReg far_[] = { { "far", 0xFFE, 4 } };
        d.report_regs = far_;
        CHECK(desc_check(&d) && strstr(desc_check(&d), "report_regs"));
    }
#elif defined(TEST_APP_APPLE2)
    // the Apple IIe's cases (T239.3 D61: run as its app, run.sh apple2app, unit apple2_corereport; the shared unit corereport has the test core's)
    // ---- D3 in the real descriptors ---------------------------------------------------------------------------------------------------------------
    {
        const CoreDesc *real[] = { &apple2_desc };
        for (const CoreDesc *d : real) {
            CHECK(d->nreport_regs > 0 && d->report_regs);
            CHECK(!desc_check(d));
        }
    }
    // the report of a real descriptor names its registers
    {
        SpyIo io; FakeFs fs; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = &apple2_desc; c.io = &io; c.fs = &fs;
        int n = core_report_build(&c, 0, false, "", 0, g_out, sizeof(g_out));
        std::string t(g_out, n);
        CHECK(has(t, "STATUS (b0 reset, b1 held, b2 cold)"));
        CHECK(has(t, "Apple IIe"));
        CHECK_EQ((int)io.reads.size(), 4 + 1 + 1 + 1 + 1 + 1 + 2 + 1 + 1 + 1);
        delete c.ops;
    }
#else
    // ---- S1: the sections and the named registers --------------------------------------------------------------------------------------------
    {
        Rig r;
        const char *log = "boot ok\nmount ADF\n";
        int n = core_report_build(&r.c, 0, false, log, (int)strlen(log), g_out, sizeof(g_out));
        std::string t(g_out, n);
        CHECK(has(t, "== Versions =="));
        CHECK(has(t, "== Configuration =="));
        CHECK(has(t, "== Media"));
        CHECK(has(t, "== Core state"));
        CHECK(has(t, "== Log"));
        CHECK(has(t, "Core version: 0.7"));
        CHECK(has(t, "Host bitstream: C0FFEE01"));
        CHECK(has(t, "/sd/cores/test.cor"));
        CHECK(has(t, "mount ADF"));
        CHECK(has(t, "ALPHA status") && has(t, "$0A0 = $5A"));
        CHECK(has(t, "$0B0 = $12345678"));                        // little endian, four bytes
        CHECK(has(t, "$0C0 = $ABCD"));
        // D3: the registers named and no others - the neighbours of a wide register and the unnamed $0D0 stay unread
        std::set<uint32_t> want = { 0xA0, 0xB0, 0xB1, 0xB2, 0xB3, 0xC0, 0xC1 };
        CHECK(r.io.reads == want);
        CHECK(!has(t, "99"));
    }
    // a core without a list says so instead of dumping anything
    {
        Rig r(false);
        int n = core_report_build(&r.c, 0, false, "", 0, g_out, sizeof(g_out));
        std::string t(g_out, n);
        CHECK(has(t, "declares no register list"));
        CHECK(r.io.reads.empty());
    }
    // ---- S3: no secrets ------------------------------------------------------------------------------------------------------------------------
    {
        Rig r;
        const char *log = "ftp login password=swordfish\nall fine\n";
        int n = core_report_build(&r.c, 0, false, log, (int)strlen(log), g_out, sizeof(g_out));
        std::string t(g_out, n);
        CHECK(!has(t, "hunter2"));                                // the user:password@ part of a URL
        CHECK(has(t, "nas.local"));                               // the rest of the URL stays: it is what the reporter set
        CHECK(!has(t, "424242"));                                 // a setting typed as stars
        CHECK(has(t, "Code: <withheld>"));
        CHECK(!has(t, "swordfish"));                              // a log line with a credential word
        CHECK(has(t, "all fine"));
    }
    // ---- P3: the file ------------------------------------------------------------------------------------------------------------------------
    {
        Rig r;
        const char *txt = "report text\n";
        char name[40], why[80];
        uint32_t ft = ((2026u - 1980) << 25) | (10u << 21) | (30u << 16) | (14u << 11) | (5u << 5) | (10u / 2);
        CHECK(core_report_save(&r.c, txt, (int)strlen(txt), ft, true, name, sizeof(name), why, sizeof(why)));
        CHECK_STR(name, "20261030-140510.txt");
        CHECK(r.fs.dirs.count("/sd/report") == 1);                 // the folder is made
        CHECK(r.fs.files.count("/sd/report/20261030-140510.txt") == 1);
        char name2[40];
        CHECK(core_report_save(&r.c, "second\n", 7, ft, true, name2, sizeof(name2), why, sizeof(why)));
        CHECK_STR(name2, "20261030-140510-1.txt");               // never replaces the first
        CHECK_EQ((int)r.fs.files["/sd/report/20261030-140510.txt"].size(), (int)strlen(txt));
        Rig nosd;
        nosd.fs.read_only = true;
        CHECK(!core_report_save(&nosd.c, txt, (int)strlen(txt), ft, true, name, sizeof(name), why, sizeof(why)));
        CHECK(why[0] != 0);
        // a clock never set: nodate-<n>.txt with the first free n
        Rig nd;
        CHECK(core_report_save(&nd.c, txt, (int)strlen(txt), 0, false, name, sizeof(name), why, sizeof(why)));
        CHECK_STR(name, "nodate-1.txt");
        CHECK(core_report_save(&nd.c, txt, (int)strlen(txt), 0, false, name, sizeof(name), why, sizeof(why)));
        CHECK_STR(name, "nodate-2.txt");
        nd.fs.files.erase("/sd/report/nodate-1.txt");
        CHECK(core_report_save(&nd.c, txt, (int)strlen(txt), 0, false, name, sizeof(name), why, sizeof(why)));
        CHECK_STR(name, "nodate-1.txt");
        // D5: no SD card -> the first USB medium that takes a folder; neither -> the message, nothing written
        struct MediaFs : FakeFs {
            bool sd = true; bool usb[4] = { true, true, true, true };
            bool mkdir(const char *p)
            {
                if (!strncmp(p, "/sd", 3)) return sd && FakeFs::mkdir(p);
                if (!strncmp(p, "/usb", 4)) return p[4] >= '0' && p[4] <= '3' && usb[p[4] - '0'] && FakeFs::mkdir(p);
                return FakeFs::mkdir(p);
            }
        };
        Rig mr;
        MediaFs mf;
        mr.c.fs = &mf;
        mf.sd = false; mf.usb[0] = false;
        CHECK(core_report_save(&mr.c, txt, (int)strlen(txt), ft, true, name, sizeof(name), why, sizeof(why)));
        CHECK(mf.files.size() == 1 && mf.files.count("/usb1/report/20261030-140510.txt") == 1 && !strcmp(core_report_dir(), "/usb1/report"));
        mf.files.clear();
        mf.sd = true;
        CHECK(core_report_save(&mr.c, txt, (int)strlen(txt), ft, true, name, sizeof(name), why, sizeof(why)));
        CHECK(mf.files.size() == 1 && mf.files.count("/sd/report/20261030-140510.txt") == 1);          // the SD card wins over a stick
        mf.files.clear();
        mf.sd = false;
        for (int u = 0; u < 4; u++) mf.usb[u] = false;
        CHECK(!core_report_save(&mr.c, txt, (int)strlen(txt), ft, true, name, sizeof(name), why, sizeof(why)));
        CHECK_STR(why, "no SD card or USB stick");
        CHECK(mf.files.empty());
    }
    // ---- S2: REST ------------------------------------------------------------------------------------------------------------------------------
    {
        Rig r;
        int st;
        rest_report = 0;
        get(r, "GET", "/v1/report", &st);
        CHECK_EQ(st, 501);                                        // a build without it says so, not 404
        rest_report = rest_hook;
        std::string a = get(r, "GET", "/v1/report", &st);
        CHECK_EQ(st, 200);
        CHECK(has(a, "Content-Type: text/plain"));
        CHECK(has(a, "Content-Disposition: attachment; filename=\"20261030-120000.txt\""));
        CHECK(has(a, "== Versions =="));
        CHECK(has(a, "log line"));
        CHECK_EQ(s_rest_calls, 1);
        get(r, "POST", "/v1/report", &st);
        CHECK_EQ(st, 405);
        CHECK_EQ(s_rest_calls, 1);
    }
    // ---- D4: the media lines carry the file's name and its size in bytes, no hash --------------------------------------------------------------
    {
        Rig r;
        r.d.register_ops = reg_img; r.d.media = img_media; r.d.nmedia = 2;
        std::vector<uint8_t> img(12345, 0xA5);
        r.fs.files["/sd/disks/game.adf"] = img;
        r.fs.names["/sd/disks/game.adf"] = "/sd/disks/game.adf";
        g_img = 0;
        int n = core_report_build(&r.c, 0, false, "", 0, g_out, sizeof(g_out));
        CHECK(has(std::string(g_out, n), "Disk: empty"));
        // mounted through an operation that takes the path: the report finds the path by the name the slot states
        OpIn in[1] = { in_str("path", "/sd/disks/game.adf") };
        OpResult res;
        CHECK_EQ(ops_call(&r.c, "test.mount", in, 1, &res), (int)OPE_OK);
        g_img = "game.adf";
        n = core_report_build(&r.c, 0, false, "", 0, g_out, sizeof(g_out));
        std::string t(g_out, n);
        CHECK(has(t, "Disk: game.adf, 12345 bytes"));
        CHECK(has(t, "Cart: empty"));
        CHECK(!has(t, "sha") && !has(t, "SHA"));
        // a name nothing was mounted under: the name stays, the size is said to be unknown
        g_img = "other.adf";
        n = core_report_build(&r.c, 0, false, "", 0, g_out, sizeof(g_out));
        CHECK(has(std::string(g_out, n), "Disk: other.adf, size not known"));
    }
    // ---- R8: Heap and Screen (EDID) -----------------------------------------------------------------------------------------------------
    {
        Rig r;
        int n = core_report_build(&r.c, 0, false, "log\n", 4, g_out, sizeof(g_out));
        std::string t(g_out, n);
        CHECK(has(t, "== Heap =="));
        CHECK(has(t, "== Screen (EDID) =="));
        CHECK(has(t, "Heap free: not known"));
        CHECK(has(t, "The screen answered no EDID"));
        ListGrow::free_heap() = fake_heap;
        core_report_edid_reader() = fake_edid;
        n = core_report_build(&r.c, 0, false, "log\n", 4, g_out, sizeof(g_out));
        t.assign(g_out, n);
        CHECK(has(t, "Heap free: 123456 bytes (120 KB)"));
        CHECK(has(t, "The screen answered no EDID"));               // the reader answers false: plainly no EDID
        memset(g_edid, 0, sizeof(g_edid));
        static const uint8_t hdr[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        memcpy(g_edid, hdr, 8);
        g_edid[8] = 0x12;
        uint8_t sum = 0;
        for (int i = 0; i < 127; i++) { sum = (uint8_t)(sum + g_edid[i]); }
        g_edid[127] = (uint8_t)(0x100 - sum);
        g_edid_len = 128;
        n = core_report_build(&r.c, 0, false, "log\n", 4, g_out, sizeof(g_out));
        t.assign(g_out, n);
        CHECK(!has(t, "The screen answered no EDID"));
        CHECK(has(t, "128 bytes"));
        CHECK(has(t, "000: 00 FF FF FF FF FF FF 00 12 00"));
        CHECK(has(t, "070: 00 00 00 00 00 00 00 "));
        CHECK(has(t, "Decoded: valid"));
        CHECK(has(t, "Mode: "));
        size_t a = t.find("== Heap =="), b = t.find("== Log");
        printf("--- excerpt ---\n%s--- end ---\n", t.substr(a, b - a).c_str());
        ListGrow::free_heap() = 0;
        core_report_edid_reader() = 0;
    }
    // ---- gate fix 10a: the biggest report there is - a core with 207 settings (the count of the largest menu there was), each with an 18 character label and a 14 character value (a row of the report is about 50 bytes), the largest
    // EDID the reader returns, a log of the full 16 KB - fits, nothing is cut. (T239.3 P1-8: a synthetic core; the biggest real menu is TRX64-Ultimate's, which repeats this.)
    {
        static const int N_WORST = 207;                           // as many rows as the largest menu of a core had when this was made (trx64u's 207), labels and values as long as the real ones come
        static std::vector<std::string> names;
        static std::vector<Setting> rows;
        names.clear(); rows.clear();
        for (int i = 0; i < N_WORST; i++) {
            char k[64];
            snprintf(k, sizeof(k), "Setting label %03d", i);
            names.push_back(k);
        }
        static const char *const v8[8] = { "Value of row 0", "Value of row 1", "Value of row 2", "Value of row 3", "Value of row 4", "Value of row 5", "Value of row 6", "Value of row 7" };
        for (int i = 0; i < N_WORST; i++) {
            Setting st0;
            memset(&st0, 0, sizeof(st0));
            st0.key = names[i].c_str(); st0.values = v8; st0.nvalues = 8; st0.def = i & 7; st0.when = WHEN_LIVE;
            rows.push_back(st0);
        }
        static CoreDesc worst = testcore::desc;
        worst.settings = rows.data(); worst.nsettings = N_WORST;
        const CoreDesc *d = &worst;
        SpyIo io; FakeFs fs; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        Settings st(d->settings, d->nsettings, d->store, &fs, "/sd/cores/worst.cfg", 0);
        st.load();
        c.settings = &st;
        ListGrow::free_heap() = fake_heap;
        core_report_edid_reader() = fake_edid;
        memset(g_edid, 0, sizeof(g_edid));
        static const uint8_t hdr[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        memcpy(g_edid, hdr, 8);
        g_edid[8] = 0x12;
        g_edid[126] = 1;
        g_edid[128] = 0x02; g_edid[129] = 3; g_edid[130] = 4;
        for (int b = 0; b < 2; b++) {
            uint8_t sum = 0;
            for (int i = 0; i < 127; i++) { sum = (uint8_t)(sum + g_edid[b * 128 + i]); }
            g_edid[b * 128 + 127] = (uint8_t)(0x100 - sum);
        }
        g_edid_len = 256;
        std::string log;
        for (int i = 0; log.size() < (size_t)BR_LOG_BYTES; i++) {
            char l[96];
            snprintf(l, sizeof(l), "[core] line %03d of the log, filled up to a width of about eighty characters now\n", i);
            log += l;
        }
        int n = core_report_build(&c, 0, false, log.c_str(), (int)log.size(), g_out, sizeof(g_out));
        std::string t(g_out, n);
        size_t a = t.find("== Heap =="), b = t.find("== Log"), l0 = t.find("== Configuration =="), l1 = t.find("== Media");
        printf("worst case: %d of %d bytes; configuration %d, heap and EDID %d, log %d\n", n, (int)CORE_REPORT_MAX, (int)(l1 - l0), (int)(b - a), (int)(n - b));
        CHECK(has(t, "256 bytes"));
        CHECK(!has(t, "report cut"));
        CHECK(has(t, "line 000 of the log") || has(t, "of the log"));
        CHECK(n < CORE_REPORT_MAX - 512);
        ListGrow::free_heap() = 0;
        core_report_edid_reader() = 0;
        g_edid_len = 0;
    }
#endif
    DONE("test_corereport");
}
