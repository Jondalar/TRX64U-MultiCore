// The VIC-20 descriptor (T83, T104; Alex 2026-10-22: "apart from trx64 and c64u, switch everything to the generic core app for now"). It
// replaces the VC-20 build of Gideon's application (fork branch t90-c16app, software/io/vc20/vc20.cc): the three ROMs into the host's
// aperture, the memory expansion as two register settings taken in reset, Reset and Reload ROMs, and a .prg run or loaded through the PRG
// library (cbm_run.cc). The picture (T119): four 376 x 283 buffers the core's frame writer fills (vc20_pixw), shown 3 x 3 on the pixel layer
// as for the C264; the host's scaler only gets the idle raster it locks to. Freeze is the host's C64_STOP and the status byte says when the core holds. The register block is
// cores/vc20/rtl/trx/vc20_regs.vhd. The 1541 drive A is the cbm1541 library (T104.1 D15, cbm1541.cc): the descriptor
// splices its settings, tasks, file actions, media slot and ROM in (cbm1541_desc.h); see T83 section 5.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "vc20_regs.h"
#include "cbm_run.h"
#include "cbm1541.h"
#include "cbm1541_desc.h"
#include "drive_rom_pick.h"
#include "cores.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application ----------------------------------------------------------------------------------
// The KERNAL and the 1541 DOS ROM come in a stock and a JiffyDOS form (T199): both are kept here and the JiffyDOS setting, taken at reset, chooses the
// pair. `kernal_applied` is what the aperture holds now (DR_*), `drv` is the drive's pick (drive_rom_pick.h).
struct Vc20State {
    CbmRun run;
    uint8_t jiffy;                                      // the JiffyDOS setting
    uint8_t have_k, have_jk;                            // the stock / the JiffyDOS KERNAL was supplied
    uint8_t kernal_applied;                             // what the aperture holds (DR_*)
    uint8_t kernal[VC20_KERNAL_BYTES], jkernal[VC20_KERNAL_BYTES];
    DriveRomPick drv;
};
static const DriveRomKind vc20_drive = { "drive.put_rom", "vc20", "1541", false };

static Vc20State *vc20_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new Vc20State();
        memset(c->state, 0, sizeof(Vc20State));
    }
    return (Vc20State *)c->state;
}

// ---- settings ------------------------------------------------------------------------------------------------------------------
// RAM_EXT bits and what each choice sets: 3K at $0400 (b0), 8K blocks at $2000 (b1), $4000 (b2), $6000 (b3), $A000 (b4).
static const char *const exp_names[] = { "Unexpanded", "+3K", "+8K", "+16K", "+24K", "+3K +24K", "+3K +24K +8K at $A000" };
static const uint8_t exp_bytes[] = { 0x00, 0x01, 0x02, 0x06, 0x0E, 0x0F, 0x1F };
static const char *const blk5_names[] = { "RAM", "ROM (read only)" };
static const uint8_t blk5_bytes[] = { 0x00, 0x10 };           // RAM_RO b4: the block at $A000 refuses writes (it only exists with b4 of RAM_EXT)

static const RegBind b_exp  = { VC20_RAM_EXT, 0, 0x1F, exp_bytes };
static const RegBind b_blk5 = { VC20_RAM_RO, 0, 0x10, blk5_bytes };

#define KEY_JIFFY "JiffyDOS"
static const char *const jiffy_names[] = { "Off", "On" };
// T214: an external datasette on the C2N port (same device as on the PET, GitHub #26): TAPE_PIN b0 MOTOR and b1 WRITE driven (WRITE out is not proven by a SAVE on real tape yet),
// b4 READ / SENSE are the machine's inputs. Off releases every pin.
static const char *const dset_names[] = { "Off (file only)", "External datasette" };
static const uint8_t dset_bytes[2] = { 0x00, 0x13 };
static const RegBind b_dset = { VC20_TAPE_PIN, 0, 0x13, dset_bytes };
static const Setting settings[4 + CBM1541_NSETTINGS] = {
    { "Memory Expansion (at next reset)", exp_names,  7, 0, WHEN_RESET, 0, &b_exp, 0, 0, 0, 0, "Memory Expansion" },
    { "Block 5 ($A000), when expanded",   blk5_names, 2, 0, WHEN_RESET, 0, &b_blk5 },
    { KEY_JIFFY, jiffy_names, 2, 0, WHEN_RESET, "vc20.set_jiffy" },
    { "Datasette port", dset_names, 2, 0, WHEN_LIVE, 0, &b_dset },
    CBM1541_SETTINGS,
};

