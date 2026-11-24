// T141 `rawkeys`: a USB keyboard delivered to the Amiga 500 and 1200 as raw key events (coreapp/src/rawkeys.cc, the table coreapp/cores/amiga_keys.h).
// S3: every key the Amiga has comes out as its raw code, the release once with bit 7. S4: each key once, the menu keys never reach the machine,
// while the menu is open USB keys go nowhere, and the register's room is respected. The expected codes below are written out from the Amiga
// Hardware Reference Manual's keyboard table, not read from amiga_keys.h.
#include "check.h"
#include <algorithm>
#include <initializer_list>
#include "amiga_model.h"
#include "engines.h"
#include "rawkeys.h"

struct Exp { uint8_t usage; uint8_t code; const char *name; };
static const Exp expect[] = {
    { 0x04, 0x20, "A" }, { 0x05, 0x35, "B" }, { 0x06, 0x33, "C" }, { 0x07, 0x22, "D" }, { 0x08, 0x12, "E" }, { 0x09, 0x23, "F" },
    { 0x0A, 0x24, "G" }, { 0x0B, 0x25, "H" }, { 0x0C, 0x17, "I" }, { 0x0D, 0x26, "J" }, { 0x0E, 0x27, "K" }, { 0x0F, 0x28, "L" },
    { 0x10, 0x37, "M" }, { 0x11, 0x36, "N" }, { 0x12, 0x18, "O" }, { 0x13, 0x19, "P" }, { 0x14, 0x10, "Q" }, { 0x15, 0x13, "R" },
    { 0x16, 0x21, "S" }, { 0x17, 0x14, "T" }, { 0x18, 0x16, "U" }, { 0x19, 0x34, "V" }, { 0x1A, 0x11, "W" }, { 0x1B, 0x32, "X" },
    { 0x1C, 0x15, "Y" }, { 0x1D, 0x31, "Z" },
    { 0x1E, 0x01, "1" }, { 0x1F, 0x02, "2" }, { 0x20, 0x03, "3" }, { 0x21, 0x04, "4" }, { 0x22, 0x05, "5" }, { 0x23, 0x06, "6" },
    { 0x24, 0x07, "7" }, { 0x25, 0x08, "8" }, { 0x26, 0x09, "9" }, { 0x27, 0x0A, "0" },
    { 0x28, 0x44, "RETURN" }, { 0x29, 0x45, "ESC" }, { 0x2A, 0x41, "BACKSPACE" }, { 0x2B, 0x42, "TAB" }, { 0x2C, 0x40, "SPACE" },
    { 0x2D, 0x0B, "-" }, { 0x2E, 0x0C, "=" }, { 0x2F, 0x1A, "[" }, { 0x30, 0x1B, "]" }, { 0x31, 0x0D, "\\" }, { 0x32, 0x2B, "non-US #" },
    { 0x33, 0x29, ";" }, { 0x34, 0x2A, "'" }, { 0x35, 0x00, "`" }, { 0x36, 0x38, "," }, { 0x37, 0x39, "." }, { 0x38, 0x3A, "/" },
    { 0x43, 0x59, "F10" }, { 0x3A, 0x50, "F1" }, { 0x3B, 0x51, "F2" }, { 0x3C, 0x52, "F3" }, { 0x3D, 0x53, "F4" }, { 0x3E, 0x54, "F5" }, { 0x3F, 0x55, "F6" },
    { 0x40, 0x56, "F7" }, { 0x41, 0x57, "F8" }, { 0x42, 0x58, "F9" },
    { 0x4B, 0x5F, "PAGE UP = HELP" }, { 0x4C, 0x46, "DELETE = DEL" },
    { 0x4F, 0x4E, "RIGHT" }, { 0x50, 0x4F, "LEFT" }, { 0x51, 0x4D, "DOWN" }, { 0x52, 0x4C, "UP" },
    { 0x53, 0x5A, "NUMLOCK = keypad (" }, { 0x54, 0x5C, "keypad /" }, { 0x55, 0x5D, "keypad *" }, { 0x56, 0x4A, "keypad -" },
    { 0x57, 0x5E, "keypad +" }, { 0x58, 0x43, "keypad ENTER" },
    { 0x59, 0x1D, "keypad 1" }, { 0x5A, 0x1E, "keypad 2" }, { 0x5B, 0x1F, "keypad 3" }, { 0x5C, 0x2D, "keypad 4" }, { 0x5D, 0x2E, "keypad 5" },
    { 0x5E, 0x2F, "keypad 6" }, { 0x5F, 0x3D, "keypad 7" }, { 0x60, 0x3E, "keypad 8" }, { 0x61, 0x3F, "keypad 9" }, { 0x62, 0x0F, "keypad 0" },
    { 0x63, 0x3C, "keypad ." }, { 0x64, 0x30, "non-US \\" }, { 0x67, 0x5B, "keypad = = )" },
};
// the usages with no Amiga key (F11 F12 PRTSC PAUSE INSERT HOME END PAGEDOWN APPLICATION) and the menu keys (F10, SCROLL LOCK)
static const uint8_t none_usage[] = { 0x44, 0x45, 0x46, 0x48, 0x49, 0x4A, 0x4D, 0x4E, 0x65, 0x66, 0x68, 0x70, 0xE8 };
static const uint8_t menu_usage[] = { 0x47 };        // F10 is the machine's (T141 D5)

