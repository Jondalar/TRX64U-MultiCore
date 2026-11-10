// T104 5.15 / T85: the Amiga 500 runs the generic core application. Its record (coreapp/cores/amiga.cc, amiga_desc) is read against the
// register decode of the gateware the A500 core is built from (cores/amiga/rtl/trx/amiga_regs.v, NOT the A1200's own copy in cores/amiga1200):
// the application is driven through a start (settings, hold, release), the four floppy slots, both hard disks, the mouse and the picture
// window, and every register it writes or reads must be one the A500 gateware decodes. The A1200 record is run the same way as the
// control of the checker itself: it uses the display and crop window registers, which only the A1200 copy has, and must be caught.
#include "check.h"
#include "amiga_model.h"
#include "boot.h"
#include "engines.h"
#include "fbwin.h"
#include "mousesink.h"
#include "ops_glue.h"
#include "video.h"
#include <fstream>
#include <set>
#include <sstream>

// the decode of the gateware: every `12'hXXX` of the write case, and the bytes of every word of the read case (10'hXXX is the word number)
struct Decode { std::set<uint32_t> wr, rd; };

static Decode read_decode(const char *path)
{
    Decode d;
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string t = ss.str();
    size_t w0 = t.find("if (we && p0)"), w1 = t.find("default: ;", w0);
    size_t r0 = t.find("case (off[11:2])"), r1 = t.find("default: ;", r0);
    CHECK(w0 != std::string::npos && w1 != std::string::npos && r0 != std::string::npos && r1 != std::string::npos);
    if (w0 == std::string::npos || r0 == std::string::npos) return d;
    for (size_t p = w0; (p = t.find("12'h", p)) != std::string::npos && p < w1; p += 4) {
        d.wr.insert((uint32_t)strtoul(t.c_str() + p + 4, 0, 16));
    }
    for (size_t p = r0; (p = t.find("10'h", p)) != std::string::npos && p < r1; p += 4) {
        uint32_t word = (uint32_t)strtoul(t.c_str() + p + 4, 0, 16);
        for (int b = 0; b < 4; b++) d.rd.insert(word * 4 + b);
    }
    return d;
}

static std::vector<uint8_t> blob(uint32_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = (uint8_t)(i * seed + (i >> 7));
    return v;
}

// Drives a machine the way the application does and returns the registers outside the decode (writes first, then reads).
static std::vector<std::string> outside(const CoreDesc *desc, const Decode &dec, int *n_wr = 0, int *n_rd = 0)
{
    FakeFs fs;
    fs.put("/usb0/a.adf", blob(AM_ADF_SIZE, 3));
    fs.put("/usb0/h.hdf", blob(64 * 512, 5));
    AmigaRig r(&fs, desc);
    r.io.regs[AM_FEAT] = desc->fbwin ? (AM_FEAT_FBWIN | 1) : 0;                // what the two gateware builds report (A500: bytes, no window registers)
    engines_of(&r.c);
    Video vid(desc->video, &r.px, &r.io);
    r.c.video = &vid;
    vid.init();
    vid.show();
    if (desc->fbwin) {                                                         // a display window that is not the standard one: the crop window moves
        uint32_t h = 0x91u | (0x1A1u << 16), v = 0x2Cu | (0x12Cu << 16);
        for (int i = 0; i < 4; i++) { r.io.regs[AM_DIW_H + i] = (uint8_t)(h >> (8 * i)); r.io.regs[AM_DIW_V + i] = (uint8_t)(v >> (8 * i)); }
    }
    boot_apply_settings(&r.c, true);
    desc->init(&r.c, 0, 0);
    desc->release(&r.c, 0, 0);
    char msg[200];
    for (int s = 0; s < 4; s++) {
        CHECK_EQ(win_mount(&r.c, s, "/usb0/a.adf", msg, sizeof(msg)), 0);
    }
    for (int u = 0; u < 2; u++) {
        CHECK_EQ(sec_mount(&r.c, u, "/usb0/h.hdf", msg, sizeof(msg)), 0);
    }
    r.io.post(0, false, 3);
    sec_service(&r.c, 16);
    r.io.log_track(0, 7);
    mouse_sink_configure(desc->mouse);
    mouse_sink_reset();
    mouse_sink_motion(5, -3, 1);
    r.io.delay_ms(5);
    mouse_sink_pace(&r.c);
    for (int i = 0; i < 40; i++) {
        r.io.delay_ms(100);
        fbwin_poll(&r.c);
        OpResult pr;
        ops_call(&r.c, "media.poll", 0, 0, &pr);
    }
    CHECK_EQ(win_eject(&r.c, 0, msg, sizeof(msg)), 0);
    CHECK_EQ(sec_eject(&r.c, 1, msg, sizeof(msg)), 0);
    std::vector<std::string> bad;
    int w = 0, rd = 0;
    for (auto &x : r.io.log) {
        w++;
        if (!dec.wr.count(x.reg)) { char b[40]; snprintf(b, sizeof(b), "write %03X", (unsigned)x.reg); bad.push_back(b); }
    }
    for (uint32_t x : r.io.rdlog) {
        rd++;
        if (!dec.rd.count(x)) { char b[40]; snprintf(b, sizeof(b), "read %03X", (unsigned)x); bad.push_back(b); }
    }
    if (n_wr) *n_wr = w;
    if (n_rd) *n_rd = rd;
    return bad;
}