// ---- ROMs ----------------------------------------------------------------------------------------------------------------------
// Plain copies at the aperture's offsets, then read back: the core's arrays are readable through the same aperture.
static int put_bytes(CoreCtx *c, const uint8_t *p, uint32_t ofs, uint32_t size, const char *role, OpOut &o)
{
    static uint8_t back[VC20_BASIC_BYTES];
    c->io->rom_write(ofs, p, size);
    c->io->rom_read(ofs, back, size);
    if (memcmp(back, p, size) != 0) {
        snprintf(o.msg, o.msglen, "VC-20 ROMs: the core did not take the %s (read-back differs)", role);
        return OPE_FAILED;
    }
    return OPE_OK;
}
static int put_rom(CoreCtx *c, const OpArgs &a, uint32_t ofs, uint32_t size, const char *role, OpOut &o)
{
    if (a.len(0) != size) {
        snprintf(o.msg, o.msglen, "%s: %u bytes, the machine takes %u", role, (unsigned)a.len(0), (unsigned)size);
        return OPE_FAILED;
    }
    return put_bytes(c, a.p(0), ofs, size, role, o);
}
static int op_put_basic(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_rom(c, a, VC20_ROM_BASIC, VC20_BASIC_BYTES, "BASIC", o); }
// The stock KERNAL goes in at once (as always); the JiffyDOS setting swaps it at the reset (op_set_jiffy).
static int op_put_kernal(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int rc = put_rom(c, a, VC20_ROM_KERNAL, VC20_KERNAL_BYTES, "KERNAL", o);
    if (rc == OPE_OK) {
        Vc20State *s = vc20_state(c);
        memcpy(s->kernal, a.p(0), VC20_KERNAL_BYTES);
        s->have_k = 1;
        s->kernal_applied = DR_STOCK;
    }
    return rc;
}
static int op_put_jiffy_kernal(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    Vc20State *s = vc20_state(c);
    if (a.len(0) != VC20_KERNAL_BYTES) {
        snprintf(o.msg, o.msglen, "JiffyDOS VC-20 KERNAL: %u bytes, %u expected", (unsigned)a.len(0), (unsigned)VC20_KERNAL_BYTES);
        return OPE_FAILED;
    }
    memcpy(s->jkernal, a.p(0), VC20_KERNAL_BYTES);
    s->have_jk = 1;
    return OPE_OK;
}
static int op_put_char(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_rom(c, a, VC20_ROM_CHAR, VC20_CHAR_BYTES, "CHAR", o); }
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// ---- the 1541 ROM and the JiffyDOS pair ----------------------------------------------------------------------------------------
static int keep_drive_rom(CoreCtx *c, const OpArgs &a, bool is_jiffy, OpOut &o)
{
    Vc20State *s = vc20_state(c);
    return drive_pick_keep(c, s->drv, vc20_drive, a.p(0), a.len(0), is_jiffy, s->jiffy != 0 && s->have_jk, o);
}
static int op_put_drive_rom(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive_rom(c, a, false, o); }
static int op_put_jiffy_rom(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive_rom(c, a, true, o); }
// A 1541 ROM picked in the browser goes the same way (the library's own set_rom would bypass the choice).
static int op_set_drive_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    Vc20State *s = vc20_state(c);
    return drive_pick_keep_file(c, s->drv, vc20_drive, a.s(0), s->jiffy != 0 && s->have_jk, o);
}

// The setting JiffyDOS, written while the core is held (WHEN_RESET): On puts the JiffyDOS KERNAL into the machine and the JiffyDOS 1541 ROM into the
// drive when BOTH files were supplied; a pair with a file missing leaves both stock and says which file (no half switch).
static int op_set_jiffy(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    Vc20State *s = vc20_state(c);
    s->jiffy = a.i(0) ? 1 : 0;
    bool pair = s->have_jk && s->drv.n_jiffy;
    if (s->jiffy && !pair) {
        snprintf(o.msg, o.msglen, "JiffyDOS: %s not supplied, KERNAL and 1541 stay stock", s->have_jk ? "JiffyDOS 1541 ROM" : "JiffyDOS VC-20 KERNAL");
        printf("[vc20] %s\n", o.msg);
    }
    bool want = s->jiffy && pair;
    uint8_t k = want ? DR_JIFFY : DR_STOCK;
    if (k != s->kernal_applied && (want || s->have_k)) {
        int rc = put_bytes(c, want ? s->jkernal : s->kernal, VC20_ROM_KERNAL, VC20_KERNAL_BYTES, want ? "JiffyDOS KERNAL" : "KERNAL", o);
        if (rc != OPE_OK) {
            return rc;
        }
        s->kernal_applied = k;
        printf("[vc20] KERNAL: %s\n", want ? "JiffyDOS" : "stock");
    }
    drive_pick_apply(c, s->drv, vc20_drive, want, 0, 0);
    return OPE_OK;
}

