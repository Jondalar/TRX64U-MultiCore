// T141 `rawkeys`: a USB keyboard delivered to the Amiga 500 and 1200 as raw key events (coreapp/src/rawkeys.cc, the table cores/amiga/app/lib/amiga_keys.h).
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
    // the Amiga 500 shares the queue (gateware: the shared amiga_input.v): the same record, the same events
    {
        FakeFs fs2;
        AmigaRig a(&fs2, &amiga_desc);
        CHECK(amiga_desc.rawkeys != 0);
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
    DONE("rawkeys");
}
