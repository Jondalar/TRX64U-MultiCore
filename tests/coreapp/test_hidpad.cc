// T156 P1: the HID report descriptor parser and the report decoder (coreapp/src/hidpad.cc). The descriptors are written here to the HID 1.11 rules and
// to the shape of common pads; NONE of them is the real 8BitDo SN30 Pro's (2DC8:6001, 114 bytes) - its bytes have not been read yet (T156 5), so what
// this test proves is the parser, not that pad.
#include <vector>
#include "check.h"
#include "hidpad.h"

typedef std::vector<uint8_t> V;

// Switch-Pro-like D-input: 14 buttons, a hat (4 bits, 0..7, null 8..15), X Y Z Rz at 8 bits, one vendor byte. Report of 8 bytes, no report ID.
static const V d_hat = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x81, 0x02,
    0x95, 0x02, 0x81, 0x01,
    0x05, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x75, 0x04, 0x95, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x95, 0x01, 0x81, 0x01,
    0x26, 0xFF, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,
    0xC0 };

// A SNES-style clone pad (DragonRise shape): X and Y 8 bit 0..255 are the D-pad (0, 127, 255), the hat is also there, 12 buttons start at bit 44, in a
// nested logical collection.
static const V d_clone = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0xA1, 0x02,
    0x75, 0x08, 0x95, 0x05, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x35, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x75, 0x01, 0x95, 0x0C, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x75, 0x01, 0x95, 0x08, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01, 0x81, 0x02,
    0xC0, 0xC0 };

// Report IDs: ID 1 is a vendor report that comes first, ID 3 holds 8 buttons and two signed axes (-127..127), the hat is 1..8 with 0 as null in ID 3 too.
static const V d_ids = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x01, 0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x03, 0x81, 0x02,
    0x85, 0x03, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
    0xC0 };

// 16 bit signed axes, buttons as two runs (Button 1..4, then 7..10 after a gap), D-pad as four one-bit usages 0x90..0x93 (Up Down Left Right).
static const V d_wide = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x02,
    0x05, 0x09, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x19, 0x01, 0x29, 0x04, 0x81, 0x02,
    0x95, 0x02, 0x81, 0x01,
    0x95, 0x04, 0x19, 0x07, 0x29, 0x0A, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x90, 0x09, 0x91, 0x09, 0x93, 0x09, 0x92, 0x95, 0x04, 0x81, 0x02,
    0xC0 };

static const V d_mouse = { 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
                           0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
                           0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0 };
static const V d_kbd = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
                         0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0 };
// a one-button joystick with X/Y: not a pad for a console core (needs two buttons)
static const V d_one = { 0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
                         0x75, 0x07, 0x95, 0x01, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0xC0 };

static HidPadLayout parse(const V &d, bool *ok = 0)
{
    HidPadLayout l;
    bool r = hidpad_parse(d.data(), (int)d.size(), &l);
    if (ok) *ok = r;
    return l;
}
static PadRaw dec(const HidPadLayout &l, const V &r, bool *ok = 0)
{
    PadRaw p = { 0xDEAD, 0xEE };
    bool k = hidpad_decode(l, r.data(), (int)r.size(), &p);
    if (ok) *ok = k;
    return p;
}

