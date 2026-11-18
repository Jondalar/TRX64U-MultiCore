// The TI-99/4A core's description for the generic application (blueprint). Written for this project; the machine is zeldin/Mega99 (cores/ti99/rtl/NOTICE).
// Modelled on coreapp/cores/vc20.cc: ROMs are put through the aperture and read back; the register block is ti99_regs.h.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hk_defaults.h"
#include "ctx.h"
#include "boot.h"
#include "desc.h"
#include "cores.h"
#include "ti99_regs.h"

// ---- ROMs: the console ROM (two files, high and low bytes) and GROM 0..2, as MAME's ti99_4a set names them ---------------------------------------
// The host's ROM aperture hands the core block-relative addresses (CHAR 0x0000, KERNAL 0x2000, BASIC 0x4000); ti99_win undoes that, so an image is written through
// the 8 KB BASIC block at offset 0 (the console ROM: high bytes at 0, low bytes at 0x1000). TI99_WBHI is the upper wishbone address byte, TI99_WBADD (n * 0x20)
// picks the GROM slot. Every image is read back; a mismatch fails the load and the message says what came back.
static int put_at(CoreCtx *c, const OpArgs &a, uint8_t hi, uint8_t wadd, uint32_t ofs, uint32_t size, const char *role, bool must_match, OpOut &o)
{
    static uint8_t back[TI99_GROM_BYTES];
    if (a.len(0) != size) {
        snprintf(o.msg, o.msglen, "%s: %u bytes, the machine takes %u", role, (unsigned)a.len(0), (unsigned)size);
        return OPE_FAILED;
    }
    c->io->wr(TI99_WBHI, hi);
    c->io->wr(TI99_WBADD, wadd);
    c->io->rom_write(ofs, a.p(0), size);
    c->io->rom_read(ofs, back, size);
    if (memcmp(back, a.p(0), size) != 0) {
        const uint8_t *want = a.p(0);
        uint32_t first = 0, nd = 0;
        for (uint32_t i = 0; i < size; i++)
            if (back[i] != want[i]) { if (!nd) first = i; nd++; }
        (void)must_match;
        snprintf(o.msg, o.msglen, "%s: read-back differs, %u of %u bytes, first at +%04X (got %02X want %02X), start %02X%02X%02X%02X",
                 role, (unsigned)nd, (unsigned)size, (unsigned)first, back[first], want[first], back[0], back[1], back[2], back[3]);
        return OPE_FAILED;
    }
    return OPE_OK;
}
static int op_put_hb(CoreCtx *c, const OpArgs &a, OpOut &o)    { return put_at(c, a, TI99_HI_CONSOLE, 0x00, TI99_ROM_HB, TI99_ROM_BYTES, "console ROM (high bytes)", true, o); }
static int op_put_lb(CoreCtx *c, const OpArgs &a, OpOut &o)    { return put_at(c, a, TI99_HI_CONSOLE, 0x00, TI99_ROM_LB, TI99_ROM_BYTES, "console ROM (low bytes)", true, o); }
static int op_put_grom0(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_at(c, a, TI99_HI_GROM, 0x00, 0x0000, TI99_GROM_BYTES, "GROM 0", true, o); }
static int op_put_grom1(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_at(c, a, TI99_HI_GROM, 0x20, 0x0000, TI99_GROM_BYTES, "GROM 1", true, o); }
static int op_put_grom2(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_at(c, a, TI99_HI_GROM, 0x40, 0x0000, TI99_GROM_BYTES, "GROM 2", true, o); }
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

static const RomSpec roms[5] = {
    { "Console ROM (high bytes)", { "994a_rom_hb.u610", "994a_rom_hb.bin", "994a_rom_hb", 0 }, { TI99_ROM_BYTES, 0 }, true, 0, "ti99.put_hb" },
    { "Console ROM (low bytes)",  { "994a_rom_lb.u611", "994a_rom_lb.bin", "994a_rom_lb", 0 }, { TI99_ROM_BYTES, 0 }, true, 0, "ti99.put_lb" },
    { "GROM 0 (system)",   { "994a_grom0.u500", "994a_grom0.bin", "994a_grom0", 0 }, { TI99_GROM_BYTES, 0 }, true, 0, "ti99.put_grom0" },
    { "GROM 1 (BASIC)",    { "994a_grom1.u501", "994a_grom1.bin", "994a_grom1", 0 }, { TI99_GROM_BYTES, 0 }, true, 0, "ti99.put_grom1" },
    { "GROM 2 (BASIC)",    { "994a_grom2.u502", "994a_grom2.bin", "994a_grom2", 0 }, { TI99_GROM_BYTES, 0 }, true, 0, "ti99.put_grom2" },
};

