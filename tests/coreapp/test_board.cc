// T104.2 S2, S4, S11: the board profile is chosen by what the hardware answers (the ICAP byte, the hand-over page), the capabilities of
// the C64 descriptor are struck by the probes, the settings find their file where the profile says, and the menu has no way back to a
// host that is not there. The probe is a fake whose answers a test flips; the descriptor is the real one.
#include <string>
#include "check.h"
#include "fakes.h"
#include "board.h"
#include "start.h"
#include "menuctl.h"
#include "cores.h"
#include "trx64u_menu.h"
#include "settings.h"
#include "handover.h"
#include "romfind.h"
#include "keyboard.h"

const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &c64_desc, 0 };

#define CHECK_TXT(s, b) do { std::string _s = (s); CHECK_STR(_s.c_str(), b); } while (0)

struct FakeProbe : BoardProbe {
    uint8_t icap = 0;
    uint32_t fpga = BOARD_FPGA_DRIVE_1541_1 | BOARD_FPGA_COMMAND_INTF;
    uint8_t caps = BOARD_TRX_CAPS_GATE | BOARD_TRX_CAPS_STATEBUS | BOARD_TRX_CAPS_ENGINE | BOARD_TRX_CAPS_CART;
    uint8_t iec = BOARD_IEC_VERSION;
    bool valid = false;
    int calls = 0;
    uint32_t has = BB_ALL;                                // the blocks of the app's driver table (hw.cc builds it from DRV_HAS_<block>)
    int fpga_reads = 0, caps_reads = 0, iec_reads = 0;    // the reads board_detect() made: a probe whose block is not there is not read
    uint32_t blocks() { return has; }
    uint8_t icap_id() { calls++; return icap; }
    uint32_t fpga_caps() { fpga_reads++; return fpga; }
    uint8_t trx_caps() { caps_reads++; return caps; }
    uint8_t iec_version() { iec_reads++; return iec; }
    bool handover_valid() { return valid; }
};

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "x"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

static int ret_calls;
static bool ret_fails(char *msg, int n) { ret_calls++; snprintf(msg, n, "no ICAP loader in this bitstream"); return false; }

static bool has_entry(const MenuTree &t, EntryKind k)
{
    for (int i = 0; i < t.n; i++) if (t.e[i].kind == k) return true;
    return false;
}

static bool names(const char *const *v, int n, const char *s)
{
    for (int i = 0; i < n; i++) if (!strcmp(v[i], s)) return true;
    return false;
}