int main()
{
    // ---- d_hat: the shape of a D-input pad
    {
        bool ok;
        HidPadLayout l = parse(d_hat, &ok);
        CHECK(ok);
        CHECK(!l.has_ids);
        CHECK_EQ(l.nruns, 1);
        CHECK_EQ(l.runs[0].count, 14);
        CHECK_EQ(l.runs[0].bit, 0);
        CHECK_EQ(l.runs[0].first, 1);
        CHECK(l.hat.ok);
        CHECK_EQ(l.hat.bit, 16);
        CHECK_EQ(l.hat.lmax, 7);
        CHECK(l.x.ok && l.y.ok);
        CHECK_EQ(l.x.bit, 24);
        CHECK_EQ(l.y.bit, 32);
        CHECK_EQ(l.bits, 64);
        // nothing pressed: hat null (15), sticks centred
        V rep = { 0, 0, 0x0F, 0x80, 0x80, 0x80, 0x80, 0 };
        PadRaw p = dec(l, rep, &ok);
        CHECK(ok);
        CHECK_EQ(p.buttons, 0);
        CHECK_EQ(p.dpad, 0);
        // Button 1 and 4, Button 14
        rep = { 0x09, 0x20, 0x0F, 0x80, 0x80, 0x80, 0x80, 0 };
        p = dec(l, rep);
        CHECK_EQ(p.buttons, 0x2009);
        // every hat position
        static const uint8_t want[8] = { HIDPAD_DPAD_UP, HIDPAD_DPAD_UP | HIDPAD_DPAD_RIGHT, HIDPAD_DPAD_RIGHT, HIDPAD_DPAD_DOWN | HIDPAD_DPAD_RIGHT,
                                         HIDPAD_DPAD_DOWN, HIDPAD_DPAD_DOWN | HIDPAD_DPAD_LEFT, HIDPAD_DPAD_LEFT, HIDPAD_DPAD_UP | HIDPAD_DPAD_LEFT };
        for (int h = 0; h < 8; h++) {
            rep = { 0, 0, (uint8_t)h, 0x80, 0x80, 0x80, 0x80, 0 };
            CHECK_EQ(dec(l, rep).dpad, want[h]);
        }
        // the analog stick is a D-pad with a dead zone (Q2): full left, full down, a small push stays released, the edge of the zone
        rep = { 0, 0, 0x0F, 0x00, 0x80, 0x80, 0x80, 0 };
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_LEFT);
        rep = { 0, 0, 0x0F, 0x80, 0xFF, 0x80, 0x80, 0 };
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_DOWN);
        rep = { 0, 0, 0x0F, 0xFF, 0x00, 0x80, 0x80, 0 };
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_RIGHT | HIDPAD_DPAD_UP);
        rep = { 0, 0, 0x0F, 0x80 + 20, 0x80 - 20, 0x80, 0x80, 0 };
        CHECK_EQ(dec(l, rep).dpad, 0);                                  // 20 of 127 is a small push
        rep = { 0, 0, 0x0F, 0x80 + 60, 0x80 - 60, 0x80, 0x80, 0 };
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_RIGHT | HIDPAD_DPAD_UP);  // 60 of 127 is past 40 %
        // the right stick (Z, Rz) is no D-pad
        rep = { 0, 0, 0x0F, 0x80, 0x80, 0x00, 0xFF, 0 };
        CHECK_EQ(dec(l, rep).dpad, 0);
        // hat and stick together
        rep = { 0, 0, 0x00, 0x00, 0x80, 0x80, 0x80, 0 };
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_UP | HIDPAD_DPAD_LEFT);
        // a short report is refused and *out stays
        V sh = { 0x01, 0x00 };
        bool k2 = true;
        PadRaw q = dec(l, sh, &k2);
        CHECK(!k2);
        CHECK_EQ(q.buttons, 0xDEAD);
    }
    // ---- d_clone: the D-pad is X / Y at 0, 127, 255; the buttons start at bit 44
    {
        bool ok;
        HidPadLayout l = parse(d_clone, &ok);
        CHECK(ok);
        CHECK_EQ(l.nruns, 1);
        CHECK_EQ(l.runs[0].count, 12);
        CHECK_EQ(l.runs[0].bit, 44);
        CHECK_EQ(l.hat.bit, 40);
        CHECK_EQ(l.x.bit, 0);
        CHECK_EQ(l.y.bit, 8);
        // X = 0 (left), Y = 255 (down); buttons 2 and 12: bit 45 is byte 5 bit 5, bit 55 is byte 6 bit 7
        V rep = { 0x00, 0xFF, 0x7F, 0x7F, 0x7F, 0x2F | 0x00, 0x80, 0 };
        rep[5] = 0x0F | 0x20;                                           // hat nibble 0xF (null), Button 2 = bit 45
        PadRaw p = dec(l, rep);
        CHECK_EQ(p.dpad, HIDPAD_DPAD_LEFT | HIDPAD_DPAD_DOWN);
        CHECK_EQ(p.buttons, (1u << 1) | (1u << 11));
        rep = { 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x0F, 0x00, 0 };
        p = dec(l, rep);
        CHECK_EQ(p.dpad, 0);
        CHECK_EQ(p.buttons, 0);
        rep = { 0xFF, 0x00, 0x7F, 0x7F, 0x7F, 0x0F, 0x00, 0 };
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_RIGHT | HIDPAD_DPAD_UP);
    }
    // ---- d_ids: report IDs, signed axes, a 1..8 hat with 0 as null
    {
        bool ok;
        HidPadLayout l = parse(d_ids, &ok);
        CHECK(ok);
        CHECK(l.has_ids);
        CHECK_EQ(l.report_id, 3);
        CHECK_EQ(l.x.bit, 8);
        CHECK(l.x.sign);
        CHECK_EQ(l.hat.lmin, 1);
        // the vendor report of ID 1 is not ours
        V other = { 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        bool k = true;
        dec(l, other, &k);
        CHECK(!k);
        V rep = { 0x03, 0x81, 0x00, 0x00, 0x00 };                       // Button 1 and 8; hat 0 (null); axes 0
        PadRaw p = dec(l, rep, &k);
        CHECK(k);
        CHECK_EQ(p.buttons, 0x81);
        CHECK_EQ(p.dpad, 0);
        rep = { 0x03, 0x00, (uint8_t)(int8_t)-100, 20, 0x01 };           // X = -100 (left), Y = +20 (small), hat 1 = up
        p = dec(l, rep);
        CHECK_EQ(p.dpad, HIDPAD_DPAD_LEFT | HIDPAD_DPAD_UP);
        rep = { 0x03, 0x00, 0, (uint8_t)(int8_t)120, 0x05 };            // Y = +120 (down), hat 5 = down
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_DOWN);
        rep = { 0x03, 0x00, 0, 0, 0x03 };                               // hat 3 = right
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_RIGHT);
    }
    // ---- d_wide: 16 bit axes, two button runs, D-pad usages
    {
        bool ok;
        HidPadLayout l = parse(d_wide, &ok);
        CHECK(ok);
        CHECK_EQ(l.nruns, 2);
        CHECK_EQ(l.runs[0].first, 1);
        CHECK_EQ(l.runs[1].first, 7);
        CHECK_EQ(l.runs[1].bit, 38);
        CHECK(l.dp[0].ok && l.dp[1].ok && l.dp[2].ok && l.dp[3].ok);
        // X = -30000, Y = 0, Button 2, Button 8 (second run, second button), D-pad right (bit 46 + 3 = ...)
        // bit layout: X 0-15, Y 16-31, Buttons 32-35, 2 pad bits 36-37, Buttons 7-10 at 38-41, D-pad up 42, down 43, left 44, right 45
        V rep(6, 0);
        rep[0] = 0xD0; rep[1] = 0x8A;                                   // -30000 = 0x8AD0
        rep[4] = 0x02;                                                  // Button 2 = bit 33
        rep[4] |= 0x80;                                                 // bit 39: second run's second button = Button 8
        rep[5] = 0x20 | 0x04;                                           // bit 45 right, bit 42 up
        PadRaw p = dec(l, rep);
        CHECK_EQ(p.buttons, (1u << 1) | (1u << 7));
        CHECK_EQ(p.dpad, HIDPAD_DPAD_LEFT | HIDPAD_DPAD_RIGHT | HIDPAD_DPAD_UP);
        rep[0] = rep[1] = 0; rep[4] = 0; rep[5] = 0x10;                 // bit 44: D-pad left
        CHECK_EQ(dec(l, rep).dpad, HIDPAD_DPAD_LEFT);
    }
    // ---- what is no pad
    {
        bool ok;
        parse(d_mouse, &ok); CHECK(!ok);
        parse(d_kbd, &ok); CHECK(!ok);
        parse(d_one, &ok); CHECK(!ok);
        V empty;
        parse(empty, &ok); CHECK(!ok);
        V junk = { 0xA1, 0x01, 0xC0, 0xFE, 0x10 };                      // truncated long item
        parse(junk, &ok); CHECK(!ok);
        // a descriptor cut off in the middle of an item is taken as far as it goes and then refused
        V cut(d_hat.begin(), d_hat.begin() + 20);
        parse(cut, &ok); CHECK(!ok);
    }
    DONE("hidpad");
}
