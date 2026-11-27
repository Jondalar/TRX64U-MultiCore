// T156 P1/P2/P3 (application side): the pad service against the real NES and SNES descriptors and a model of their register blocks (FakeIo): plug
// order, the default map (D3), the register words, the write only when the word changes, the unplug (S5), a custom map and its text, the .cfg round trip
// ("Pad N map", "Pad map"), the capture the mapping wizard uses, a core without pad support. The descriptors are those of hidpad.cc's test (a Switch-Pro-
// like D-input report and a clone pad with X/Y as the D-pad); the 8BitDo SN30 Pro is NOT among them: its descriptor and button numbering are not known
// yet, so the default map's Button numbers (pad.h PAD_D3_*) stay an assumption.
#include <vector>
#include <fstream>
#include <iterator>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#if   defined(TEST_APP_SNES)             // the SNES's app (run.sh snesapp, the unit snes_pad) has its own core only
#include "snes_regs.h"
extern const CoreDesc snes_desc;         // cores.h lists the shared app's cores; the SNES is its own app (cores/snes/app/lib)
#else
#error "test_pad runs on a core's pad: build it as a core's app (run.sh <core>app, TEST_APP_<CORE>); the shared app has no core of its own left (T239.3 D61)"
#endif
#include "pad.h"
#include "ops_glue.h"
#include "descrec.h"

typedef std::vector<uint8_t> V;

static const V d_hat = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x81, 0x02,
    0x95, 0x02, 0x81, 0x01,
    0x05, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x75, 0x04, 0x95, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x95, 0x01, 0x81, 0x01,
    0x26, 0xFF, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,
    0xC0 };
static const V d_mouse = { 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
                           0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
                           0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0 };

// a report of d_hat: buttons (bit n = Button n+1), hat 0..7 or 15, X/Y centred
static V rep(uint16_t buttons, uint8_t hat = 15, uint8_t x = 0x80, uint8_t y = 0x80)
{
    return V{ (uint8_t)buttons, (uint8_t)(buttons >> 8), hat, x, y, 0x80, 0x80, 0 };
}

struct Rig {
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    Settings *st;
    explicit Rig(const CoreDesc *d, const char *cfg = "/sd/cores/x/x.cfg")
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        st->load();
        c.settings = st;
    }
    uint16_t word(uint32_t reg) { return (uint16_t)(io.regs[reg] | io.regs[reg + 1] << 8); }
    int writes(uint32_t reg) { int n = 0; for (auto &w : io.log) n += (w.reg == reg); return n; }
};

static int k1, k2, k3;
// the default map of a spec for a pad that is not in the table (the generic numbering)
static const uint8_t *gen_map(const PadSpec *sp)
{
    static uint8_t m[2][16];
    static int n;
    pad_default_map(sp, 0, 0, m[n & 1]);
    return m[n++ & 1];
}                                   // three interfaces' identities

