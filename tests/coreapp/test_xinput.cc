// T196 S1-S4: XInput pads. The driver's match function (xinput_claim) on interface triples and on the capability gate, 360 and GIP report fixtures through the
// real pad service (pad_attach with the synthetic descriptor, pad_report, pad_state, the register words of the SNES and NES descriptors), the capture of the
// mapping wizard, two pads as two players, and the start sequences against a model of the OUT endpoint. The report bytes are WRITTEN HERE from the public
// layouts named in xinput.h; no pad was measured (spec T196 section 5), so what this proves is the conversion, not that a Series pad sends these bytes.
#include <vector>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#ifdef TEST_APP_ATARI800                 // the Atari's app (run.sh a8app, the unit a8_xinput) has its own core only
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
#elif defined(TEST_APP_SNES)             // the SNES's app (run.sh snesapp, the unit snes_xinput) has its own core only
#include "snes_regs.h"
extern const CoreDesc snes_desc;         // cores.h lists the shared app's cores; the SNES is its own app (cores/snes/app/lib)
#else
#error "test_xinput runs on a core's pad: build it as a core's app (run.sh <core>app, TEST_APP_<CORE>); the shared app has no core of its own left (T239.3 D61)"
#endif
#include "pad.h"
#include "hidpad.h"
#include "xinput.h"

typedef std::vector<uint8_t> V;

struct ModelOut : XiOut {
    std::vector<V> sent;
    bool write(const uint8_t *d, int n) override { sent.push_back(V(d, d + n)); return true; }
};

static int k1, k2;

// a 360 report: buttons byte 2, byte 3, triggers, left stick
static V r360(uint8_t b2, uint8_t b3, uint8_t lt = 0, uint8_t rt = 0, int16_t lx = 0, int16_t ly = 0)
{
    V r(20, 0);
    r[1] = 0x14; r[2] = b2; r[3] = b3; r[4] = lt; r[5] = rt;
    r[6] = (uint8_t)lx; r[7] = (uint8_t)((uint16_t)lx >> 8); r[8] = (uint8_t)ly; r[9] = (uint8_t)((uint16_t)ly >> 8);
    return r;
}
// a GIP input message 0x20: buttons bytes 4-5, triggers 6-9, left stick 10-13, right stick 14-17
static V g20(uint8_t b0, uint8_t b1, uint16_t lt = 0, uint16_t rt = 0, int16_t lx = 0, int16_t ly = 0, uint8_t opt = 0x20)
{
    V r(18, 0);
    r[0] = 0x20; r[1] = opt; r[2] = 7; r[3] = 14; r[4] = b0; r[5] = b1;
    r[6] = (uint8_t)lt; r[7] = (uint8_t)(lt >> 8); r[8] = (uint8_t)rt; r[9] = (uint8_t)(rt >> 8);
    r[10] = (uint8_t)lx; r[11] = (uint8_t)((uint16_t)lx >> 8); r[12] = (uint8_t)ly; r[13] = (uint8_t)((uint16_t)ly >> 8);
    return r;
}

// the pad service sees the report the way the driver hands it over
static bool feed(XiSession &s, const void *key, const V &native)
{
    uint8_t rep[XI_REPORT_LEN];
    if (!s.data(native.data(), (int)native.size(), rep)) return false;
    pad_report(key, rep, XI_REPORT_LEN);
    return true;
}
static uint32_t btn(int p) { uint32_t b = 0; uint8_t d = 0; pad_state(p, &b, &d); return b; }
static uint8_t dp(int p) { uint32_t b = 0; uint8_t d = 0; pad_state(p, &b, &d); return d; }
#define SRC(n) (1u << (n))

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
#ifdef TEST_APP_ATARI800
    // D4 on the Atari 800 XL (its app, run.sh a8app): the capability `pad` is exactly the cores that have a PadSpec - the Atari has neither
    CHECK_EQ(desc_has_capability(&atari800_desc, "pad"), atari800_desc.pad != NULL);
    CHECK(!desc_has_capability(&atari800_desc, "pad"));
    DONE("xinput");
