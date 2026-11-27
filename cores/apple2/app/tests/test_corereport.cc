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
extern const CoreDesc apple2_desc;              // the Apple IIe is its own app (cores/apple2/app/lib): its build has its core only (T239.3 D61)

const CoreDesc *const core_table[] = { &testcore::desc, &apple2_desc, 0 };

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
    DONE("test_corereport");
}