static void unplug_all() { pad_detach(&k1); pad_detach(&k2); pad_detach(&k3); }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    // ---- plug order, the placement of a third pad, a mouse is no pad
    {
        Rig r(&snes_desc);
        pad_configure(snes_desc.pad);
        CHECK(!pad_attach(&k3, "mouse", 1, 2, d_mouse.data(), (int)d_mouse.size()));
        CHECK_EQ(pad_player_of(&k3), -1);
        CHECK(pad_attach(&k1, "first", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(pad_attach(&k2, "second", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK_EQ(pad_player_of(&k1), 0);
        CHECK_EQ(pad_player_of(&k2), 1);
        CHECK(!pad_attach(&k3, "third", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));       // two pads in use
        CHECK_STR(pad_name(0), "first");
        CHECK_STR(pad_name(1), "second");
        pad_detach(&k1);                                                                          // player 1 leaves; the next pad takes the free place
        CHECK_EQ(pad_player_of(&k2), 1);
        CHECK(pad_attach(&k3, "third", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK_EQ(pad_player_of(&k3), 0);
        unplug_all();
        CHECK_STR(pad_name(0), "");
    }
    // ---- SNES: default map (D3), the register pair, written only on change
    {
        Rig r(&snes_desc);
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(pad_pace(&r.c) == true);                                     // the first pace writes both players (zero)
        CHECK_EQ(r.writes(SNES_PAD1), 1);
        CHECK_EQ(r.writes(SNES_PAD1 + 1), 1);
        CHECK_EQ(r.writes(SNES_PAD2), 1);
        CHECK(pad_pace(&r.c) == false);                                    // nothing changed: nothing written
        CHECK_EQ(r.writes(SNES_PAD1), 1);
        // D3: Button 1 (bottom) is SNES B, 2 (right) A, 3 (left) Y, 4 (top) X, 5 L, 6 R, 7 Select, 8 Start; serial bit order B Y Select Start Up Down Left Right A X L R
        struct { int button; int bit; const char *name; } map[] = { { 1, 0, "B" }, { 3, 1, "Y" }, { 7, 2, "Select" }, { 8, 3, "Start" }, { 2, 8, "A" }, { 4, 9, "X" }, { 5, 10, "L" }, { 6, 11, "R" } };
        for (auto &m : map) {
            V p = rep((uint16_t)(1u << (m.button - 1)));
            pad_report(&k1, p.data(), (int)p.size());
            pad_pace(&r.c);
            CHECK_EQ(r.word(SNES_PAD1), 1u << m.bit);
            if (r.word(SNES_PAD1) != (1u << m.bit)) printf("     %s: register %04x\n", m.name, r.word(SNES_PAD1));
            CHECK_EQ(r.word(SNES_PAD2), 0);                                // player 2 has no pad
        }
        // the D-pad: hat up/down/left/right, and the stick
        static const uint8_t hat[4] = { 0, 4, 6, 2 };                     // up, down, left, right
        for (int d = 0; d < 4; d++) {
            V p = rep(0, hat[d]);
            pad_report(&k1, p.data(), (int)p.size());
            pad_pace(&r.c);
            CHECK_EQ(r.word(SNES_PAD1), 1u << (4 + d));                   // Up Down Left Right are serial bits 4..7
        }
        V p = rep(0, 15, 0x00, 0x00);                                      // stick up-left
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), (1u << 4) | (1u << 6));
        // the pair: every change writes the low byte first, the high byte last (the gateware's commit)
        p = rep(0);
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        p = rep(0x0400);                                                   // Button 11: not in the map, the word does not change
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(!pad_pace(&r.c));
        p = rep(0x0020, 15);                                               // Button 6 = R: bit 11 (high byte)
        pad_report(&k1, p.data(), (int)p.size());
        size_t n0 = r.io.log.size();
        pad_pace(&r.c);
        CHECK_EQ(r.io.log.size() - n0, 2);
        CHECK_EQ(r.io.log[n0].reg, SNES_PAD1);
        CHECK_EQ(r.io.log[n0 + 1].reg, SNES_PAD1 + 1);
        // S5: the pad is pulled while a button is held: the word goes to zero at the next pace
        CHECK_EQ(r.word(SNES_PAD1), 1u << 11);
        pad_detach(&k1);
        CHECK(pad_pace(&r.c));
        CHECK_EQ(r.word(SNES_PAD1), 0);
        // and a pad that comes back starts released
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), 0);
        // the second player has its own register
        CHECK(pad_attach(&k2, "pad2", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        p = rep(0x0002);
        pad_report(&k2, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD2), 1u << 8);
        CHECK_EQ(r.word(SNES_PAD1), 0);
        // a report of an interface the service does not know is dropped
        p = rep(0xFFFF);
        pad_report(&k3, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), 0);
        unplug_all();
    }
    // ---- the numbering of a pad: the 8BitDo SN30 Pro (read on the board) has its own entry, every other pad the generic one (T156, board 2026-10-29)
    {
        CHECK_STR(pad_numbering_for(0x2DC8, 0x6001)->name, "8BitDo SN30 Pro");
        CHECK_STR(pad_numbering_for(0x2DC8, 0x9999)->name, "generic");          // same maker, another product
        CHECK_STR(pad_numbering_for(0x1234, 0x6001)->name, "generic");
        // SNES: the map the wizard recorded on the board, 01040A0B2021222300030607, is this pad's default
        uint8_t m[12];
        pad_default_map(snes_desc.pad, 0x2DC8, 0x6001, m);
        char t[40];
        CHECK(pad_map_to_text(m, 12, t, sizeof(t)));
        CHECK_STR(t, "01040A0B2021222300030607");
        pad_default_map(snes_desc.pad, 0x1234, 0x0001, m);
        CHECK(pad_map_to_text(m, 12, t, sizeof(t)));
        CHECK_STR(t, "000206072021222301030405");
        Rig r(&snes_desc);
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "8Bitdo SN30 Pro", 0x2DC8, 0x6001, d_hat.data(), (int)d_hat.size()));
        CHECK(pad_attach(&k2, "other", 0x2DC8, 0x9999, d_hat.data(), (int)d_hat.size()));
        // Button 2 is the bottom button of the SN30: SNES B (bit 0); on the other pad Button 2 is the right one: SNES A (bit 8)
        V p = rep(0x0002);
        pad_report(&k1, p.data(), (int)p.size());
        pad_report(&k2, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), 1u << 0);
        CHECK_EQ(r.word(SNES_PAD2), 1u << 8);
        p = rep(0x0400 | 0x0800);                                              // Buttons 11 and 12 of the SN30: Select and Start
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), (1u << 2) | (1u << 3));
        // the NES takes A and B from the same positions: its unit nes_pad
        unplug_all();
    }
    // ---- the descriptor is kept (REST) and printed once more long after the plug
    {
        Rig r(&snes_desc);
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "SN30", 0x2DC8, 0x6001, d_hat.data(), (int)d_hat.size()));
        int len = 0;
        const uint8_t *d = pad_descriptor(0, &len);
        CHECK(d != 0);
        CHECK_EQ(len, (int)d_hat.size());
        CHECK(d && !memcmp(d, d_hat.data(), d_hat.size()));
        CHECK(pad_descriptor(1, &len) == 0);
        CHECK_EQ(len, 0);
        std::string st = ops_state(&r.c, "pad.1.descriptor");
        CHECK(st.find("SN30 2DC8:6001 83 bytes: 05 01 09 05 A1 01") == 0);
        CHECK(st.size() > 83 * 3);
        CHECK_STR(ops_state(&r.c, "pad.2.descriptor"), "-");
        CHECK_EQ(pad_desc_dumps(0), 1);
        r.io.t = 1000;
        pad_pace(&r.c);                                                        // the first pace that sees the pad
        r.io.t = 1000 + 14000;
        pad_pace(&r.c);
        CHECK_EQ(pad_desc_dumps(0), 1);                                        // not yet
        r.io.t = 1000 + 15000;
        pad_pace(&r.c);
        CHECK_EQ(pad_desc_dumps(0), 2);                                        // once more
        r.io.t = 1000 + 60000;
        pad_pace(&r.c);
        CHECK_EQ(pad_desc_dumps(0), 2);                                        // and no more
        unplug_all();
    }
    // ---- the map text: two hex digits per button; what does not fit the core is refused
    {
        uint8_t m[16];
        char t[64];
        uint8_t src[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 32, 33, 34, 0xFF };
        CHECK(pad_map_to_text(src, 12, t, sizeof(t)));
        CHECK_STR(t, "0001020304050607202122FF");
        CHECK(pad_map_from_text(t, 12, m));
        CHECK(!memcmp(m, src, 12));
        uint8_t keep[12];
        memset(m, 0x55, sizeof(m)); memcpy(keep, m, 12);
        CHECK(!pad_map_from_text("0001", 12, m));                          // wrong length
        CHECK(!pad_map_from_text("00010203040506072021222G", 12, m));      // not hex
        CHECK(!pad_map_from_text("000102030405060720212224", 12, m));      // source 36 does not exist
        CHECK(!memcmp(m, keep, 12));                                       // a refused text changes nothing
        CHECK(!pad_map_to_text(src, 12, t, 20));                           // no room
    }
    // ---- the custom map: mode, active map, the register word under it; .cfg round trip
    {
        Rig r(&snes_desc, "/sd/cores/snes/snes.cfg");
        pad_configure(snes_desc.pad);
        pad_load_maps(r.st);
        CHECK_EQ(pad_mode(), PAD_MODE_DEFAULT);
        CHECK(!pad_custom_valid(0));
        CHECK(!memcmp(pad_active_map(0), gen_map(snes_desc.pad), 12));
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        // a map where SNES B is Button 9 and SNES A is Button 10, everything else as the default
        uint8_t cm[12];
        memcpy(cm, gen_map(snes_desc.pad), 12);
        cm[0] = 8; cm[8] = 9;
        CHECK(pad_set_custom(0, cm));
        CHECK(pad_custom_valid(0));
        V p = rep(0x0100);                                                 // Button 9
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), 0);                                    // the default map is in force: Button 9 is not mapped
        pad_set_mode(PAD_MODE_CUSTOM);
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), 1u << 0);                              // now it is SNES B
        p = rep(0x0200);
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(SNES_PAD1), 1u << 8);                              // Button 10 is SNES A
        // player 2 has no custom map: it keeps the default while the mode is Custom
        CHECK(!memcmp(pad_active_map(1), gen_map(snes_desc.pad), 12));
        // save: the file holds "Pad 1 map=" and nothing else of the pad; the setting "Pad map" goes with it
        CHECK(pad_save_map(r.st, 0));
        int mi = r.st->find("Pad map");
        CHECK(mi >= 0);
        CHECK(r.st->set(mi, PAD_MODE_CUSTOM));
        std::string text(r.fs.files["/sd/cores/snes/snes.cfg"].begin(), r.fs.files["/sd/cores/snes/snes.cfg"].end());
        CHECK(text.find("Pad 1 map=080206072021222309030405") != std::string::npos);
        CHECK(text.find("Pad map=Custom") != std::string::npos);
        CHECK(text.find("Pad 2 map") == std::string::npos);
        // a new start: settings from the file, the service gets map and mode from them
        Settings st2(snes_desc.settings, snes_desc.nsettings, snes_desc.store, &r.fs, "/sd/cores/snes/snes.cfg", 0);
        st2.load();
        pad_configure(snes_desc.pad);
        CHECK_EQ(pad_mode(), PAD_MODE_DEFAULT);
        pad_load_maps(&st2);
        CHECK_EQ(pad_mode(), PAD_MODE_CUSTOM);
        CHECK(pad_custom_valid(0));
        CHECK(!pad_custom_valid(1));
        CHECK(!memcmp(pad_active_map(0), cm, 12));
        // another setting written later keeps the map in the file
        int vi = st2.find("Video standard");
        CHECK(st2.set(vi, 2));
        std::string t2(r.fs.files["/sd/cores/snes/snes.cfg"].begin(), r.fs.files["/sd/cores/snes/snes.cfg"].end());
        CHECK(t2.find("Pad 1 map=") != std::string::npos);
        CHECK(t2.find("Video standard=PAL") != std::string::npos);
        // a damaged map in the file is not taken: the default stays
        r.fs.put_text("/sd/cores/snes/snes.cfg", "[SNES Settings]\nPad map=Custom\nPad 1 map=0001\n\n");
        Settings st3(snes_desc.settings, snes_desc.nsettings, snes_desc.store, &r.fs, "/sd/cores/snes/snes.cfg", 0);
        st3.load();
        pad_configure(snes_desc.pad);
        pad_load_maps(&st3);
        CHECK(!pad_custom_valid(0));
        CHECK(!memcmp(pad_active_map(0), gen_map(snes_desc.pad), 12));
        // "Reset to host defaults" removes the map from the file
        CHECK(st3.reset_to_host());
        char v[80];
        CHECK(!st3.extra_get("Pad 1 map", v, sizeof(v)));
        unplug_all();
    }
    // ---- the operation of the setting and the states
    {
        Rig r(&snes_desc);
        pad_configure(snes_desc.pad);
        OpResult res;
        OpIn in = in_int("value", 1);
        CHECK_EQ(ops_call(&r.c, "pad.set_map", &in, 1, &res), 0);
        CHECK_EQ(pad_mode(), PAD_MODE_CUSTOM);
        in = in_int("value", 0);
        CHECK_EQ(ops_call(&r.c, "pad.set_map", &in, 1, &res), 0);
        CHECK_EQ(pad_mode(), PAD_MODE_DEFAULT);
        CHECK_STR(ops_state(&r.c, "pad.1"), "-");
        CHECK(pad_attach(&k1, "SN30", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        V p = rep(0x0003);
        pad_report(&k1, p.data(), (int)p.size());
        CHECK_STR(ops_state(&r.c, "pad.1"), "SN30 buttons=00000003 dpad=0");
        unplug_all();
    }
    // ---- the capture of the mapping wizard: waits for the release, reports the first new press, sees the D-pad
    {
        Rig r(&snes_desc);
        pad_configure(snes_desc.pad);
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        V p = rep(0x0004);                                                 // Button 3 is held when the capture starts
        pad_report(&k1, p.data(), (int)p.size());
        pad_capture_start(0);
        uint8_t src = 0xEE;
        CHECK(pad_capturing());
        CHECK(!pad_capture_poll(&src));
        p = rep(0x0004);                                                   // still held: not a press
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(!pad_capture_poll(&src));
        p = rep(0x0004 | 0x0008);                                          // another button joins while the first is still held: the pad was not let go
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(!pad_capture_poll(&src));
        p = rep(0);                                                        // released
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(!pad_capture_poll(&src));
        p = rep(0x0020 | 0x0008);                                          // Buttons 6 and 4 together: the lowest source wins
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(pad_capture_poll(&src));
        CHECK_EQ(src, 3);
        CHECK(!pad_capturing());
        CHECK(!pad_capture_poll(&src));                                    // taken once
        // a capture started on a released pad takes the next press at once, a D-pad direction is a source too
        p = rep(0);
        pad_report(&k1, p.data(), (int)p.size());
        pad_capture_start(0);
        p = rep(0, 6);                                                     // hat left
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(pad_capture_poll(&src));
        CHECK_EQ(src, PAD_SRC_LEFT);
        // the other player's pad does not feed it
        CHECK(pad_attach(&k2, "pad2", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        p = rep(0);
        pad_report(&k1, p.data(), (int)p.size());
        pad_capture_start(0);
        p = rep(0x0001);
        pad_report(&k2, p.data(), (int)p.size());
        CHECK(!pad_capture_poll(&src));
        pad_capture_stop();
        CHECK(!pad_capturing());
        char nm[24];
        CHECK_STR(pad_src_name(0, nm, sizeof(nm)), "Button 1");
        CHECK_STR(pad_src_name(PAD_SRC_DOWN, nm, sizeof(nm)), "Down");
        CHECK_STR(pad_src_name(PAD_SRC_NONE, nm, sizeof(nm)), "none");
        unplug_all();
    }
    DONE("pad");
}