// The names the old application tried (the VICE and the Commodore part names), first match wins.
static const RomSpec roms[6] = {
    { "BASIC", { "basic.bin", "basic", "basic-901486-01.bin", 0 }, { VC20_BASIC_BYTES, 0 }, true, 0, "vc20.put_basic" },
    { "KERNAL", { "kernal.bin", "kernal", "kernal.901486-07.bin", "kernal.901486-06.bin", 0 }, { VC20_KERNAL_BYTES, 0 }, true, 0, "vc20.put_kernal" },
    { "CHAR", { "char.bin", "chargen.bin", "chargen", "characters.bin", "chargen-901460-03.bin", "chargen-901460-02.bin", 0 },
      { VC20_CHAR_BYTES, 0 }, true, 0, "vc20.put_char" },
    { "1541 drive", { "1541.rom", "1541.bin", "1541-ii.bin", "1541-ii.rom", "1541ii.rom", "1541c.rom", "dos1541.rom", "dos1541-325302-01+901229-05.bin", 0 },
      { 16384, 32768, 0 }, false, 0, "vc20.put_drive_rom", 0, true },
    // T199: optional, used only by the setting JiffyDOS, and only as a pair
    { "JiffyDOS VC-20 KERNAL", { "jiffydos-vc20.bin", "jiffydos_vc20.bin", "jiffydos-vic20.bin", "jiffydos_vic20.bin", "jiffydos-kernal-vc20.bin", 0 },
      { VC20_KERNAL_BYTES, 0 }, false, 0, "vc20.put_jiffy_kernal" },
    { "JiffyDOS 1541 ROM", { "jiffydos-1541.bin", "jiffydos1541.bin", "jiffydos_1541.bin", "jiffydos-1541-ii.bin", "jiffydos1541-ii.bin", "jiffydos.bin", 0 },
      { 16384, 32768, 0 }, false, 0, "vc20.put_jiffy_rom" },
};

// ---- control -------------------------------------------------------------------------------------------------------------------
// The host holds the core from power-up until C64_MODE is written; the application's own hold is that reset line. The expansion registers are
// written (boot_apply_settings) while it is held, so the machine never sees the map change under it.
static int vc20_init(CoreCtx *c, char *, int)
{
    cbm_run_clear(&vc20_state(c)->run);                // a reset ends a program that was still waiting for READY
    c->io->mode_reset(true);
    return 0;
}

static int vc20_release(CoreCtx *c, char *, int)
{
    c->io->mode_reset(false);
    return 0;
}