// T191: the clock. The setting Real-time clock Off writes nothing; On loads the board's time once (13 nibbles under the loader hold, then LOADED),
// a second release does not load again, and without a board clock the fixed date goes in.
static void rtc_case(const CoreDesc *desc)
{
    FakeFs fs;
    AmigaRig r(&fs, desc);
    r.io.have_time = true;
    int rs = r.st->find("Real-time clock (at next reset)");
    CHECK(rs >= 0);
    CHECK_EQ(desc->settings[rs].def, 0);                                       // default Off: the stock machine
    boot_apply_settings(&r.c, true);
    desc->init(&r.c, 0, 0);
    desc->release(&r.c, 0, 0);
    CHECK_EQ(r.io.regs[AM_RTC_EN] & 1, 0);
    CHECK_EQ(r.io.rtc_commits, 0);
    CHECK(!r.io.rtc_loaded);
    r.st->set(rs, 1);                                                          // On, at the next reset
    boot_apply_settings(&r.c, true);
    desc->init(&r.c, 0, 0);
    desc->release(&r.c, 0, 0);
    CHECK_EQ(r.io.regs[AM_RTC_EN] & 1, 1);
    CHECK_EQ(r.io.rtc_commits, 16);                                            // loader hold, 13 nibbles, zero the second, release the hold
    CHECK_EQ(r.io.rtc_unheld, 0);
    CHECK(r.io.rtc_loaded && !r.io.rtc_lhold);
    static const uint8_t want[13] = { 9, 0, 5, 0, 4, 1, 6, 0, 1, 1, 6, 2, 5 };   // S1 S10 MI1 MI10 H1 H10 D1 D10 MO1 MO10 Y1 Y10 W: 2026-11-06 14:05:09, Friday
    for (int i = 0; i < 13; i++) {
        CHECK_EQ(r.io.rtc_nib[i], want[i]);
    }
    desc->init(&r.c, 0, 0);                                                    // a machine reset keeps the time the machine has
    desc->release(&r.c, 0, 0);
    CHECK_EQ(r.io.rtc_commits, 16);
    r.io.rtc_loaded = false;                                                   // a reloaded bitstream: not loaded, and no board clock this time
    r.io.have_time = false;
    desc->init(&r.c, 0, 0);
    desc->release(&r.c, 0, 0);
    CHECK_EQ(r.io.rtc_commits, 32);
    CHECK(r.io.rtc_nib[11] == 2 && r.io.rtc_nib[10] == 6 && r.io.rtc_nib[9] == 0 && r.io.rtc_nib[8] == 1 && r.io.rtc_nib[7] == 0 && r.io.rtc_nib[6] == 1);   // 2026-01-01
}

int main()
{
    rtc_case(&amiga_desc);
    rtc_case(&amiga1200_desc);
    Decode dec = read_decode(COREAPP_DIR "/../cores/amiga/rtl/trx/amiga_regs.v");
    CHECK(dec.wr.count(AM_CTRL) && dec.wr.count(AM_HDF_MOUNT) && dec.wr.count(AM_MOUSE_DY) && dec.rd.count(AM_STATUS) && dec.rd.count(AM_FEAT));
    CHECK(!dec.wr.count(AM_FB_SKIP_X) && !dec.rd.count(AM_DIW_H));            // the decode is the A500's: the A1200 registers are not in it

    // the A500 record: the application touches nothing the gateware does not decode, and it does touch the machine (not vacuous)
    int nw = 0, nr = 0;
    std::vector<std::string> bad = outside(&amiga_desc, dec, &nw, &nr);
    for (auto &b : bad) printf("A500 uses a register its gateware does not have: %s\n", b.c_str());
    CHECK_EQ(bad.size(), 0);
    printf("A500: %d writes, %d reads\n", nw, nr);
    CHECK(nw > 30 && nr > 50);
    CHECK(amiga_desc.fbwin == 0 && amiga_desc.sector_media && amiga_desc.window_media && amiga_desc.mouse);

    // the control of the checker: the A1200 record writes the display/crop window registers and the checker sees them
    std::vector<std::string> bad12 = outside(&amiga1200_desc, dec);
    bool crop = false, diw = false;
    for (auto &b : bad12) { crop |= b == "write 0D0"; diw |= b == "read 0C0"; }
    CHECK(crop && diw);

    // what the old application gave the A500 is in the record: four drives with the dirty log, two hard disks, the mouse, Kickstart 1.3, 512K/OCS
    CHECK_EQ(amiga_desc.window_media->nslots, 4);
    CHECK_EQ(amiga_desc.sector_media->nunits, 2);
    CHECK_EQ(amiga_desc.romset->want, KS_VER_13);
    CHECK_STR(amiga_desc.cor_name, "Amiga (OCS/ECS, 68000)");
    CHECK_EQ(amiga_desc.settings[4].nvalues, 2);                               // OCS, ECS: no AGA on a 68000 machine
    DONE("test_amiga500");
}
