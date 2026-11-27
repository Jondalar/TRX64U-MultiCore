// The C64SCPU descriptor (T182 step 8): a C64 with a SuperCPU, a LARGE core (flat bitstream, .cfw) on the generic application. What the person supplies
// goes into the core here: the SuperCPU ROM (scpu64.rom, the gate of D2: without it the core is not released and the 65816 stays in reset), the C64's
// BASIC, KERNAL and character ROM, and optionally the 1541 and 1581 drive ROMs and a JiffyDOS ROM of each. The register page is c64scpu_regs.h (the contract is
// in spec T182 section 3); the gateware side is cores/c64scpu/c64scpu_regs.v.
// Settings: the speed switch (20 MHz / 1 MHz, the cartridge's switch, D4), JiffyDOS (the switch bit the SuperCPU ROM reads, and the JiffyDOS ROM in
// drive A when it was supplied, D5), the joystick ports, the System Mode. Drive A is the cbm1541 library with the 1541 and the 1581 (T182 D17, as the C128's).
// Not in this descriptor: the cartridge port and the REU (D6: they need step 7 of the gateware, the pass-through), a Run PRG entry for a PRG file of the stick. Run Disk and the inner Mount & Run / Real Run run or type at READY (T206).
#include "hk_defaults.h"
#include "ctx.h"
#include "boot.h"
#include "c64scpu_regs.h"
#include "c64scpu_prg.h"
#include "cbm1541.h"
#include "cbm1541_desc.h"
#include "cbm_run.h"
#include "cores.h"
#include "drive_rom_pick.h"
#include "memops.h"
#include "ops_glue.h"
#include "picture.h"
#include <stdio.h>
#include <string.h>

// ---- the application's state for this core ----------------------------------------------------------------------------------------
// The two drive ROMs the person supplied are kept in `drv` (drive_rom_pick.h): which one the drive holds depends on the JiffyDOS setting, and the
// setting can change at any time (a put of a ROM and a change of the setting both end in apply_drive_rom).
struct ScpuState {
    CbmRun run;                                         // T206: Run Disk types LOAD"*",8,1 and RUN at READY (the C64's BASIC, cbm_run.cc)
    uint8_t fast;                                       // the speed switch: 1 = 20 MHz
    uint8_t jiffy;                                      // the JiffyDOS setting
    DriveRomPick drv, drv81;
};
static const DriveRomKind scpu_drive = { "drive.put_rom", "c64scpu", "1541", false };
static const DriveRomKind scpu_drive81 = { "drive.put_rom1581", "c64scpu", "1581", true, true };

static ScpuState *scpu_state(CoreCtx *c)
{
    if (!c->state) {
        ScpuState *s = new ScpuState();
        memset(s, 0, sizeof(ScpuState));
        s->fast = 1;                                    // the default of both settings: 20 MHz, JiffyDOS off
        c->state = s;
    }
    return (ScpuState *)c->state;
}

static void write_sw(CoreCtx *c)
{
    ScpuState *s = scpu_state(c);
    c->io->wr(SCPU_SW, (uint8_t)((s->fast ? SCPU_SW_FAST : 0) | (s->jiffy ? SCPU_SW_JIFFY : 0)));
}

// ---- settings ------------------------------------------------------------------------------------------------------------------
#define KEY_SPEED  "SuperCPU speed"
#define KEY_JIFFY  "JiffyDOS"
static const char *const speed_names[] = { "20 MHz", "1 MHz" };
static const char *const jiffy_names[] = { "Off", "On" };
static const char *const joy_names[]   = { "Normal", "Swapped" };
#define KEY_SYSMODE "System Mode"
static const char *const sysmode_names[] = { "PAL", "NTSC", "PAL-60", "NTSC-50" };
// C64_VIDEOFORMAT per mode, as in the C128 descriptor: the host's mode controller (host/mode_ctl.v) is the same file.
static const uint8_t sysmode_fmt[4] = { 0x00, 0x2B, 0x2A, 0x01 };