// ---- .prg ----------------------------------------------------------------------------------------------------------------------
// The numbers of the old application (vc20.cc load_prg / type_keys): BASIC starts $1001 (unexpanded), $0401 (+3K), $1201 (8K or more); the start
// pointer pair at $2B with its window $0400-$2000, the three end pointers behind it, the keyboard buffer at $0277 with its count at $C6.
// ready_ms + settle_ms is the old application's fixed 2.5 s after a reset; here it counts the time the machine runs and waits for the
// KERNAL's start pointer in between (cbm_run.cc).
static CbmRunMachine machine(void)
{
    CbmRunMachine m;
    memset(&m, 0, sizeof(m));
    m.tag = "vc20";
    m.prg.basic_starts[0] = 0x1001;
    m.prg.basic_starts[1] = 0x0401;
    m.prg.basic_starts[2] = 0x1201;
    m.prg.nbasic_starts = 3;
    m.prg.txttab = VC20_TXTTAB;
    m.prg.txttab_min = 0x0400;
    m.prg.txttab_max = 0x2000;
    m.prg.vartab = 0x2D; m.prg.arytab = 0x2F; m.prg.strend = 0x31;
    m.prg.kbd_buffer = VC20_KBD_BUF; m.prg.kbd_count = VC20_KBD_COUNT;
    m.prg.p00_header = 0x1A;
    m.prg.min_size = 3;
    m.prg.max_size = 0x1A + 2 + 0x10000;
    m.ready_ms = 2200;
    m.settle_ms = 300;
    m.giveup_ms = 4000;
    return m;
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue(c, &vc20_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_load_now(c, &vc20_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int rc = cbm_run_service(c, &vc20_state(c)->run, machine());
    o.more = cbm1541_service(c);                        // the drive's write-back, on the same task (T104.1 D15 poll)
    return rc;
}
static int op_type(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue_script(c, &vc20_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec text_arg[] = { ARG_STRING("text", 63) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };
static const ArgSpec bit_arg[] = { ARG_INT("value", 0, 1) };
static const char *prg_name(CoreCtx *c) { return cbm_run_name(&vc20_state(c)->run); }
static const char *prg_pending(CoreCtx *c) { return cbm_run_pending(&vc20_state(c)->run); }

static const OpBind file_bind[] = { B_FILE("path") };
static const FileAction prg_actions[] = {
    { "Run PRG", "vc20.run_prg", file_bind, 1, true, 0, 0 },
    { "Load PRG (no reset, no start)", "vc20.load_prg", file_bind, 1, false, 0, 0 },
};
// The 1541 ROM of a file picked in the browser goes through the JiffyDOS choice (vc20.set_drive_rom), not the library's own set_rom.
static const FileAction rom_actions[] = {
    { "Set as 1541 ROM", "vc20.set_drive_rom", file_bind, 1, false, 0, 0 },
};
static const FileType filetypes[2 + CBM1541_NFILETYPES] = {
    { ".prg", prg_actions, 2 },
    { ".p00", prg_actions, 2 },
    { ".d64", c1541_d64_actions, 5, ".prg", c1541_inner_actions, 2 },
    { ".g64", c1541_g64_actions, 4, 0, 0, 0 },
    { ".rom", rom_actions, 1, 0, 0, 0 },
};

static const Task tasks[2 + CBM1541_NTASKS] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Reload ROMs, reset", "machine.reload_roms", 0, 0, true },
    CBM1541_TASKS,
};

static const MediaSlot media[] = { { "Program", "vc20.program", 0, 0, 0 }, CBM1541_MEDIA };

static const MemRegion vc20_mem_regions[] = { { 0, 0x10000, MK_RAM, 0, 0, "RAM" } };
static const MemMap vc20_mem = { vc20_mem_regions, 1, true, 0 };

static void vc20_register(OpRegistry &r)
{
    r.contributor("vc20");
    MEM_OPS(r, vc20_mem, 0xFFFF);
    r.add_op("vc20.put_basic", rom_arg, 1, op_put_basic);
    r.add_op("vc20.put_kernal", rom_arg, 1, op_put_kernal);
    r.add_op("vc20.put_char", rom_arg, 1, op_put_char);
    r.add_op("vc20.put_jiffy_kernal", rom_arg, 1, op_put_jiffy_kernal);
    r.add_op("vc20.put_drive_rom", rom_arg, 1, op_put_drive_rom);
    r.add_op("vc20.put_jiffy_rom", rom_arg, 1, op_put_jiffy_rom);
    r.add_op("vc20.set_drive_rom", path_arg, 1, op_set_drive_rom);
    r.add_op("vc20.set_jiffy", bit_arg, 1, op_set_jiffy);
    r.add_op("vc20.run_prg", path_arg, 1, op_run);
    r.add_op("vc20.load_prg", path_arg, 1, op_load);
    r.add_op("vc20.service", 0, 0, op_service);
    r.add_op("vc20.type_script", text_arg, 1, op_type);
    r.add_state("vc20.program", prg_name);
    r.add_state("vc20.pending", prg_pending);
    cbm1541_register(r);
}

// The VIC-20 core carries the 1541 (drive_l2) on the IEC bus; the cbm1541 library serves it (a library that needs the capability is refused
// or loads as these say, T104.2 D3).
static const char *const caps[] = { "iec", "drive_a" };

extern const CoreDesc vc20_desc;
const CoreDesc vc20_desc = {
    { 'V', 'C', '2', '0' },
    "VIC-20",
    "VIC-20",
    "VIC-20",
    "vc20",
    "VIC-20 Settings",
    1,
    roms, 6,
    settings, 4 + CBM1541_NSETTINGS,
    tasks, 2 + CBM1541_NTASKS,
    filetypes, 2 + CBM1541_NFILETYPES,
    media, 2,
    // 376 x 283 shown 3 x 3 (1128 x 849, VIC-20 pixels 5 : 3 as the writer makes them square), centred on 1920 x 1080; the buffers are the core's
    // reset values of FB_BASE / FB_STRIDE
    { 376, 283, 3, 396, 115, VC20_FB_ADDR, VC20_FB_STRIDE_BYTES, 4, VC20_FB_LATEST, VC20_FRAME_SEQ, VC20_FB_BASE, VC20_FB_STRIDE },
    { VC20_STATUS, VC20_ST_HELD, 200 },
    vc20_init, vc20_release, 0, 20,
    "vc20.service", 20, false,                  // a PRG waits for READY while the machine runs, not while the menu holds it; the drive's poll rides on it
    0, 0,
    vc20_register,
    caps, 2,
    false, false, 0, 0, 0, 0, 0, 0, 0,
    "vc20.type_script",
    "vc20.run_prg",
    0x10000,
    0, 0, 0, 0,                                // cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                      // menu_rec, menu_rec_n
    0,                                         // core_id
    0, 0,                                      // report_regs, n
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    cbm1541_media_flush, cbm1541_media_pending,   // T180 D4: the drive's write-back
    &cbm1541_mount_slots,                      // T180 D2, D5: Drive A
    &hk_cbm,                               // T181: the default hotkey map (hk_defaults.h)
    0, false, 0,                                     // alt_video, no_tone, rom_key
    "Memory Expansion",                    // T212 D3: machine_info
};