static void report(uint8_t mods, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0, uint8_t e = 0, uint8_t f = 0)
{
    uint8_t r[8] = { mods, 0, a, b, c, d, e, f };
    rawkeys_usb_report(r);
}

// what the application wrote since `from`
static std::vector<uint8_t> since(const AmigaIo &io, size_t from)
{
    if (from > io.rawkeys.size()) {
        from = io.rawkeys.size();                       // a broken product wrote less than the test expects: the checks go red, no crash
    }
    return std::vector<uint8_t>(io.rawkeys.begin() + from, io.rawkeys.end());
}

static bool same(const std::vector<uint8_t> &v, std::initializer_list<uint8_t> l)
{
    return v.size() == l.size() && std::equal(v.begin(), v.end(), l.begin());
}

// the core's poll for 200 ms (the model's queue drains meanwhile)
static void settle(AmigaRig &r)
{
    for (int i = 0; i < 40; i++) {
        r.io.delay_ms(5);
        engines_poll(&r.c);                              // the core's poll (media.poll), which calls rawkeys_pace
    }
}

int main()
{
    FakeFs fs;
    AmigaRig r(&fs, &amiga1200_desc);
    engines_of(&r.c);                                    // the record is made known
    AmigaIo &io = r.io;

    // S3: every key of the table: its code on the press, the code | 0x80 on the release, one event each
    for (const Exp &e : expect) {
        size_t n0 = io.rawkeys.size();
        report(0, e.usage);
        settle(r);
        std::vector<uint8_t> p = since(io, n0);
        if (!same(p, { e.code })) printf("  press %s (usage %02X): expected %02X\n", e.name, e.usage, e.code);
        CHECK(same(p, { e.code }));
        n0 = io.rawkeys.size();
        report(0);
        settle(r);
        std::vector<uint8_t> q = since(io, n0);
        if (!same(q, { (uint8_t)(e.code | 0x80) })) printf("  release %s (usage %02X): expected %02X\n", e.name, e.usage, e.code | 0x80);
        CHECK(same(q, { (uint8_t)(e.code | 0x80) }));
    }
    // the table is complete: ESC, TAB, F1-F10, DEL, HELP, the cursor keys, the keypad (with ENTER) are all in it (F10 since T141 D5)
    CHECK_EQ(sizeof(expect) / sizeof(expect[0]), 88u);     // 26 letters, 10 digits, the punctuation, RETURN ESC BACKSPACE TAB SPACE, F1-F10, HELP, DEL, cursors, keypad

    // keys the Amiga has not got, and the menu keys: nothing is sent
    for (uint8_t u : none_usage) {
        size_t n0 = io.rawkeys.size();
        report(0, u);
        settle(r);
        report(0);
        settle(r);
        CHECK_EQ(io.rawkeys.size(), n0);
    }
    for (uint8_t u : menu_usage) {
        size_t n0 = io.rawkeys.size();
        report(0, u);
        settle(r);
        report(0);
        settle(r);
        CHECK_EQ(io.rawkeys.size(), n0);
    }

    // the modifiers: SHIFT, CTRL, ALT and the Amiga keys, left and right
    {
        static const uint8_t mod_code[8] = { 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 };    // L CTRL, L SHIFT, L ALT, L GUI, R CTRL, R SHIFT, R ALT, R GUI
        for (int b = 0; b < 8; b++) {
            size_t n0 = io.rawkeys.size();
            report((uint8_t)(1 << b));
            settle(r);
            report(0);
            settle(r);
            CHECK(same(since(io, n0), { mod_code[b], (uint8_t)(mod_code[b] | 0x80) }));
        }
    }

    // press and release by difference between reports, each key once; an idle report that repeats the keys is nothing
    {
        size_t n0 = io.rawkeys.size();
        report(0, 0x04);                                 // A
        settle(r);
        report(0, 0x04);                                 // the idle report repeats it
        settle(r);
        report(0, 0x04, 0x05);                           // B joins
        settle(r);
        report(0, 0x05);                                 // A comes up
        settle(r);
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x20, 0x35, 0xA0, 0xB5 }));
    }
    // SHIFT and a key in one report: the modifier first; both released: the key first, the modifier after
    {
        size_t n0 = io.rawkeys.size();
        report(0x02, 0x04);
        settle(r);
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x60, 0x20, 0xA0, 0xE0 }));
    }
    // both CTRL keys are the Amiga's one CTRL: pressed once, released when the last one is up
    {
        size_t n0 = io.rawkeys.size();
        report(0x01);
        settle(r);
        report(0x11);
        settle(r);
        report(0x10);
        settle(r);
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x63, 0xE3 }));
    }
    // ErrorRollOver: a report that says nothing changes nothing
    {
        size_t n0 = io.rawkeys.size();
        report(0, 0x04);
        settle(r);
        report(0, 1, 1, 1, 1, 1, 1);
        settle(r);
        report(0, 0x04);                                 // A is still held: not pressed again
        settle(r);
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x20, 0xA0 }));
    }
    // CAPS LOCK is the Amiga's lock key: a press turns it on (62), the next press off (E2); the release of the USB key is nothing
    {
        size_t n0 = io.rawkeys.size();
        report(0, 0x39);
        settle(r);
        report(0);
        settle(r);
        report(0, 0x39);
        settle(r);
        report(0);
        settle(r);
        report(0, 0x39);
        settle(r);
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x62, 0xE2, 0x62 }));
        report(0, 0x39);                                 // back to off for the rest
        settle(r);
        report(0);
        settle(r);
    }

    // the register's room: 14 keys at once (8 modifiers count as 4 codes: both CTRL one, so 6 keys + 7 codes) are written in order and the
    // gateware's queue of 16 never overflows
    {
        size_t n0 = io.rawkeys.size();
        io.rk_max = 0;
        io.rk_lost = 0;
        report(0xFF, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09);
        rawkeys_pace(&r.c);                              // no time passes: at most what fits
        size_t first = io.rawkeys.size() - n0;
        CHECK(first >= 1 && first <= 16);
        settle(r);
        std::vector<uint8_t> p = since(io, n0);
        CHECK(same(p, { 0x63, 0x60, 0x64, 0x66, 0x61, 0x65, 0x67, 0x20, 0x35, 0x33, 0x22, 0x12, 0x23 }));
        CHECK_EQ(io.rk_lost, 0);
        CHECK(io.rk_max <= 16);
        report(0);
        settle(r);
        CHECK_EQ(io.rk_lost, 0);
        CHECK_EQ(io.rawkeys.size() - n0, 26u);           // 13 presses, 13 releases
    }
    // 40 reports in a burst, faster than the machine takes the events: nothing is lost, nothing doubled
    {
        size_t n0 = io.rawkeys.size();
        io.rk_lost = 0;
        for (int i = 0; i < 20; i++) {
            report(0, 0x04);
            report(0);
        }
        settle(r);
        CHECK_EQ(io.rk_lost, 0);
        CHECK_EQ(io.rawkeys.size() - n0, 40u);
    }

    // the menu: what is down is released, the keys typed while it is open go nowhere, a key still held at the close is ignored until it is up
    {
        size_t n0 = io.rawkeys.size();
        report(0x02, 0x04, 0x05);                        // SHIFT, A, B down
        settle(r);
        CHECK(same(since(io, n0), { 0x60, 0x20, 0x35 }));
        n0 = io.rawkeys.size();
        rawkeys_menu(&r.c, true);                        // F10 opened the menu
        CHECK(same(since(io, n0), { 0xA0, 0xB5, 0xE0 }));      // ascending codes: the keys, then the modifier
        report(0x02, 0x04, 0x06);                        // in the menu: SHIFT and A still held, B up, C down (no poll runs while it is open)
        report(0x02, 0x04, 0x06, 0x07);                  // D too
        report(0x02, 0x04);                              // C and D up
        report(0x02, 0x04, 0x08);                        // E typed and held
        CHECK_EQ(io.rawkeys.size(), n0 + 3);             // nothing reached the machine
        rawkeys_menu(&r.c, false);                       // closed with SHIFT, A and E still held
        settle(r);
        CHECK_EQ(io.rawkeys.size(), n0 + 3);
        report(0x02, 0x04, 0x08, 0x09);                  // F pressed after the close: it is typed
        settle(r);
        CHECK(same(since(io, n0 + 3), { 0x23 }));
        report(0x02, 0x08, 0x09);                        // A (held across the menu) comes up: it was never pressed, no release
        settle(r);
        report(0x02, 0x04, 0x08, 0x09);                  // and goes down again: now it is typed
        settle(r);
        CHECK(same(since(io, n0 + 3), { 0x23, 0x20 }));
        report(0);
        settle(r);
        // E was held across the menu and never pressed; its release is nothing; the others are released once
        std::vector<uint8_t> t = since(io, n0 + 3);
        CHECK(same(t, { 0x23, 0x20, 0xA0, 0xA3 }));
        int rel_e = 0;
        for (uint8_t x : t) rel_e += (x == 0x92 || x == 0xE0);   // E and SHIFT were held across the menu and never pressed: no release
        CHECK_EQ(rel_e, 0);
    }
    // the menu opens with nothing down: no event
    {
        size_t n0 = io.rawkeys.size();
        rawkeys_menu(&r.c, true);
        rawkeys_menu(&r.c, false);
        settle(r);
        CHECK_EQ(io.rawkeys.size(), n0);
    }
    // a key typed and released in the menu leaves no trace; the first report after the close is judged against the baseline
    {
        size_t n0 = io.rawkeys.size();
        rawkeys_menu(&r.c, true);
        report(0, 0x04);
        report(0);
        rawkeys_menu(&r.c, false);
        settle(r);
        report(0, 0x05);
        settle(r);
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x35, 0xB5 }));
    }
    // reset: the machine has no key down after a hold; a key held before is pressed again by the next report that holds it
    {
        size_t n0 = io.rawkeys.size();
        report(0, 0x04);
        settle(r);
        rawkeys_reset();
        report(0);
        settle(r);
        CHECK(same(since(io, n0), { 0x20 }));            // the release of a key the reset forgot is not sent
    }

#ifndef TEST_APP_A1200                   // the A500 is in the shared app only
    // the Amiga 500 shares the queue (gateware: the shared amiga_input.v): the same record, the same events
    {
        FakeFs fs2;
        AmigaRig a(&fs2, &amiga_desc);
        CHECK(amiga_desc.rawkeys != 0);
        CHECK(amiga1200_desc.rawkeys != 0);
        rawkeys_configure(0);
        engines_of(&a.c);
        report(0, 0x04);
        settle(a);
        CHECK(same(since(a.io, 0), { 0x20 }));
        report(0);
        settle(a);
        CHECK(same(since(a.io, 0), { 0x20, 0xA0 }));
        bool rd = false;
        for (uint32_t reg : a.io.rdlog) rd = rd || reg == AM_RAWKEY;
        CHECK(rd);
    }
#endif
    DONE("rawkeys");
}