static const Setting settings[4 + CBM1541_NSETTINGS] = {
    { KEY_SPEED, speed_names, 2, 0, WHEN_LIVE, "c64scpu.set_speed", 0 },
    { KEY_JIFFY, jiffy_names, 2, 0, WHEN_LIVE, "c64scpu.set_jiffy", 0 },
    { "Joystick ports", joy_names, 2, 0, WHEN_LIVE, "c64scpu.set_joy", 0 },
    { KEY_SYSMODE, sysmode_names, 4, 0, WHEN_LIVE, "c64scpu.set_sysmode", 0 },
    CBM1541_SETTINGS,
};

// The speed setting is the cartridge's switch: "20 MHz" is the switch at the fast position (SCPU_SW b0 = 1).
static int op_set_speed(CoreCtx *c, const OpArgs &a, OpOut &)
{
    scpu_state(c)->fast = a.i(0) == 0 ? 1 : 0;
    write_sw(c);
    return OPE_OK;
}
static const ArgSpec bit_arg[] = { ARG_INT("value", 0, 1) };

static void apply_drive_rom(CoreCtx *c, char *msg, int msglen)
{
    ScpuState *s = scpu_state(c);
    drive_pick_apply(c, s->drv, scpu_drive, s->jiffy != 0, msg, msglen);
    drive_pick_apply(c, s->drv81, scpu_drive81, s->jiffy != 0, 0, 0);
}

static int op_set_jiffy(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    scpu_state(c)->jiffy = a.i(0) ? 1 : 0;
    write_sw(c);                                        // the switch bit follows the setting whether or not the file is there
    apply_drive_rom(c, o.msg, o.msglen);
    return OPE_OK;
}

static int op_set_joy(CoreCtx *c, const OpArgs &a, OpOut &)
{
    c->io->wr(SCPU_JOY, (uint8_t)(a.i(0) ? 1 : 0));
    return OPE_OK;
}

static int op_set_sysmode(CoreCtx *c, const OpArgs &a, OpOut &)
{
    int sys = a.i(0);
    picture_set_system(c, sys);
    c->io->video_format(sysmode_fmt[sys & 3]);
    return OPE_OK;
}
static const ArgSpec sysmode_arg[] = { ARG_INT("value", 0, 3) };

// ---- the ROMs -----------------------------------------------------------------------------------------------------------------
// The SuperCPU ROM is accepted when its size is 64, 128 or 256 KiB, the 16 bit reset vector at file offset 0xFFFC points into $8000-$FFFF (the boot map
// shows file bytes 0x8000-0xFFFF there) and the image is neither all 0x00 nor all 0xFF. The text for the person says which of these failed.
static const char *scpu_rom_fault(const uint8_t *d, uint32_t n)
{
    if (n != 65536u && n != 131072u && n != SCPU_SROM_MAX) {
        return "its size is not 65536, 131072 or 262144 bytes";
    }
    uint32_t vec = (uint32_t)d[0xFFFC] | ((uint32_t)d[0xFFFD] << 8);
    if (vec < 0x8000u) {
        return "its reset vector (file offset 0xFFFC) is below $8000";
    }
    bool zero = true, ff = true;
    for (uint32_t i = 0; i < n; i++) {
        zero = zero && d[i] == 0x00;
        ff = ff && d[i] == 0xFF;
    }
    if (zero || ff) {
        return "it is empty (all 0x00 or all 0xFF)";
    }
    return 0;
}
static bool scpu_rom_check(const uint8_t *d, uint32_t n) { return scpu_rom_fault(d, n) == 0; }

static uint32_t rd32(CoreCtx *c, uint32_t r)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)c->io->rd(r + (uint32_t)i) << (8 * i);
    }
    return v;
}

// ROM_CTRL.b0 clears the valid bits of the line caches (a line fetched before is stale): the ROM images' and the 65816's (T250 D3). The busy
// bit waited out is the C64 image cache's walker; the 65816's cache clears in 2048 clocks, before the next request can come.
static int flush_cache(CoreCtx *c, const char *role, char *msg, int msglen)
{
    c->io->wr(SCPU_ROM_CTRL, 0x01);
    uint32_t t0 = c->io->now_ms();
    while (c->io->rd(SCPU_ROM_CTRL) & 0x01) {
        if (c->io->now_ms() - t0 > 100) {
            snprintf(msg, msglen, "%s: the cache flush did not finish", role);
            return 1;
        }
        c->io->delay_ms(1);
    }
    return 0;
}