// ---- settings: the bits of CFG ------------------------------------------------------------------------------------------------------------------
static const char *const onoff_names[] = { "Off", "On" };
static const char *const cpu_names[]   = { "Normal (3 MHz)", "Fast" };
static const RegBind b_turbo = { TI99_CFG, 0, 1, 0 };
static const RegBind b_swap  = { TI99_CFG, 1, 1, 0 };
static const RegBind b_ram   = { TI99_CFG, 2, 1, 0 };
static const Setting settings[3] = {
    { "32K RAM card", onoff_names, 2, 1, WHEN_RESET, 0, &b_ram },
    { "CPU speed",                    cpu_names,   2, 0, WHEN_LIVE,  0, &b_turbo },
    { "Swap joysticks",               onoff_names, 2, 0, WHEN_LIVE,  0, &b_swap },
};

static const Task tasks[2] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Reload ROMs, reset", "machine.reload_roms", 0, 0, true },
};

// ---- control ------------------------------------------------------------------------------------------------------------------------------------
static int ti99_init(CoreCtx *c, char *, int)
{
    c->io->mode_reset(true);
    return 0;
}
static int ti99_release(CoreCtx *c, char *, int)
{
    c->io->mode_reset(false);
    return 0;
}

static void ti99_register(OpRegistry &r)
{
    r.contributor("ti99");
    r.add_op("ti99.put_hb", rom_arg, 1, op_put_hb);
    r.add_op("ti99.put_lb", rom_arg, 1, op_put_lb);
    r.add_op("ti99.put_grom0", rom_arg, 1, op_put_grom0);
    r.add_op("ti99.put_grom1", rom_arg, 1, op_put_grom1);
    r.add_op("ti99.put_grom2", rom_arg, 1, op_put_grom2);
}

// T162 D3: the state block of the bug report. Side-effect-free reads only. 0B0-0B9 are the diagnostic probes (CPU PC, ST, WP, GROM address, VDP address).
static const ReportReg report_regs[] = {
    { "CTRL (b0 reset, b1 halt)", 0x0A0, 1 }, { "CFG", 0x0A1, 1 }, { "STATUS (b0 in reset, b1 held)", 0x0A4, 1 },
    { "FRAMES", 0x0A8, 2 }, { "FB_LATEST", 0x0C0, 1 }, { "FRAME_SEQ", 0x0C1, 1 }, { "FB_OVERRUNS", 0x0C2, 2 },
    { "CPU PC", 0x0B0, 2 }, { "CPU ST", 0x0B2, 2 }, { "CPU WP", 0x0B4, 2 }, { "GROM address", 0x0B6, 2 }, { "VDP address", 0x0B8, 2 },
};

extern const CoreDesc ti99_desc;
const CoreDesc ti99_desc = {
    { 'T', 'I', '9', '9' },
    "TI-99/4A",
    "TI-99/4A",
    "TI-99/4A",
    "ti99",
    "TI-99/4A Settings",
    1,
    roms, 5,
    settings, 3,
    tasks, 2,
    0, 0,                                      // file types: cartridges (.rpk) and disks come later
    0, 0,                                      // media slots
    // 280 x 240 shown 3 x 3 (840 x 720, the generic application allows 1..3; the Scale setting of the video service fits or fills the screen from there),
    // centred on 1920 x 1080; the buffers are the core's reset values of FB_BASE / FB_STRIDE
    { 280, 240, 3, 540, 180, TI99_FB_ADDR, TI99_FB_STRIDE_BYTES, 4, TI99_FB_LATEST, TI99_FRAME_SEQ, TI99_FB_BASE, TI99_FB_STRIDE },
    { TI99_STATUS, TI99_ST_HELD, 200 },
    ti99_init, ti99_release, 0, 20,
    0, 0, false,                               // no service poll
    0, 0,
    ti99_register,
    0, 0,                                      // capabilities: none
    false, false, 0, 0, 0, 0, 0, 0, 0,
    0,                                         // type script
    0,                                         // run
    0,                                         // memory size
    0, 0, 0, 0,                                // cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    report_regs, (int)(sizeof(report_regs) / sizeof(report_regs[0])),
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    0, 0,                                      // media flush / pending
    0,                                         // mount slots
    &hk_cbm,                                   // the default hotkey map (hk_defaults.h): placeholder, the C64 family's; a TI map comes with the keyboard work
    0, false, 0,                               // alt_video, no_tone, rom_key
    0,                                         // machine_info
};