#else
    int dl = 0;
    const uint8_t *desc = xinput_descriptor(&dl);

    // ---- S1: which interfaces are claimed
    CHECK_EQ(xinput_claim(0xFF, 0x5D, 0x01, true, true), XI_360);
    CHECK_EQ(xinput_claim(0xFF, 0x47, 0xD0, true, true), XI_GIP);
    CHECK_EQ(xinput_claim(0xFF, 0x5D, 0x01, false, true), XI_NONE);             // no pad sink: the host app, nothing set
    CHECK_EQ(xinput_claim(0xFF, 0x47, 0xD0, false, true), XI_NONE);
    CHECK_EQ(xinput_claim(0xFF, 0x5D, 0x01, true, false), XI_NONE);             // a core without the capability `pad`
    CHECK_EQ(xinput_claim(0xFF, 0x47, 0xD0, true, false), XI_NONE);
    CHECK_EQ(xinput_claim(0xFF, 0x5D, 0x02, true, true), XI_NONE);              // the 360 pad's plugin-module and audio interfaces
    CHECK_EQ(xinput_claim(0xFF, 0x5D, 0x03, true, true), XI_NONE);
    CHECK_EQ(xinput_claim(0xFF, 0xFD, 0x13, true, true), XI_NONE);
    CHECK_EQ(xinput_claim(0xFF, 0xFF, 0x00, true, true), XI_NONE);              // a vendor-class USB Ethernet adapter (AX88772)
    CHECK_EQ(xinput_claim(0xFF, 0x47, 0xD1, true, true), XI_NONE);
    CHECK_EQ(xinput_claim(0x08, 0x06, 0x50, true, true), XI_NONE);              // mass storage
    CHECK_EQ(xinput_claim(0x01, 0x01, 0x00, true, true), XI_NONE);              // audio
    CHECK_EQ(xinput_claim(0x03, 0x01, 0x01, true, true), XI_NONE);              // HID keyboard: stays the HID driver's
    CHECK_EQ(xinput_claim(0x03, 0x00, 0x00, true, true), XI_NONE);
    // D4: the capability `pad` is exactly the cores that have a PadSpec (the Neo Geo's: the unit neogeo_pad, built as its app)
    {
        const CoreDesc *all[] = { &snes_desc };
        for (const CoreDesc *d : all) CHECK_EQ(desc_has_capability(d, "pad"), d->pad != NULL);
        CHECK(desc_has_capability(&snes_desc, "pad"));
    }
    // the synthetic descriptor is a gamepad for hidpad_parse, with 16 buttons, a hat and X/Y
    {
        HidPadLayout l;
        CHECK(hidpad_parse(desc, dl, &l));
        CHECK(l.hat.ok && l.x.ok && l.y.ok);
        CHECK_EQ(l.nruns, 1);
        CHECK_EQ(l.runs[0].count, 16);
        CHECK_EQ(l.need, 40);                                       // 16 + 4 + 4 + 8 + 8 bits
    }

    // ---- S2 / S3: 360 reports through the pad service
    {
        FakeIo io; FakeFs fs; CoreCtx c; memset(&c, 0, sizeof(c)); c.desc = &snes_desc; c.io = &io; c.fs = &fs;
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "Xbox 360 pad", 0x045E, 0x028E, desc, dl));
        CHECK_STR(pad_name(0), "Xbox 360 pad");                                  // S3: the menu names the pad
        CHECK_EQ(pad_vid(0), 0x045E);
        ModelOut o;
        XiSession s;
        s.start(XI_360, &o, pad_player_of(&k1));
        // buttons: byte 3 A B X Y LB RB Guide, byte 2 Start Back L3 R3
        struct { uint8_t b2, b3; int src; } bt[] = { { 0, 0x10, 0 }, { 0, 0x20, 1 }, { 0, 0x40, 2 }, { 0, 0x80, 3 }, { 0, 0x01, 4 }, { 0, 0x02, 5 },
                                                      { 0x20, 0, 6 }, { 0x10, 0, 7 }, { 0x40, 0, 8 }, { 0x80, 0, 9 }, { 0, 0x04, 10 } };
        for (auto &b : bt) {
            CHECK(feed(s, &k1, r360(b.b2, b.b3)));
            CHECK_EQ(btn(0), SRC(b.src));
            CHECK_EQ(dp(0), 0);
        }
        // D-pad buttons and the diagonal; opposite directions cancel
        static const struct { uint8_t b2; uint8_t want; } dpad[] = { { 1, HIDPAD_DPAD_UP }, { 2, HIDPAD_DPAD_DOWN }, { 4, HIDPAD_DPAD_LEFT }, { 8, HIDPAD_DPAD_RIGHT },
                                                                      { 5, HIDPAD_DPAD_UP | HIDPAD_DPAD_LEFT }, { 10, HIDPAD_DPAD_DOWN | HIDPAD_DPAD_RIGHT },
                                                                      { 3, 0 }, { 12, 0 } };
        for (auto &d : dpad) {
            CHECK(feed(s, &k1, r360(d.b2, 0)));
            CHECK_EQ(dp(0), d.want);
            CHECK_EQ(btn(0), 0);
        }
        // the left stick is a D-pad past the deadzone (40 %), not before; Y up is positive on an XInput pad
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, -20000, 0)));  CHECK_EQ(dp(0), HIDPAD_DPAD_LEFT);
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, 20000, 0)));   CHECK_EQ(dp(0), HIDPAD_DPAD_RIGHT);
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, 0, 20000)));   CHECK_EQ(dp(0), HIDPAD_DPAD_UP);
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, 0, -20000)));  CHECK_EQ(dp(0), HIDPAD_DPAD_DOWN);
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, -9000, 9000))); CHECK_EQ(dp(0), 0);               // 27 %: inside the deadzone
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, -32768, 32767))); CHECK_EQ(dp(0), HIDPAD_DPAD_LEFT | HIDPAD_DPAD_UP);   // the extremes do not wrap
        CHECK(feed(s, &k1, r360(0, 0, 0, 0, 32767, -32768))); CHECK_EQ(dp(0), HIDPAD_DPAD_RIGHT | HIDPAD_DPAD_DOWN);
        // triggers are buttons from 25 %
        CHECK(feed(s, &k1, r360(0, 0, XI_TRIGGER_360 - 1, XI_TRIGGER_360 - 1)));  CHECK_EQ(btn(0), 0);
        CHECK(feed(s, &k1, r360(0, 0, XI_TRIGGER_360, 0)));                       CHECK_EQ(btn(0), SRC(XI_B_LT));
        CHECK(feed(s, &k1, r360(0, 0, 0, 255)));                                  CHECK_EQ(btn(0), SRC(XI_B_RT));
        // everything at once
        CHECK(feed(s, &k1, r360(0xFF, 0xF7, 255, 255, 30000, -30000)));
        CHECK_EQ(btn(0), 0x1FFFu);
        // a short report, another type: nothing handed over
        uint8_t rep[XI_REPORT_LEN];
        V shortr(10, 0); shortr[1] = 0x14;
        CHECK(!s.data(shortr.data(), (int)shortr.size(), rep));
        V other = r360(0, 0x10); other[0] = 0x01;                                 // type 1 is a status message of the pad, not an input report
        CHECK(!s.data(other.data(), (int)other.size(), rep));
        // the register word of the SNES: A (Xbox) = Button 1 = SNES B, serial bit 0; the stick left = bit 6
        CHECK(feed(s, &k1, r360(0, 0x10, 0, 0, -20000, 0)));
        CHECK(pad_pace(&c));
        CHECK_EQ((uint16_t)(io.regs[SNES_PAD1] | io.regs[SNES_PAD1 + 1] << 8), (1u << 0) | (1u << 6));
        // the NES: the same report on its own register is its unit nes_xinput
        pad_detach(&k1);
    }

    // ---- S2: GIP messages
    {
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "Xbox Wireless Controller", 0x045E, 0x0B12, desc, dl));
        ModelOut o;
        XiSession s;
        s.start(XI_GIP, &o, 0);
        // buttons byte 4: Start(2) Back(3) A(4) B(5) X(6) Y(7); byte 5: Up Down Left Right LB RB L3 R3
        struct { uint8_t b0, b1; int src; } bt[] = { { 0x10, 0, 0 }, { 0x20, 0, 1 }, { 0x40, 0, 2 }, { 0x80, 0, 3 }, { 0, 0x10, 4 }, { 0, 0x20, 5 },
                                                      { 0x08, 0, 6 }, { 0x04, 0, 7 }, { 0, 0x40, 8 }, { 0, 0x80, 9 } };
        for (auto &b : bt) {
            CHECK(feed(s, &k1, g20(b.b0, b.b1)));
            CHECK_EQ(btn(0), SRC(b.src));
            CHECK_EQ(dp(0), 0);
        }
        static const struct { uint8_t b1; uint8_t want; } dpad[] = { { 1, HIDPAD_DPAD_UP }, { 2, HIDPAD_DPAD_DOWN }, { 4, HIDPAD_DPAD_LEFT }, { 8, HIDPAD_DPAD_RIGHT },
                                                                      { 9, HIDPAD_DPAD_UP | HIDPAD_DPAD_RIGHT }, { 6, HIDPAD_DPAD_DOWN | HIDPAD_DPAD_LEFT }, { 3, 0 } };
        for (auto &d : dpad) {
            CHECK(feed(s, &k1, g20(0, d.b1)));
            CHECK_EQ(dp(0), d.want);
            CHECK_EQ(btn(0), 0);
        }
        CHECK(feed(s, &k1, g20(0, 0, 0, 0, -20000, 0)));   CHECK_EQ(dp(0), HIDPAD_DPAD_LEFT);
        CHECK(feed(s, &k1, g20(0, 0, 0, 0, 0, 20000)));    CHECK_EQ(dp(0), HIDPAD_DPAD_UP);
        CHECK(feed(s, &k1, g20(0, 0, 0, 0, 9000, -9000))); CHECK_EQ(dp(0), 0);
        // triggers: 10 bit, 25 % = 256
        CHECK(feed(s, &k1, g20(0, 0, XI_TRIGGER_GIP - 1, XI_TRIGGER_GIP - 1)));    CHECK_EQ(btn(0), 0);
        CHECK(feed(s, &k1, g20(0, 0, XI_TRIGGER_GIP, 0)));                         CHECK_EQ(btn(0), SRC(XI_B_LT));
        CHECK(feed(s, &k1, g20(0, 0, 0, 1023)));                                   CHECK_EQ(btn(0), SRC(XI_B_RT));
        // the guide button is its own message (0x07): it adds Button 11 to the last input state and releases again
        CHECK(feed(s, &k1, g20(0x10, 0)));
        V guide = { 0x07, 0x20, 0x09, 0x02, 0x01, 0x5B };
        CHECK(feed(s, &k1, guide));
        CHECK_EQ(btn(0), SRC(0) | SRC(XI_B_GUIDE));
        guide[4] = 0;
        CHECK(feed(s, &k1, guide));
        CHECK_EQ(btn(0), SRC(0));
        // other messages are not passed on: status 0x03, announce 0x02, a short or foreign input
        uint8_t rep[XI_REPORT_LEN];
        V st = { 0x03, 0x20, 0x01, 0x04, 0x01, 0, 0, 0 };
        CHECK(!s.data(st.data(), (int)st.size(), rep));
        V sh = g20(0x10, 0); sh.resize(12);
        CHECK(!s.data(sh.data(), (int)sh.size(), rep));
        CHECK(!s.data(st.data(), 2, rep));
        pad_detach(&k1);
        // a guide message before any input: nothing to add it to
        XiSession s2;
        ModelOut o2;
        s2.start(XI_GIP, &o2, 0);
        V g2 = { 0x07, 0x20, 0x01, 0x02, 0x01, 0x5B };
        CHECK(!s2.data(g2.data(), (int)g2.size(), rep));
    }

    // ---- S3: two pads are two players; the wizard's capture takes an XInput button
    {
        FakeIo io; FakeFs fs; CoreCtx c; memset(&c, 0, sizeof(c)); c.desc = &snes_desc; c.io = &io; c.fs = &fs;
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "Xbox 360 pad", 0x045E, 0x028E, desc, dl));
        CHECK(pad_attach(&k2, "Xbox Wireless Controller", 0x045E, 0x0B12, desc, dl));
        CHECK_EQ(pad_player_of(&k1), 0);
        CHECK_EQ(pad_player_of(&k2), 1);
        ModelOut o1, o2;
        XiSession s1, s2;
        s1.start(XI_360, &o1, pad_player_of(&k1));
        s2.start(XI_GIP, &o2, pad_player_of(&k2));
        CHECK(feed(s1, &k1, r360(0, 0x10)));
        CHECK(feed(s2, &k2, g20(0x20, 0)));
        CHECK_EQ(btn(0), SRC(0));
        CHECK_EQ(btn(1), SRC(1));
        CHECK(pad_pace(&c));
        CHECK_EQ((uint16_t)(io.regs[SNES_PAD1] | io.regs[SNES_PAD1 + 1] << 8), 1u << 0);
        CHECK_EQ((uint16_t)(io.regs[SNES_PAD2] | io.regs[SNES_PAD2 + 1] << 8), 1u << 8);      // Xbox B = Button 2 = SNES A, serial bit 8
        // F8 capture on player 1: waits for the release, then takes the Y button (Button 4 = source 3)
        pad_capture_start(0);
        uint8_t src = 0xEE;
        CHECK(!pad_capture_poll(&src));
        CHECK(feed(s1, &k1, r360(0, 0)));
        CHECK(!pad_capture_poll(&src));
        CHECK(feed(s1, &k1, r360(0, 0x80)));
        CHECK(pad_capture_poll(&src));
        CHECK_EQ(src, 3);
        // and the left stick direction is a source too
        pad_capture_start(0);
        CHECK(feed(s1, &k1, r360(0, 0)));
        CHECK(!pad_capture_poll(&src));
        CHECK(feed(s1, &k1, r360(0, 0, 0, 0, -30000, 0)));
        CHECK(pad_capture_poll(&src));
        CHECK_EQ(src, PAD_SRC_LEFT);
        // the pad leaves: its place is free and its word goes to zero
        pad_detach(&k1);
        CHECK(pad_pace(&c));
        CHECK_EQ((uint16_t)(io.regs[SNES_PAD1] | io.regs[SNES_PAD1 + 1] << 8), 0);
        pad_detach(&k2);
    }

    // ---- S4: the start sequences and the acknowledges, against the model of the OUT endpoint
    CHECK(V(xinput_360_magic, xinput_360_magic + 8) == (V{ 0xC1, 0x01, 0x00, 0x01, 0x00, 0x00, 0x14, 0x00 }));
    {
        ModelOut o;
        XiSession s;
        s.start(XI_GIP, &o, 0);
        CHECK_EQ(o.sent.size(), 3);                                                  // power on, LED on, authentication done (xpad.c), once
        CHECK(o.sent[0] == (V{ 0x05, 0x20, 0x00, 0x01, 0x00 }));
        CHECK(o.sent[1] == (V{ 0x0A, 0x20, 0x01, 0x03, 0x00, 0x01, 0x14 }));
        CHECK(o.sent[2] == (V{ 0x06, 0x20, 0x02, 0x02, 0x01, 0x00 }));
        uint8_t rep[XI_REPORT_LEN];
        // the announce message of the pad (command 0x02, options 0x20, payload 28 bytes) wants no answer unless it sets the ack bit
        V ann(32, 0); ann[0] = 0x02; ann[1] = 0x20; ann[2] = 0x01; ann[3] = 28;
        CHECK(!s.data(ann.data(), (int)ann.size(), rep));
        CHECK_EQ(o.sent.size(), 3);
        // with the ack bit: acknowledged with the sequence number, command, options (ack bit cleared) and payload length
        ann[1] = 0x30; ann[2] = 0x03;
        CHECK(!s.data(ann.data(), (int)ann.size(), rep));
        CHECK_EQ(o.sent.size(), 4);
        CHECK(o.sent[3] == (V{ 0x01, 0x20, 0x03, 0x09, 0x00, 0x02, 0x20, 28, 0x00, 0x00, 0x00, 0x00, 0x00 }));
        // the guide message with the ack bit is acknowledged and still delivers its report once an input was seen
        CHECK(s.data(g20(0x10, 0).data(), 18, rep));
        V guide = { 0x07, 0x30, 0x0A, 0x02, 0x01, 0x5B };
        CHECK(s.data(guide.data(), (int)guide.size(), rep));
        CHECK_EQ(o.sent.size(), 5);
        CHECK(o.sent[4] == (V{ 0x01, 0x20, 0x0A, 0x09, 0x00, 0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 }));
        // input messages never trigger the power-on again, an ack is never acknowledged
        for (int i = 0; i < 20; i++) CHECK(s.data(g20(0x10, 0).data(), 18, rep));
        V ack = { 0x01, 0x30, 0x01, 0x09, 0, 0x05, 0x20, 0x01, 0, 0, 0, 0, 0 };
        CHECK(!s.data(ack.data(), (int)ack.size(), rep));
        CHECK_EQ(o.sent.size(), 5);
        CHECK_EQ(s.writes(), 5);
        // an endpoint that refuses a write is not counted
        struct Full : XiOut { bool write(const uint8_t *, int) override { return false; } } full;
        XiSession s3;
        s3.start(XI_GIP, &full, 0);
        CHECK_EQ(s3.writes(), 0);
    }
    {
        // the 360: the LED packet sets the ring's player lamp, once, and the reports need no answer
        for (int player = 0; player < 4; player++) {
            ModelOut o;
            XiSession s;
            s.start(XI_360, &o, player);
            CHECK_EQ(o.sent.size(), 1);
            CHECK(o.sent[0] == (V{ 0x01, 0x03, (uint8_t)(0x06 + player) }));
            uint8_t rep[XI_REPORT_LEN];
            V r = r360(0, 0x10);
            for (int i = 0; i < 5; i++) CHECK(s.data(r.data(), (int)r.size(), rep));
            CHECK_EQ(o.sent.size(), 1);
        }
        // a session that was never started does nothing
        XiSession idle;
        uint8_t rep[XI_REPORT_LEN];
        V r = r360(0, 0x10);
        CHECK(!idle.data(r.data(), (int)r.size(), rep));
    }
    DONE("xinput");
#endif
}