// Bytes into the DDR2 window at `off`, read back and compared (the same bytes or no start).
static int ddr_put(CoreCtx *c, uint32_t off, const uint8_t *data, uint32_t n, const char *role, char *msg, int msglen)
{
    c->io->ddr_write(off, data, n);
    static uint8_t back[4096];
    for (uint32_t o = 0; o < n; o += sizeof(back)) {
        uint32_t m = n - o < sizeof(back) ? n - o : (uint32_t)sizeof(back);
        c->io->ddr_read(off + o, back, m);
        if (memcmp(back, data + o, m) != 0) {
            snprintf(msg, msglen, "%s: the DDR2 did not take the image (read-back differs at +0x%X)", role, (unsigned)o);
            return 1;
        }
    }
    return 0;
}

static int put_srom(CoreCtx *c, const uint8_t *data, uint32_t n, char *msg, int msglen)
{
    const char *why = scpu_rom_fault(data, n);
    if (why) {
        snprintf(msg, msglen, "SuperCPU ROM: %s", why);
        return 1;
    }
    uint32_t base = rd32(c, SCPU_SROM_BASE);
    if (base != SCPU_SROM_DDR_ADDR) {
        snprintf(msg, msglen, "SuperCPU ROM: the core keeps its ROM at 0x%08X, this application writes 0x%08X (version mismatch)",
                 (unsigned)base, (unsigned)SCPU_SROM_DDR_ADDR);
        return 1;
    }
    int log2n = 0;
    while ((1u << log2n) < n) {
        log2n++;
    }
    c->io->wr(SCPU_ROM_VALID, 0);                       // the 65816 stays in reset from here until the image is complete and checked
    c->io->wr(SCPU_SROM_LOG2, (uint8_t)log2n);
    if (ddr_put(c, SCPU_SROM_WIN_OFF, data, n, "SuperCPU ROM", msg, msglen) || flush_cache(c, "SuperCPU ROM", msg, msglen)) {
        return 1;
    }
    c->io->wr(SCPU_ROM_VALID, 1);
    if (!(c->io->rd(SCPU_ROM_VALID) & 1)) {
        snprintf(msg, msglen, "SuperCPU ROM: the core did not take the image as valid");
        return 1;
    }
    return 0;
}