int main()
{
    // ---- S2: the monolith. The ICAP loader is not decoded there and reads 0. Since T112 Q1 a valid page is believed: the legacy start of a
    // large .cor writes one (its path), a .cfw start wipes it; a page that fails the check is still no hand-over.
    FakeProbe mono;
    mono.valid = false;
    BoardProfile m = board_detect(&mono);
    CHECK_EQ(m.kind, BOARD_MONOLITH);
    CHECK(!m.has_handover);
    mono.valid = true;
    BoardProfile mh = board_detect(&mono);
    CHECK_EQ(mh.kind, BOARD_MONOLITH);                    // the profile (ROM and settings directories of the flash) stays the monolith's
    CHECK(mh.has_handover);
    CHECK_STR(mh.why, "ICAP absent, hand-over valid");
    CHECK_STR(mh.rom_dir, "/flash/roms");
    CHECK(!m.can_return_host);
    CHECK_STR(board_name(m), "monolith");
    CHECK_STR(m.why, "ICAP absent, no hand-over");
    CHECK_STR(m.cfg_dir, "");                              // T239.3 P1-5: the directory is named by the core's record, board_set_core below
    CHECK_STR(m.boot_cfg, "");
    CHECK_STR(m.rom_dir, "/flash/roms");

    // the MultiCore host: ICAP answers 0x7A, the hand-over is valid
    FakeProbe mc;
    mc.icap = 0x7A;
    mc.valid = true;
    BoardProfile h = board_detect(&mc);
    CHECK_EQ(h.kind, BOARD_MULTICORE);
    CHECK(h.has_handover);
    CHECK(h.can_return_host);
    CHECK_STR(board_name(h), "multicore");
    CHECK(!h.cfg_dir[0] && h.rom_dir == 0);              // its settings live beside the .cor, its ROMs under <root>/roms/<folder>
    // ... and the same host started over JTAG: no valid page, but the ICAP loader is there
    mc.valid = false;
    BoardProfile hd = board_detect(&mc);
    CHECK_EQ(hd.kind, BOARD_MULTICORE);
    CHECK(!hd.has_handover && hd.can_return_host);
    CHECK(strstr(hd.why, "no valid hand-over") != 0);
    // T239.3 V-4b: a probe runs only when the block it asks of is in the app's driver table. The whole table (the shared app's): every probe is read and holds.
    FakeProbe all;
    all.icap = 0x7A;
    BoardProfile ba = board_detect(&all);
    CHECK_EQ(ba.probes & (BC_DRIVE_A | BC_IEC | BC_CARTRIDGE | BC_REU | BC_RING | BC_UCI), BC_DRIVE_A | BC_IEC | BC_CARTRIDGE | BC_REU | BC_RING | BC_UCI);
    CHECK_EQ(all.calls, 1); CHECK_EQ(all.fpga_reads, 1); CHECK_EQ(all.caps_reads, 1); CHECK_EQ(all.iec_reads, 1);
    // the VIC-20's table: the ICAP loader, drive_a and iec_status, no cmd_if, c64cart, c64_debug or dma. The hardware would answer yes to every bit; the app asks only for its own.
    FakeProbe vic;
    vic.icap = 0x7A;
    vic.has = BB_ICAP | BB_DRIVE_A | BB_IEC;
    BoardProfile bv = board_detect(&vic);
    CHECK_EQ(bv.kind, BOARD_MULTICORE);
    CHECK_EQ(bv.probes & (BC_DRIVE_A | BC_IEC), BC_DRIVE_A | BC_IEC);
    CHECK_EQ(bv.probes & (BC_CARTRIDGE | BC_REU | BC_RING | BC_UCI), 0u);
    CHECK_EQ(vic.caps_reads, 0);                          // TRX_CAPS is read for the cartridge and the ring only
    CHECK_EQ(vic.fpga_reads, 1);                          // the ITU word is read for the drive bit; the command interface bit in it is not asked
    // no ICAP block: the byte is not read, the profile is the standalone installation's, and a drive without its block is not asked either
    FakeProbe none;
    none.icap = 0x7A;
    none.has = 0;
    BoardProfile bn = board_detect(&none);
    CHECK_EQ(bn.kind, BOARD_MONOLITH);
    CHECK_EQ(none.calls, 0); CHECK_EQ(none.fpga_reads, 0); CHECK_EQ(none.caps_reads, 0); CHECK_EQ(none.iec_reads, 0);
    CHECK_EQ(bn.probes & ~(uint32_t)BC_SID_SOCKETS, 0u);
    // the cartridge and the ring ask for their own block each: the byte is read once for either, the bit of the other is not struck for it
    FakeProbe cart;
    cart.has = BB_CART;
    BoardProfile bc = board_detect(&cart);
    CHECK_EQ(bc.probes & (BC_CARTRIDGE | BC_RING | BC_REU), BC_CARTRIDGE);
    CHECK_EQ(cart.caps_reads, 1);
    FakeProbe ring;
    ring.has = BB_RING | BB_REU;
    BoardProfile br = board_detect(&ring);
    CHECK_EQ(br.probes & (BC_CARTRIDGE | BC_RING | BC_REU), BC_RING | BC_REU);
    // a command interface block without the drive: the UCI bit holds, the drive bit is not taken from the same word
    FakeProbe uci;
    uci.has = BB_CMD_IF;
    BoardProfile bu = board_detect(&uci);
    CHECK_EQ(bu.probes & (BC_DRIVE_A | BC_UCI), BC_UCI);
    // the capabilities the descriptor lists are struck by that profile: a VIC-20 listing cartridge, reu, ring and uci loses all four, drive_a and iec stay
    const char *vcaps[] = { "drive_a", "iec", "cartridge", "reu", "ring", "uci" };
    const char *kept[6];
    int nk = board_effective_caps(bv, vcaps, 6, kept, 6, 0, 0);
    CHECK_EQ(nk, 2);
    CHECK(names(kept, nk, "drive_a") && names(kept, nk, "iec"));

    // a byte that is neither 0 nor 0x7A is no loader
    FakeProbe odd;
    odd.icap = 0x41;
    BoardProfile o = board_detect(&odd);
    CHECK_EQ(o.kind, BOARD_MONOLITH);
    CHECK(strstr(o.why, "0x41") != 0);

    // the menu: "Return to host menu" exists only where the loader does (negative control: the monolith model with the byte forced to 0x7A)
    for (int k = 0; k < 2; k++) {
        const BoardProfile &p = k ? h : m;
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        FakeIo io; FakeFs fs; Host host;
        c.desc = &c64_desc; c.io = &io; c.fs = &fs; c.board = &p;
        MenuCtl mc_ctl(&c, &host);
        CHECK_EQ(has_entry(mc_ctl.tree(), EK_RETURN_HOST), k == 1);
        CHECK(has_entry(mc_ctl.tree(), EK_RESET_HOST));
    }
    {   // a context without a profile is the MultiCore host of before T104.2
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        FakeIo io; FakeFs fs; Host host;
        c.desc = &c64_desc; c.io = &io; c.fs = &fs;
        MenuCtl ctl(&c, &host);
        CHECK(has_entry(ctl.tree(), EK_RETURN_HOST));
    }

    // ---- T115 7 H1, R1-R5: the held page ("This core cannot be started") offers the way back where the SYSTEM menu does
    {
        const char *why = "This application has no description for the core with the ID ZZZZ. The core stays held.";
        char text[400], msg[120] = "";
        struct { const char *name; const BoardProfile *p; bool offer; } cases[] = {
            { "R1 multicore, layer-2 core, no page", &hd, true },
            { "R2 multicore, large .cor, valid page", &h, true },
            { "R3 monolith, no page", &m, false },
            { "R4 monolith, valid page (large TRX64 started by the host)", &mh, false },
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const BoardProfile *p = cases[i].p;
            held_text(why, p, 0, text, sizeof(text));
            CHECK_EQ(held_offers_return(p), cases[i].offer);
            CHECK_EQ(strstr(text, "Press RETURN to return to the host menu.") != 0, cases[i].offer);
            CHECK(strstr(text, "The core stays held.") != 0);                  // the reason is always the first thing on the page
            ret_calls = 0;
            CHECK_EQ(held_key(p, KEY_RETURN, ret_fails, msg, sizeof(msg)), cases[i].offer ? HELD_RETURN_FAILED : HELD_IGNORED);
            CHECK_EQ(ret_calls, cases[i].offer ? 1 : 0);                      // the loader is called once where it exists, never where it does not
            CHECK_EQ(held_key(p, 'x', ret_fails, msg, sizeof(msg)), HELD_IGNORED);   // any other key does nothing
            CHECK_EQ(ret_calls, cases[i].offer ? 1 : 0);
            // the SYSTEM menu agrees with the page (R2: the large core has the entry)
            CoreCtx c;
            memset(&c, 0, sizeof(c));
            FakeIo io; FakeFs fs; Host host;
            c.desc = &c64_desc; c.io = &io; c.fs = &fs; c.board = p;
            MenuCtl ctl(&c, &host);
            CHECK_EQ(has_entry(ctl.tree(), EK_RETURN_HOST), cases[i].offer);
        }
        // R5: the return came back: the reason reaches the page, the page keeps the reason of the refusal and the hint
        held_text(why, &h, "no ICAP loader in this bitstream", text, sizeof(text));
        CHECK(strstr(text, "The way back did not work: no ICAP loader in this bitstream") && strstr(text, "Press RETURN"));
        held_text(why, &m, "x", text, sizeof(text));
        CHECK(!strstr(text, "Press RETURN"));
        CHECK(!held_offers_return(0));
    }

    // ---- S4: the descriptor names what the machine may have, the probes strike
    {
        const char *out[16];
        char struck[80];
        int n = board_effective_caps(m, c64_desc.caps, c64_desc.ncaps, out, 16, struck, sizeof(struck));
        CHECK_EQ(n, 7);                                   // the monolith: all seven
        CHECK_STR(struck, "");
        for (int i = 0; i < c64_desc.ncaps; i++) CHECK(names(out, n, c64_desc.caps[i]));

        // MultiCore trx64 core: no T37 sockets (the registers are write only, so the profile answers), and a build without the UCI
        mc.valid = true;
        mc.fpga = BOARD_FPGA_DRIVE_1541_1;
        BoardProfile h2 = board_detect(&mc);
        n = board_effective_caps(h2, c64_desc.caps, c64_desc.ncaps, out, 16, struck, sizeof(struck));
        CHECK_EQ(n, 5);
        CHECK(!names(out, n, "sid_sockets") && !names(out, n, "uci"));
        CHECK(names(out, n, "drive_a") && names(out, n, "iec") && names(out, n, "cartridge") && names(out, n, "reu") && names(out, n, "ring"));
        CHECK(strstr(struck, "sid_sockets") && strstr(struck, "uci"));

        // each probe by itself: a bitstream that lacks the thing loses exactly its name
        struct { const char *name; void (*strip)(FakeProbe &); } one[] = {
            { "drive_a", [](FakeProbe &p) { p.fpga &= ~BOARD_FPGA_DRIVE_1541_1; } },
            { "iec", [](FakeProbe &p) { p.iec = 0; } },
            { "cartridge", [](FakeProbe &p) { p.caps &= ~BOARD_TRX_CAPS_CART; } },
            { "ring", [](FakeProbe &p) { p.caps &= ~BOARD_TRX_CAPS_ENGINE; } },
            { "uci", [](FakeProbe &p) { p.fpga &= ~BOARD_FPGA_COMMAND_INTF; } },
        };
        for (auto &t : one) {
            FakeProbe p;
            t.strip(p);
            BoardProfile bp = board_detect(&p);
            n = board_effective_caps(bp, c64_desc.caps, c64_desc.ncaps, out, 16, struck, sizeof(struck));
            CHECK_EQ(n, 6);
            CHECK(!names(out, n, t.name));
            CHECK_STR(struck, t.name);
        }
        // a name the board has no probe for stays: the table is the probes', not a whitelist
        const char *odd_caps[] = { "sio", "cartridge", "drive_b" };
        FakeProbe none;
        none.caps = 0;
        BoardProfile bn = board_detect(&none);
        n = board_effective_caps(bn, odd_caps, 3, out, 16, struck, sizeof(struck));
        CHECK_EQ(n, 2);
        CHECK(names(out, n, "sio") && names(out, n, "drive_b") && !names(out, n, "cartridge"));
        // the table is read by the output limit too
        n = board_effective_caps(m, c64_desc.caps, c64_desc.ncaps, out, 3, 0, 0);
        CHECK_EQ(n, 3);
    }
    // the Atari and the NES list what they have and ask for no striking
    CHECK(!atari800_desc.board_caps && !nes_desc.board_caps && c64_desc.board_caps);

    // ---- T104.1 5.5: where the boot-attempt counter lives - a path the flash guard lets through, one per profile
    CHECK_STR(h.boot_cfg, "/Flash/boot.cfg");
    CHECK_STR(hd.boot_cfg, "/Flash/boot.cfg");
    CHECK(!fs_write_refused(h.boot_cfg));
    CHECK(fs_write_refused("/flash/trx64u/boot.cfg") && fs_write_refused("/flash/trx64u/c64.cfg"));   // before the core is known nothing of the flash is open
    CHECK(board_set_core(&m, c64_desc.core_id, c64_desc.folder));        // trx64u's descriptor: today's paths
    CHECK_STR(m.cfg_dir, "/flash/trx64u");
    CHECK_STR(m.boot_cfg, "/flash/trx64u/boot.cfg");
    CHECK(!fs_write_refused(m.boot_cfg) && !fs_write_refused("/flash/trx64u/c64.cfg") && !fs_write_refused("/Flash/boot.cfg"));
    CHECK(fs_write_refused("/flash/trx64u/other.cfg"));
    // T239.3 P1-5, the paths come from the record: another core id names another directory, and the old core's files close again (a persistence path: control in run.sh)
    {
        BoardProfile v = m;
        CHECK(board_set_core(&v, "vc20", "vic20"));
        CHECK_STR(v.cfg_dir, "/flash/vc20");
        CHECK_STR(v.boot_cfg, "/flash/vc20/boot.cfg");
        CHECK(!fs_write_refused("/flash/vc20/vic20.cfg") && !fs_write_refused("/FLASH/VC20/BOOT.CFG"));
        CHECK(fs_write_refused("/flash/trx64u/c64.cfg") && fs_write_refused("/flash/trx64u/boot.cfg"));
        CHECK(fs_write_refused("/flash/vc20/c64.cfg") && fs_write_refused("/flash/vc20/boot.cfg.bak"));
        CoreDesc d = c64_desc;                                           // the settings file is "<cfg_dir>/<folder>.cfg": the folder of the descriptor, in the id's directory
        d.folder = "vic20";
        CoreCtx cv;
        memset(&cv, 0, sizeof(cv));
        cv.desc = &d;
        start_paths(&cv, 0, &v);
        CHECK_STR(cv.cfg_path, "/flash/vc20/vic20.cfg");
        CHECK(!fs_write_refused(cv.cfg_path));
        // a malformed id (a path piece, a space, too long) is refused and opens nothing; no id keeps the settings in memory
        BoardProfile bad = m;
        CHECK(!board_set_core(&bad, "../x", "c64") && !bad.cfg_dir[0] && !bad.boot_cfg[0]);
        CHECK(!board_set_core(&bad, "a b", "c64") && !board_set_core(&bad, "a/b", "c64"));
        CHECK(!board_set_core(&bad, "abcdefghijklmnopqrstuvwxyz0123456", "c64") && !bad.cfg_dir[0]);
        CHECK(fs_write_refused("/flash/x/boot.cfg") && fs_write_refused("/flash/vc20/vic20.cfg"));
        CHECK(board_set_core(&bad, 0, "c64") && !bad.cfg_dir[0] && !bad.boot_cfg[0]);
        CHECK(board_set_core(&bad, "", "c64") && !bad.cfg_dir[0]);
        CoreCtx cn;
        memset(&cn, 0, sizeof(cn));
        cn.desc = &c64_desc;
        start_paths(&cn, 0, &bad);
        CHECK_STR(cn.cfg_path, "");
        // a folder that is no name leaves the counter's file open and the settings file shut
        CHECK(board_set_core(&bad, "vc20", "a/b"));
        CHECK(!fs_write_refused("/flash/vc20/boot.cfg") && fs_write_refused("/flash/vc20/a/b.cfg"));
        // an id and a folder of the longest allowed names make a settings path that does not fit: refused, and no other file is registered in its place
        {
            BoardProfile lg = m;
            char id[BOARD_CORE_ID_MAX + 1], fo[64];
            memset(id, 'i', BOARD_CORE_ID_MAX); id[BOARD_CORE_ID_MAX] = 0;
            memset(fo, 'f', 63); fo[63] = 0;
            CHECK(!board_set_core(&lg, id, fo));
            CHECK(!fs_write_refused(lg.boot_cfg));
            std::string cut = std::string(lg.cfg_dir) + "/" + fo + ".cfg";
            cut.resize(FS_FLASH_EXC_LEN - 1);
            CHECK(fs_write_refused(cut.c_str()));
            CHECK(board_set_core(&m, c64_desc.core_id, c64_desc.folder));
        }
        // the MultiCore profile is not touched: no flash directory, the host's file, nothing registered
        BoardProfile hh = h;
        CHECK(board_set_core(&hh, "vc20", "vic20") && !hh.cfg_dir[0]);
        CHECK_STR(hh.boot_cfg, "/Flash/boot.cfg");
        CHECK(fs_write_refused("/flash/vc20/vic20.cfg"));
        CHECK(board_set_core(&m, c64_desc.core_id, c64_desc.folder));    // back to trx64u for the rest
    }

    // ---- S11: where the settings live. The monolith has no .cor, so the profile gives the directory, and the file is named by the folder.
    {
        CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = &c64_desc;
        start_paths(&c, 0, &m);
        CHECK_STR(c.cfg_path, "/flash/trx64u/c64.cfg");    // the one file the CoreFs lets through the flash guard (fsif.h)
        CHECK_STR(c.cfg_path, "/flash/trx64u/c64.cfg");
        CHECK(fs_flash_exception(c.cfg_path) && !fs_write_refused(c.cfg_path));
        // T228 DC13: the parameter flash_cfg decides, not the board's kind: on for the monolith (standalone), off under the MultiCore host (D11)
        CHECK(m.flash_cfg && !h.flash_cfg && !hd.flash_cfg);
        {
            BoardProfile off = m;                          // a standalone installation that turns the parameter off keeps its settings in memory
            off.flash_cfg = false;
            CoreCtx c2;
            memset(&c2, 0, sizeof(c2));
            c2.desc = &c64_desc;
            start_paths(&c2, 0, &off);
            CHECK(!c2.cfg_path[0]);
            BoardProfile on = h;                           // the MultiCore profile with the parameter on: the flash file again
            on.flash_cfg = true;
            snprintf(on.cfg_dir, sizeof(on.cfg_dir), "%s", m.cfg_dir);
            start_paths(&c2, 0, &on);
            CHECK_STR(c2.cfg_path, "/flash/trx64u/c64.cfg");
            CHECK(!start_migrate_cfg(&c2, &off));          // off: no old file is looked for
        }
        // the MultiCore host: beside the .cor when there is a hand-over, nowhere when there is none
        Handover hv;
        handover_fill(&hv, "/usb1/cores/trx64.cor", "TRX64U", "1.0.0", 7);
        start_paths(&c, &hv, &h);
        CHECK_STR(c.cfg_path, "/usb1/cores/trx64/trx64.cfg");
        // T112 Q1, a large core on the monolith profile: the hand-over path wins over the flash file, the .cfg lies in the core's folder (T148 D1)
        Handover lv;
        handover_fill(&lv, "/usb1/cores/trx64/trx64.cor", "", "", 0, 0);
        start_paths(&c, &lv, &mh);
        CHECK_STR(c.cor_path, "/usb1/cores/trx64/trx64.cor");
        CHECK_STR(c.cfg_path, "/usb1/cores/trx64/trx64/trx64.cfg");
        {   // the ROMs: the core's folder alone; /flash/roms is a source of the one-time copy (T148 D2, D3)
            char own[96];
            CHECK(rom_own_dir(c.cor_path, own, sizeof(own)));
            CHECK_STR(own, "/usb1/cores/trx64/trx64/roms");
            FakeFs rf;
            RomResult rr[8];
            char sm[300];
            rf.put_size("/flash/roms/kernal.bin", 8192, 1);
            rf.put_size("/usb1/cores/trx64/trx64/roms/kernal.bin", 8192, 2);
            static const RomSpec ks[1] = { { "KERNAL", { "kernal.bin", 0 }, { 8192, 0 }, true, 0, 0 } };
            CHECK_EQ(rom_scan(&rf, "c64", ks, 1, rr, sm, sizeof(sm), mh.rom_dir, own), ROM_FOUND);
            CHECK_STR(rr[0].hit.dir, "/usb1/cores/trx64/trx64/roms");
            rf.remove("/usb1/cores/trx64/trx64/roms/kernal.bin");        // T148 D2/D3: nothing in the core folder, so nothing is read from /flash/roms ...
            CHECK_EQ(rom_scan(&rf, "c64", ks, 1, rr, sm, sizeof(sm), mh.rom_dir, own), ROM_MISSING);
            CHECK_EQ(rom_import(&rf, "c64", ks, 1, own, mh.rom_dir, 0), 1);       // ... but it is a source of the one-time copy; the flash file stays
            CHECK_EQ(rom_scan(&rf, "c64", ks, 1, rr, sm, sizeof(sm), mh.rom_dir, own), ROM_FOUND);
            CHECK_STR(rr[0].hit.dir, "/usb1/cores/trx64/trx64/roms");
            CHECK(rf.has("/flash/roms/kernal.bin"));
        }
        {   // settings are not lost: the flash file is copied into the core's folder once, the original stays, an existing .cfg is never overwritten
            FakeFs sf;
            c.fs = &sf;
            sf.put("/flash/trx64u/c64.cfg", std::vector<uint8_t>{'a', '=', '1', '\n'});
            CHECK(start_migrate_cfg(&c, &mh));
            uint64_t sz; bool d;
            CHECK(sf.stat("/usb1/cores/trx64/trx64/trx64.cfg", &sz, &d) && sz == 4);
            CHECK(sf.stat("/flash/trx64u/c64.cfg", &sz, &d));
            sf.put("/usb1/cores/trx64/trx64/trx64.cfg", std::vector<uint8_t>{'b'});
            CHECK(!start_migrate_cfg(&c, &mh));
            CHECK(sf.stat("/usb1/cores/trx64/trx64/trx64.cfg", &sz, &d) && sz == 1);
            // no hand-over: cfg_path is the flash file itself, nothing to copy
            start_paths(&c, 0, &m);
            CHECK(!start_migrate_cfg(&c, &m));
            c.fs = 0;
        }
        start_paths(&c, 0, &hd);
        CHECK_STR(c.cfg_path, "");
        start_paths(&c, 0);                                // before T104.2 there was no profile at all
        CHECK_STR(c.cfg_path, "");
        // a profile without a context descriptor does not guess a name
        CoreCtx nod;
        memset(&nod, 0, sizeof(nod));
        start_paths(&nod, 0, &m);
        CHECK_STR(nod.cfg_path, "");
    }
    {   // the value survives a restart; with the path unwritable it does not, and the screen is told why
        const CoreDesc *cd = trx64u::desc();                       // the C64's settings are in its menu record (T104.2 D10)
        const Setting *it = cd->settings;
        FakeFs fs;
        {
            Settings s(it, cd->nsettings, cd->store, &fs, "/flash/trx64u/c64.cfg", 0);
            s.load();
            int i = s.find("Freeze mode");
            CHECK(i >= 0);
            CHECK_EQ(s.value(i), 0);                       // Whole machine
            CHECK(s.set(i, 1));
            CHECK(fs.has("/flash/trx64u/c64.cfg"));
            CHECK_TXT(fs.text("/flash/trx64u/c64.cfg"), "[C64 Settings]\nFreeze mode=CPU only\n\n");
        }
        {
            Settings s(it, cd->nsettings, cd->store, &fs, "/flash/trx64u/c64.cfg", 0);
            s.load();                                      // the restart
            CHECK_EQ(s.value(s.find("Freeze mode")), 1);
            CHECK_EQ(s.layer(s.find("Freeze mode")), Settings::L_CORE);
        }
        FakeFs ro;
        ro.read_only = true;
        {
            Settings s(it, cd->nsettings, cd->store, &ro, "/flash/trx64u/c64.cfg", 0);
            s.load();
            int i = s.find("Freeze mode");
            CHECK(!s.set(i, 1));
            CHECK_EQ(s.value(i), 1);                       // holds for the session ...
            const char *why = s.take_notice();
            CHECK(strstr(why, "cannot be saved") != 0);    // ... and the screen is told why
        }
        {
            Settings s(it, cd->nsettings, cd->store, &ro, "/flash/trx64u/c64.cfg", 0);
            s.load();
            CHECK_EQ(s.value(s.find("Freeze mode")), 0);   // after the restart it is the default again
            CHECK_EQ(s.layer(s.find("Freeze mode")), Settings::L_DEFAULT);
        }
    }
    DONE("test_board");
}