static int op_put_srom(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_srom(c, a.p(0), a.len(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

static int put_c64rom(CoreCtx *c, const OpArgs &a, uint32_t off, uint32_t size, const char *role, OpOut &o)
{
    if (a.len(0) != size) {
        snprintf(o.msg, o.msglen, "%s: %u bytes, the machine takes %u", role, (unsigned)a.len(0), (unsigned)size);
        return OPE_FAILED;
    }
    uint32_t base = rd32(c, SCPU_ROM_BASE);
    if (base != SCPU_ROM_DDR_ADDR) {
        snprintf(o.msg, o.msglen, "%s: the core keeps its ROM at 0x%08X, this application writes 0x%08X (version mismatch)",
                 role, (unsigned)base, (unsigned)SCPU_ROM_DDR_ADDR);
        return OPE_FAILED;
    }
    if (ddr_put(c, SCPU_ROM_WIN_OFF + off, a.p(0), size, role, o.msg, o.msglen) || flush_cache(c, role, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    return OPE_OK;
}
static int op_put_basic(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_c64rom(c, a, SCPU_BASIC_OFF, SCPU_BASIC_BYTES, "BASIC", o); }
static int op_put_kernal(CoreCtx *c, const OpArgs &a, OpOut &o) { return put_c64rom(c, a, SCPU_KERNAL_OFF, SCPU_KERNAL_BYTES, "KERNAL", o); }

// The character ROM goes into the VIC's own store (CHR_ADDR / CHR_DATA), written and read back.
static int op_put_char(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (a.len(0) != SCPU_CHR_BYTES) {
        snprintf(o.msg, o.msglen, "CHAR: %u bytes, the machine takes %u", (unsigned)a.len(0), (unsigned)SCPU_CHR_BYTES);
        return OPE_FAILED;
    }
    const uint8_t *chr = a.p(0);
    c->io->wr(SCPU_CHR_ADDR, 0);
    c->io->wr(SCPU_CHR_ADDR + 1, 0);
    for (uint32_t i = 0; i < SCPU_CHR_BYTES; i++) {
        c->io->wr(SCPU_CHR_DATA, chr[i]);
    }
    c->io->wr(SCPU_CHR_ADDR, 0);
    c->io->wr(SCPU_CHR_ADDR + 1, 0);
    for (uint32_t i = 0; i < SCPU_CHR_BYTES; i++) {
        if (c->io->rd(SCPU_CHR_DATA) != chr[i]) {
            snprintf(o.msg, o.msglen, "CHAR: the store did not take the image (read-back differs at +0x%X)", (unsigned)i);
            return OPE_FAILED;
        }
    }
    return OPE_OK;
}

// The drive ROMs: kept, then the drive gets the one the JiffyDOS setting chooses.
static int keep_drive_rom(CoreCtx *c, const uint8_t *data, uint32_t n, bool jiffy, OpOut &o)
{
    return drive_pick_keep(c, scpu_state(c)->drv, scpu_drive, data, n, jiffy, scpu_state(c)->jiffy != 0, o);
}
static int op_put_drive_rom(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive_rom(c, a.p(0), a.len(0), false, o); }
static int op_put_jiffy_rom(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive_rom(c, a.p(0), a.len(0), true, o); }
static int keep_drive_rom81(CoreCtx *c, const uint8_t *data, uint32_t n, bool jiffy, OpOut &o)
{
    return drive_pick_keep(c, scpu_state(c)->drv81, scpu_drive81, data, n, jiffy, scpu_state(c)->jiffy != 0, o);
}
static int op_put_drive_rom81(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive_rom81(c, a.p(0), a.len(0), false, o); }
static int op_put_jiffy_rom81(CoreCtx *c, const OpArgs &a, OpOut &o) { return keep_drive_rom81(c, a.p(0), a.len(0), true, o); }
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// The 1541 ROM of a file picked in the browser goes the same way as the one of the start (the library's own operation would bypass the choice).
static int op_set_drive_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return drive_pick_keep_file(c, scpu_state(c)->drv, scpu_drive, a.s(0), scpu_state(c)->jiffy != 0, o);
}
static const ArgSpec path_arg[] = { ARG_PATH("path") };

// The names of the C64 ROMs are those of the C64 core's set (Gideon's /flash/roms first); the SuperCPU ROM's are the coordinator's reading of what
// VICE and the dumps call it (T182 T7).
static const RomSpec roms[8] = {
    { "SuperCPU ROM", { "scpu64.rom", "scpu.rom", "supercpu.rom", "scpu64-v2.04.rom", 0 }, { 131072, 65536, 262144 }, true, scpu_rom_check, "c64scpu.put_srom" },
    { "BASIC", { "basic-901226-01.bin", "basic.bin", "basic.901226-01", "basic.901226-01.bin", "basic.rom", 0 }, { SCPU_BASIC_BYTES, 0 }, true, 0, "c64scpu.put_basic" },
    { "KERNAL", { "kernal-901227-03.bin", "kernal.bin", "kernal.901227-03", "kernal.901227-03.bin", "kernal.rom", 0 }, { SCPU_KERNAL_BYTES, 0 }, true, 0, "c64scpu.put_kernal" },
    { "CHAR", { "chargen-901225-01.bin", "chars.bin", "characters.901225-01", "characters.901225-01.bin", "chargen.bin", "chars.rom", 0 }, { SCPU_CHR_BYTES, 0 }, true, 0, "c64scpu.put_char" },
    { "1541 drive", { "1541.rom", "1541.bin", "1541-ii.bin", "1541-ii.rom", "1541ii.rom", "1541c.rom", "dos1541.rom", "dos1541-325302-01+901229-05.bin", 0 },
      { 16384, 32768, 0 }, false, 0, "c64scpu.put_drive_rom", 0, true },
    { "JiffyDOS 1541 ROM", { "jiffydos-1541.bin", "jiffydos1541.bin", "jiffydos_1541.bin", "jiffydos-1541-ii.bin", "jiffydos1541-ii.bin", "jiffydos.bin", 0 },
      { 16384, 32768, 0 }, false, 0, "c64scpu.put_jiffy_rom" },
    { "1581 drive", { "1581.rom", "1581.bin", "dos1581.rom", 0 }, { 32768, 0 }, false, 0, "c64scpu.put_drive_rom1581", 0 },
    { "JiffyDOS 1581 ROM", { "jiffydos-1581.bin", "jiffydos1581.bin", "jiffydos_1581.bin", "jiffydos-1581.rom", 0 },
      { 32768, 0 }, false, 0, "c64scpu.put_jiffy_rom1581" },
};

// ---- control -------------------------------------------------------------------------------------------------------------------
static int c64scpu_init(CoreCtx *c, char *, int)
{
    cbm_run_clear(&scpu_state(c)->run);                 // a reset ends a typed start that was still waiting for READY
    c->io->mode_reset(true);
    return 0;
}

// The gate (D2, S2): the machine is released only when the core says the SuperCPU ROM is in (ROM_VALID). The start does not get here without the file;
// this is the second lock, for a core that lost the image or a version that does not keep the bit.
static int c64scpu_release(CoreCtx *c, char *msg, int msglen)
{
    if (!(c->io->rd(SCPU_ROM_VALID) & 1)) {
        snprintf(msg, msglen, "SuperCPU ROM is not in the core: the machine stays held");
        printf("[c64scpu] %s\n", msg);
        return 1;
    }
    write_sw(c);
    c->io->mode_reset(false);
    return 0;
}

// The cold start (the core's COLD line): every register of the machine to its power-on value, the switch byte too; the restart writes the settings again.
static int c64scpu_cold_start(CoreCtx *c, char *msg, int msglen)
{
    c->io->wr(SCPU_COLD, 0x01);
    uint32_t t0 = c->io->now_ms();
    for (;;) {
        c->io->delay_ms(2);
        if (!(c->io->rd(SCPU_COLD) & 0x01)) {
            break;
        }
        if (c->io->now_ms() - t0 > SCPU_COLD_MAX_MS) {
            snprintf(msg, msglen, "C64SCPU cold start: the core did not finish it in %d ms", SCPU_COLD_MAX_MS);
            return 1;
        }
    }
    return 0;
}

// T206: the machine the typed start waits for is the C64's (the SuperCPU changes the CPU, not the KERNAL): BASIC at $0801, TXTTAB $2B, the keyboard
// buffer at $0277 with its count at $C6, the C64's own numbers. ready_ms is the C64's 1000 ms; no board has measured it on this core yet.
// The typed start does NOT use the buffer: the 65816 reads bank 0 from the SuperCPU's own SRAM (DDR2 0x03000000); the C64 RAM the application's RAM window writes
// is only ever the mirror of CPU writes to the pages $02-$FF (scpu_mem.v mir_tab, one way), so $C6 and $2B (page 0) never show there. A program and its pointers are
// written into the SRAM itself (c64scpu_prg.h mem_ddr, T217). m.screen switches the job to
// the key matrix (cbm_run.h): the keys go in as a person's, READY is read from the screen at $0400 (page 4, mirrored). Alex 2026-11-12, T206 decision A.
static CbmRunMachine machine(void)
{
    CbmRunMachine m;
    memset(&m, 0, sizeof(m));
    m.tag = "c64scpu";
    scpu_prg_params(&m.prg);                            // T217: the program goes into the 65816's own SRAM (c64scpu_prg.h; page 0 and the BASIC pointers are not in the C64 RAM window)
    m.mem_flush_reg = SCPU_ROM_CTRL;
    m.ready_ms = 1000;
    m.settle_ms = 300;
    m.giveup_ms = 4000;
    m.screen = 0x0400;
    m.key_hold_ms = SCPU_KEY_HOLD_MS;
    m.key_gap_ms = SCPU_KEY_GAP_MS;
    return m;
}

static int op_type(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue_script(c, &scpu_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static const ArgSpec text_arg[] = { ARG_STRING("text", 63) };

// T206: Mount & Run of a PRG inside a disk image reads the file out of the image and runs it (the C64's prg.run): reset, wait for READY, place, RUN or SYS
static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue(c, &scpu_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// T217: "Load" of a .prg: the bytes into the 65816's SRAM as the machine is now (BASIC relinked to $2B's value, its pointers follow), nothing typed
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_load_now(c, &scpu_state(c)->run, machine(), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int rc = cbm_run_service(c, &scpu_state(c)->run, machine());
    o.more = cbm1541_service(c);
    return rc;
}

// ---- menu ----------------------------------------------------------------------------------------------------------------------
static const OpBind file_bind[] = { B_FILE("path") };
static const FileAction rom_actions[] = {
    { "Set as 1541 ROM", "c64scpu.set_drive_rom", file_bind, 1, false, 0, 0 },
};
// T206: the disk actions are the library's (cbm1541_desc.h): the same names and order as in every core with a drive
static const FileAction prg_actions[] = {
    { "Load", "c64scpu.load_prg", file_bind, 1, false, 0, 0 },
    { "Load + run", "c64scpu.run_prg", file_bind, 1, true, 0, 0 },
};
static const FileType filetypes[6] = {
    { ".rom", rom_actions, 1, 0, 0, 0 },
    { ".prg", prg_actions, 2, 0, 0, 0 },
    { ".p00", prg_actions, 2, 0, 0, 0 },
    { ".d64", c1541_d64_actions, 5, ".prg", c1541_inner_actions, 2 },     // the files inside: Mount & Run (run_op) and Real Run (type_op)
    { ".g64", c1541_g64_actions, 4, 0, 0, 0 },
    { ".d81", c1581_disk_actions, 4, ".prg", c1541_inner_actions, 2 },
};

static const Task tasks[2 + CBM1541_NTASKS + CBM1581_NTASKS] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Reload ROMs, reset", "machine.reload_roms", 0, 0, true },
    CBM1541_TASKS,
    CBM1581_TASKS,
};

static const MediaSlot media[] = {
    CBM1541_1581_MEDIA,
};

// T250 D2, D3: machine.read_mem / machine.write_mem. The 65816's stores where scpu_mem.v puts them in the DDR2, and the C64 RAM above
// them through the RAM window (the RAM under the I/O). The ROM banks $F8-$FF are not mapped (T250 D2). Bank 0 is the SRAM store, where
// the CPU writes: in the normal configuration it reads $A000-$BFFF and $E000-$FFFF from bank 1 (the BASIC and KERNAL copies) and
// $D000-$DFFF from the I/O, so a read of bank 0 there gives the RAM below. A write here does not reach the C64 RAM (the CPU's own writes
// are mirrored there, these are not): to change a byte both sides see, write both addresses. A write into the 65816's RAM goes past the
// CPU's cache, so the cache is flushed after it; reads need nothing, the cache writes through.
static const MemRegion scpu_mem_regions[] = {
    { 0x000000u, 0x020000u, MK_DDR, SCPU_SRAM_WIN_OFF, 0, "SRAM (banks 0 and 1)" },
    { 0x020000u, 0xF40000u, MK_DDR, SCPU_SUPERRAM_WIN_OFF + 0x020000u, 0, "SuperRAM (banks $02-$F5)" },
    { 0xF60000u, 0x020000u, MK_DDR, SCPU_SUPERRAM_WIN_OFF, 0, "SuperRAM window (banks $F6-$F7)" },
    { 0x1000000u, 0x010000u, MK_RAM, 0, 0, "C64 RAM (at $1000000)" },
};
static const MemMap scpu_mem = { scpu_mem_regions, 4, true, 0 };

static int op_write_mem(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int e = mem_write(scpu_mem, c, a, o);
    if (e == OPE_OK && (uint32_t)a.i(0) < 0x1000000u && flush_cache(c, "machine.write_mem (the bytes are written)", o.msg, o.msglen)) return OPE_FAILED;
    return e;
}

static void c64scpu_register(OpRegistry &r)
{
    r.contributor("c64scpu");
    r.add_op("c64scpu.put_srom", rom_arg, 1, op_put_srom);
    r.add_op("c64scpu.put_basic", rom_arg, 1, op_put_basic);
    r.add_op("c64scpu.put_kernal", rom_arg, 1, op_put_kernal);
    r.add_op("c64scpu.put_char", rom_arg, 1, op_put_char);
    r.add_op("c64scpu.put_drive_rom", rom_arg, 1, op_put_drive_rom);
    r.add_op("c64scpu.put_jiffy_rom", rom_arg, 1, op_put_jiffy_rom);
    r.add_op("c64scpu.put_drive_rom1581", rom_arg, 1, op_put_drive_rom81);
    r.add_op("c64scpu.put_jiffy_rom1581", rom_arg, 1, op_put_jiffy_rom81);
    r.add_op("c64scpu.set_drive_rom", path_arg, 1, op_set_drive_rom);
    r.add_op("c64scpu.load_prg", path_arg, 1, op_load);
    r.add_op("c64scpu.set_speed", bit_arg, 1, op_set_speed);
    r.add_op("c64scpu.set_jiffy", bit_arg, 1, op_set_jiffy);
    r.add_op("c64scpu.set_joy", bit_arg, 1, op_set_joy);
    r.add_op("c64scpu.set_sysmode", sysmode_arg, 1, op_set_sysmode);
    r.add_op("c64scpu.service", 0, 0, op_service);
    r.add_op("c64scpu.type_script", text_arg, 1, op_type);
    r.add_op("c64scpu.run_prg", path_arg, 1, op_run);
    static const ArgSpec rd_[] = { ARG_INT("address", 0, 0x100FFFF), ARG_INT("length", 1, OP_DATA_MAX) };
    static const ArgSpec wr_[] = { ARG_INT("address", 0, 0x100FFFF), ARG_STRING("data", 254) };
    r.add_op("machine.read_mem", rd_, 2, mem_op_read<&scpu_mem>);
    r.add_op("machine.write_mem", wr_, 2, op_write_mem);
    cbm1541_register(r);
}

static const char *const caps[] = { "iec", "drive_a" };

extern const CoreDesc c64scpu_desc;
const CoreDesc c64scpu_desc = {
    { 'S', 'C', 'P', 'U' },
    "C64 with SuperCPU",
    "C64 with SuperCPU",
    "SCPU",
    "c64scpu",
    "C64SCPU Settings",
    1,
    roms, 8,
    settings, 4 + CBM1541_NSETTINGS,
    tasks, 2 + CBM1541_NTASKS + CBM1581_NTASKS,
    filetypes, 6,
    media, 1,
    // the picture goes through the host's scaler (the VIC-II), as the C64's: no buffers, no registers
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { SCPU_STATUS, SCPU_ST_HELD, 200, false },
    c64scpu_init, c64scpu_release, 0, 50,
    "c64scpu.service", 20, false,
    0, 0,
    c64scpu_register,
    caps, 2,
    false, false, 0, 0, 0, 0, 0, 0, 0,
    "c64scpu.type_script",                              // type_op (T206: Run Disk)
    "c64scpu.run_prg",                                  // run_op (T206: Mount & Run of a file inside a disk image)
    65536,                                              // cold_ram_bytes: the RAM block the RAM window reaches (SRAM and SuperRAM are not cleared by the restart yet)
    c64scpu_cold_start, 0, 0, 0,                        // cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0,                                               // menu_rec, menu_rec_n
    0,                                                  // core_id
    0, 0,                                               // report_regs, n
    0, false, 0, 0, false,                              // rawkeys, own_volume, pad, live_ms, menu_first
    cbm1541_media_flush, cbm1541_media_pending,         // T180 D4: the drive's write-back
    &cbm1541_mount_slots,                               // T180 D2, D5: Drive A
    &hk_cbm,                               // T181: the default hotkey map (hk_defaults.h)
    0, false, 0,                                     // alt_video, no_tone, rom_key
    "SuperCPU speed",                          // T212 D13: machine_info
};
