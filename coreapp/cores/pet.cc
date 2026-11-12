// The Commodore PET descriptor (T134, spec docs/specs/T134-pet-core.md D8 and Q10-Q12): the ROM rows with VICE's file names, model / CPU speed /
// Diag / picture rate / phosphor as register settings, the .prg actions "Load" and "Load + run" through the PRG library (cbm_run.cc), the picture:
// four 640 x 400 buffers the core's frame writer fills, shown 2 x 2 on the pixel layer. The register block is cores/pet/rtl/trx/pet_regs.v.
#include "hk_defaults.h"
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "pet_regs.h"
#include "cbm_run.h"
#include "pet_models.h"
#include "romfind.h"
#include "cores.h"
#include "tape_deck.h"
#include <stdio.h>
#include <string.h>

struct PetDrive {
    CoreFile *file;
    uint32_t size;
    bool ro;
    char name[56];
    char path[FS_PATH_MAX];
};
struct PetState {
    CbmRun run;
    PetDrive drv[2];
    TapeDeck deck;
};
// slot 2 of mount_slots: Drive 8, Drive 8.1, Tape; the PET's tapes are Commodore .tap
static const TapeRegs tape_regs = { PET_TAPE_CTRL, PET_TAPE_BASE, PET_TAPE_POS, PET_TAPE_SEEK, PET_TAPE_STATUS, PET_W_TAPE, 2, 1 };

static PetState *pet_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new PetState();
        memset(c->state, 0, sizeof(PetState));
    }
    return (PetState *)c->state;
}

// ---- settings ------------------------------------------------------------------------------------------------------------------
// The SuperPET (model 5) is an 8032 plus its board (6809, 64 KiB banked RAM at $9000, Waterloo ROMs, dongle, ACIA; T134 Q13): the setting "SuperPET CPU" picks the processor.
static const char *const model_names[] = { "2001", "3032", "4032", "8032", "8096", "SuperPET" };
static const char *const ram_names[]   = { "8 KiB", "16 KiB", "32 KiB" };
static const char *const cpu_names[]   = { "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8" };
static const char *const diag_names[]  = { "Off", "On (DIAG jumper)" };
static const char *const hz_names[]    = { "60 Hz", "50 Hz" };
static const char *const phos_names[]  = { "Green", "Amber", "White" };
static const RegBind b_model = { PET_MODEL, 0, 7, 0 };
static const RegBind b_cpu   = { PET_CPU_X, 0, 7, 0 };
static const RegBind b_diag  = { PET_DIAG, 0, 1, 0 };
static const RegBind b_hz    = { PET_HZ, 0, 1, 0 };
static const RegBind b_phos  = { PET_PHOSPHOR, 0, 3, 0 };
static const RegBind b_ram   = { PET_RAMSZ, 0, 3, 0 };
static const char *const drive_names[] = { "Off", "4040 (D64)", "8250 (D80, D82)" };
static const uint8_t drive_bytes[3] = { 0, 3, 1 };     // PET_DRV_CTL: b0 device 8 on, b1 4040
static const RegBind b_drive = { PET_DRV_CTL, 0, 3, drive_bytes };
static const char *const dset_names[] = { "Off (file only)", "External datasette" };
static const uint8_t dset_bytes[2] = { 0x00, 0x11 };    // PET_TAPE_PIN: b0 motor pin driven, b4 READ / SENSE pins are the PET's inputs (provisional until the board session of 2026-11-14)
static const RegBind b_dset = { PET_TAPE_PIN, 0, 0x1F, dset_bytes };
static const char *const spcpu_names[] = { "6502", "6809", "Program ($EFF8)" };   // PROG: the 6502 starts and hands over by writing $EFF8 b0 = 0
static const RegBind b_spcpu = { PET_SP_CPU, 0, 3, 0 };

// The setting indices pet_model() and pet_hz50() read (the order of the table below).
enum { PET_S_MODEL = 0, PET_S_HZ = 3, PET_S_DRIVE = 6, PET_S_SPCPU = 8 };
static const Setting settings[9] = {
    { "Model (at next reset)", model_names, 6, 2, WHEN_RESET, 0, &b_model, 0, 0, 0, 0, "Model" },
    { "CPU speed",             cpu_names,   8, 0, WHEN_LIVE,  0, &b_cpu,   0, 0, 0, 0, 0 },
    { "Diag (at next reset)",  diag_names,  2, 0, WHEN_RESET, 0, &b_diag,  0, 0, 0, 0, "Diag" },
    { "Picture rate",          hz_names,    2, 0, WHEN_LIVE,  0, &b_hz,    0, 0, 0, 0, 0 },
    { "Phosphor",              phos_names,  3, 0, WHEN_LIVE,  0, &b_phos,  0, 0, 0, 0, 0 },
    { "RAM (at next reset)",   ram_names,   3, 2, WHEN_RESET, 0, &b_ram,   0, 0, 0, 0, "RAM" },   // the CRTC models always have 32 KiB
    { "Drive 8 (at next reset)", drive_names, 3, 0, WHEN_RESET, 0, &b_drive, 0, 0, 0, 0, "Drive" },   // the dual unit of ieee_drive: drives 0 and 1
    { "Datasette port",        dset_names,  2, 0, WHEN_LIVE,  0, &b_dset,  0, 0, 0, 0, 0 },
    { "SuperPET CPU (at next reset)", spcpu_names, 3, 0, WHEN_RESET, 0, &b_spcpu, 0, 0, 0, 0, "SPCPU" },   // only the SuperPET model looks at it; default 6502 as in VICE
};
static int pet_model(CoreCtx *c) { return c->settings->value(PET_S_MODEL); }
static bool pet_hz50(CoreCtx *c) { return c->settings->value(PET_S_HZ) == 1; }

// ---- ROMs ----------------------------------------------------------------------------------------------------------------------
// The staging buffers live on the heap for the duration of one call: as static arrays they were 60 KiB of .bss, and the core app's .bss is full (T134 merged with T206/T205).
struct Scratch {
    uint8_t *p;
    uint32_t n;
    explicit Scratch(uint32_t size) : p(new uint8_t[size]), n(size) {}
    ~Scratch() { delete[] p; }
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;
    operator uint8_t *() { return p; }
};

// A ROM goes to its machine address: BASIC ends at $DFFF (8 KB BASIC 1/2 from $C000, 12 KB BASIC 4 from $B000), the editor is 2 KB at $E000, the
// KERNAL 4 KB at $F000. The aperture shows one 16 KB ROM space at a time (ROM_SEL); a ROM across the $C000 edge is written in two pieces.
static int put_space(CoreCtx *c, int sel, uint32_t off, const uint8_t *data, uint32_t n, const char *role, char *msg, int msglen)
{
    Scratch back(PET_ROM_APERTURE);
    c->io->wr(PET_ROM_SEL, (uint8_t)sel);
    if (c->io->rd(PET_ROM_SEL) != sel) {
        snprintf(msg, msglen, "PET ROMs: the core did not take ROM_SEL for the %s", role);
        return 1;
    }
    c->io->rom_write(off, data, n);
    c->io->rom_read(off, back, n);
    if (memcmp(back, data, n) != 0) {
        snprintf(msg, msglen, "PET ROMs: the core did not take the %s (read-back differs)", role);
        return 1;
    }
    return 0;
}

static int put_at(CoreCtx *c, uint32_t addr, const uint8_t *data, uint32_t n, const char *role, char *msg, int msglen)
{
    uint32_t first = n;
    if (addr < 0xC000 && addr + n > 0xC000) {
        first = 0xC000 - addr;
    }
    if (put_space(c, addr < 0xC000 ? PET_SPACE_8000 : PET_SPACE_C000, addr & 0x3FFF, data, first, role, msg, msglen)) {
        return 1;
    }
    return first < n ? put_space(c, PET_SPACE_C000, 0, data + first, n - first, role, msg, msglen) : 0;
}

// The ROM of a role is the one the MODEL names (pet_models.h): the scan finds a file of any model, so here the model's own file is looked up again
// (the same folders as the scan) and a missing one is told by name. A 4 KiB character file gives its first 2 KiB.
static int put_model_rom(CoreCtx *c, int role, uint32_t want, const char *label, OpOut &o)
{
    Scratch buf(12288);
    const char *names[6];
    pet_rom_names(pet_model(c), pet_hz50(c), role, names);
    RomSpec s;
    memset(&s, 0, sizeof(s));
    s.role = label;
    for (int i = 0; names[i] && i < ROM_NAMES_MAX; i++) {
        s.names[i] = names[i];
    }
    s.sizes[0] = want;
    if (role == PET_R_CHAR) {
        s.sizes[1] = 2 * PET_CHAR_BYTES;
    }
    s.required = true;
    s.put_op = "";
    char own[96], sum[160];
    const char *own_dir = rom_own_dir(c->cor_path, own, sizeof(own)) ? own : 0;
    RomResult res[1];
    if (rom_scan(c->fs, c->desc->folder, &s, 1, res, sum, sizeof(sum), c->board ? c->board->rom_dir : 0, own_dir) != ROM_FOUND ||
        res[0].hit.size > buf.n || !rom_read(c->fs, &res[0].hit, buf)) {
        snprintf(o.msg, o.msglen, "%s for the %s: %s", label, model_names[pet_model(c)], names[0]);
        return OPE_FAILED;
    }
    uint32_t n = role == PET_R_CHAR ? PET_CHAR_BYTES : want;
    printf("[core] %s for the %s: %s\n", label, model_names[pet_model(c)], res[0].hit.name);
    if (role == PET_R_BASIC) {
        return put_at(c, 0xE000 - n, buf, n, "BASIC", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
    }
    if (role == PET_R_CHAR) {
        return put_space(c, PET_SPACE_CHAR, 0, buf, n, "character ROM", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
    }
    return put_at(c, role == PET_R_EDITOR ? 0xE000 : 0xF000, buf, n, label, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_put_basic(CoreCtx *c, const OpArgs &, OpOut &o)
{
    return put_model_rom(c, PET_R_BASIC, pet_basic_gen(pet_model(c)) == 4 ? 12288 : 8192, "BASIC", o);
}
static int op_put_editor(CoreCtx *c, const OpArgs &, OpOut &o) { return put_model_rom(c, PET_R_EDITOR, 2048, "Editor", o); }
static int op_put_kernal(CoreCtx *c, const OpArgs &, OpOut &o) { return put_model_rom(c, PET_R_KERNAL, 4096, "KERNAL", o); }
static int op_put_char(CoreCtx *c, const OpArgs &, OpOut &o) { return put_model_rom(c, PET_R_CHAR, PET_CHAR_BYTES, "Characters", o); }
static const ArgSpec rom_arg[] = { ARG_BLOB("data") };

// VICE's file names first (Alex 2026-11-10, Q12: "VICE is on the disk, take it from there"), then the generic ones. The scan only has to see that a
// file of the role is there; which one goes into the machine is the model's (put_model_rom, pet_models.h).
static const RomSpec roms[14] = {
    { "BASIC", { "basic-4.901465-23-20-21.bin", "basic-2.901465-01-02.bin", "basic-1.901439-09-05-02-06.bin", "basic4.bin", "basic2.bin", "basic.bin", "basic", 0 },
      { 8192, 12288, 0 }, true, 0, "pet.put_basic" },
    { "Editor", { "edit-4-40-n-60Hz.901499-01.bin", "edit-4-40-n-50Hz.901498-01.bin", "edit-4-40-b-60Hz.ts.bin", "edit-4-80-b-60Hz.901474-03.bin",
                  "edit-2-n.901447-24.bin", "edit-2-b.901474-01.bin", "edit-1-n.901439-03.bin", "edit.bin", 0 },
      { 2048, 0 }, true, 0, "pet.put_editor" },
    { "KERNAL", { "kernal-4.901465-22.bin", "kernal-2.901465-03.bin", "kernal-1.901439-04-07.bin", "kernal.bin", "kernal", 0 },
      { 4096, 0 }, true, 0, "pet.put_kernal" },
    { "Characters", { "characters.901640-01.bin", "characters-2.901447-10.bin", "characters-1.901447-08.bin", "chargen.bin", "chargen", "characters.bin", 0 },
      { PET_CHAR_BYTES, 2 * PET_CHAR_BYTES, 0 }, true, 0, "pet.put_char" },
    // the disk drive's (optional: the drive setting names the pair that is put)
    { "4040 DOS", { "dos4040-901468-14-16.bin", "dos4040.bin", 0 }, { 12288, 0 }, false, 0, "pet.put_dos4040" },
    { "4040 controller", { "ctl4040.bin", "4040-controller.bin", 0 }, { 2048, 0 }, false, 0, "pet.put_ctl4040" },
    { "8250 DOS", { "dos1001-901887+8-01.bin", "dos8250.bin", "dos1001", "dos1001.bin", 0 }, { 16384, 0 }, false, 0, "pet.put_dos8250" },
    { "8250 controller", { "ctl8250.bin", "8250-controller.bin", 0 }, { 2048, 0 }, false, 0, "pet.put_ctl8250" },
    // the SuperPET's 6809 side (optional: the 6502 runs without them), VICE's names
    { "Waterloo A000", { "waterloo-a000.901898-01.bin", 0 }, { 4096, 0 }, false, 0, "pet.put_wl_a" },
    { "Waterloo B000", { "waterloo-b000.901898-02.bin", 0 }, { 4096, 0 }, false, 0, "pet.put_wl_b" },
    { "Waterloo C000", { "waterloo-c000.901898-03.bin", 0 }, { 4096, 0 }, false, 0, "pet.put_wl_c" },
    { "Waterloo D000", { "waterloo-d000.901898-04.bin", 0 }, { 4096, 0 }, false, 0, "pet.put_wl_d" },
    { "Waterloo E000", { "waterloo-e000.901897-01.bin", 0 }, { 4096, 2048, 0 }, false, 0, "pet.put_wl_e" },
    { "Waterloo F000", { "waterloo-f000.901898-05.bin", 0 }, { 4096, 0 }, false, 0, "pet.put_wl_f" },
};

// ---- the disk drive (T134 D6, Q4): the dual unit 4040 / 8250 of cores/pet/rtl/ieee_drive ------------------------------------------
// The DOS and controller ROMs go through the aperture (ROM_SEL 4/5 for the 4040, 6/7 for the 8250; write only). VICE has the DOS files; the controller
// ROM (2 KB) has no VICE file, its names are the MiSTer ones. The disk image lives in the application's DDR2 window (slot 0 / 1, PET_DRV_IMG0/1): the core's
// block service reads and writes it, the application fills it at the mount and takes it back into the file when the core says dirty.
enum { PET_DRV_OFF = 0, PET_DRV_4040 = 1, PET_DRV_8250 = 2 };
static int put_drive_rom(CoreCtx *c, int type, const char *label, const char *const *names, uint32_t want, int sel, uint32_t off, OpOut &o)
{
    Scratch buf(16384);
    if (c->settings->value(PET_S_DRIVE) != type) {
        return OPE_OK;                                  // the other drive type's ROM, or no drive: nothing to put
    }
    RomSpec s;
    memset(&s, 0, sizeof(s));
    s.role = label;
    for (int i = 0; names[i] && i < ROM_NAMES_MAX; i++) {
        s.names[i] = names[i];
    }
    s.sizes[0] = want;
    s.required = false;
    s.put_op = "";
    char own[96], sum[160];
    const char *own_dir = rom_own_dir(c->cor_path, own, sizeof(own)) ? own : 0;
    RomResult res[1];
    if (rom_scan(c->fs, c->desc->folder, &s, 1, res, sum, sizeof(sum), c->board ? c->board->rom_dir : 0, own_dir) != ROM_FOUND ||
        res[0].hit.size != want || !rom_read(c->fs, &res[0].hit, buf)) {
        snprintf(o.msg, o.msglen, "%s for the disk drive: %s", label, names[0]);
        return OPE_FAILED;
    }
    c->io->wr(PET_ROM_SEL, (uint8_t)sel);
    if (c->io->rd(PET_ROM_SEL) != sel) {
        snprintf(o.msg, o.msglen, "PET ROMs: the core did not take ROM_SEL for the %s", label);
        return OPE_FAILED;
    }
    c->io->rom_write(off, buf, want);
    printf("[core] %s: %s\n", label, res[0].hit.name);
    return OPE_OK;
}
static const char *const names_dos4040[] = { "dos4040-901468-14-16.bin", "dos4040.bin", 0 };
static const char *const names_ctl4040[] = { "ctl4040.bin", "4040-controller.bin", 0 };
static const char *const names_dos8250[] = { "dos1001-901887+8-01.bin", "dos8250.bin", "dos1001", "dos1001.bin", 0 };
static const char *const names_ctl8250[] = { "ctl8250.bin", "8250-controller.bin", 0 };
static int op_put_dos4040(CoreCtx *c, const OpArgs &, OpOut &o) { return put_drive_rom(c, PET_DRV_4040, "4040 DOS", names_dos4040, 12288, 4, 0x1000, o); }
static int op_put_ctl4040(CoreCtx *c, const OpArgs &, OpOut &o) { return put_drive_rom(c, PET_DRV_4040, "4040 controller", names_ctl4040, 2048, 5, 0, o); }
static int op_put_dos8250(CoreCtx *c, const OpArgs &, OpOut &o) { return put_drive_rom(c, PET_DRV_8250, "8250 DOS", names_dos8250, 16384, 6, 0, o); }
static int op_put_ctl8250(CoreCtx *c, const OpArgs &, OpOut &o) { return put_drive_rom(c, PET_DRV_8250, "8250 controller", names_ctl8250, 2048, 7, 0, o); }

// The Waterloo ROMs (SuperPET only) go through the aperture too: ROM_SEL 3 is $A000-$DFFF (the files a, b, c, d at 0, $1000, $2000, $3000), ROM_SEL 8 is
// $E000-$FFFF (e at 0, f at $1000); the 6809 reads them from there. A file that is not on the card is an error only when the 6809 can run (the switch is
// not 6502); an E000 file of 2 KiB is the part below the I/O page.
static const char *const wl_names[6] = { "waterloo-a000.901898-01.bin", "waterloo-b000.901898-02.bin", "waterloo-c000.901898-03.bin",
                                         "waterloo-d000.901898-04.bin", "waterloo-e000.901897-01.bin", "waterloo-f000.901898-05.bin" };
static int put_waterloo(CoreCtx *c, int i, OpOut &o)
{
    Scratch buf(4096), back(4096);
    if (pet_model(c) != PET_M_SUPERPET) {
        return OPE_OK;
    }
    char label[24];
    snprintf(label, sizeof(label), "Waterloo %c000", 'A' + i);
    RomSpec s;
    memset(&s, 0, sizeof(s));
    s.role = label;
    s.names[0] = wl_names[i];
    s.sizes[0] = 4096;
    if (i == 4) {
        s.sizes[1] = 2048;
    }
    s.required = false;
    s.put_op = "";
    char own[96], sum[160];
    const char *own_dir = rom_own_dir(c->cor_path, own, sizeof(own)) ? own : 0;
    RomResult res[1];
    if (rom_scan(c->fs, c->desc->folder, &s, 1, res, sum, sizeof(sum), c->board ? c->board->rom_dir : 0, own_dir) != ROM_FOUND ||
        res[0].hit.size > buf.n || !rom_read(c->fs, &res[0].hit, buf)) {
        if (c->settings->value(PET_S_SPCPU) == 0) {
            return OPE_OK;
        }
        snprintf(o.msg, o.msglen, "%s for the SuperPET's 6809: %s", label, wl_names[i]);
        return OPE_FAILED;
    }
    uint32_t n = res[0].hit.size;
    int sel = i < 4 ? PET_SPACE_WL_A : PET_SPACE_WL_E;
    uint32_t off = i < 4 ? (uint32_t)i * 0x1000u : (i == 4 ? 0u : 0x1000u);
    c->io->wr(PET_ROM_SEL, (uint8_t)sel);
    if (c->io->rd(PET_ROM_SEL) != sel) {
        snprintf(o.msg, o.msglen, "PET ROMs: the core did not take ROM_SEL for the %s", label);
        return OPE_FAILED;
    }
    c->io->rom_write(off, buf, n);
    c->io->rom_read(off, back, n);
    if (memcmp(back, buf, n) != 0) {
        snprintf(o.msg, o.msglen, "PET ROMs: the core did not take the %s (read-back differs)", label);
        return OPE_FAILED;
    }
    printf("[core] %s: %s\n", label, res[0].hit.name);
    return OPE_OK;
}
static int op_put_wl_a(CoreCtx *c, const OpArgs &, OpOut &o) { return put_waterloo(c, 0, o); }
static int op_put_wl_b(CoreCtx *c, const OpArgs &, OpOut &o) { return put_waterloo(c, 1, o); }
static int op_put_wl_c(CoreCtx *c, const OpArgs &, OpOut &o) { return put_waterloo(c, 2, o); }
static int op_put_wl_d(CoreCtx *c, const OpArgs &, OpOut &o) { return put_waterloo(c, 3, o); }
static int op_put_wl_e(CoreCtx *c, const OpArgs &, OpOut &o) { return put_waterloo(c, 4, o); }
static int op_put_wl_f(CoreCtx *c, const OpArgs &, OpOut &o) { return put_waterloo(c, 5, o); }

static uint32_t drv_base(int d) { return d ? PET_DRV_IMG1 : PET_DRV_IMG0; }
static void wr32(CoreCtx *c, uint16_t reg, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        c->io->wr((uint32_t)reg + i, (uint8_t)(v >> (8 * i)));
    }
}

// What the image's size says it is: a D64 (35 tracks, 40 tracks, each with or without the error bytes) is the 4040's, a D80 / D82 the 8250's.
static int drv_kind(uint32_t n)
{
    if (n == 174848 || n == 175531 || n == 196608 || n == 197376) {
        return PET_DRV_4040;
    }
    if (n == 533248 || n == 1066496) {
        return PET_DRV_8250;
    }
    return PET_DRV_OFF;
}

// The image goes back to its file. The dirty flag is cleared first, so a sector the drive writes during the copy raises it again and is copied later.
static int drv_writeback(CoreCtx *c, int d, char *msg, int msglen)
{
    Scratch buf(4096);
    PetDrive &dr = pet_state(c)->drv[d];
    c->io->wr(PET_DRV_CLR, (uint8_t)(1u << d));
    for (uint32_t off = 0; off < dr.size; off += buf.n) {
        uint32_t n = dr.size - off < buf.n ? dr.size - off : (uint32_t)buf.n;
        c->io->ddr_read(drv_base(d) - 0x01000000u + off, buf, n);
        if (!dr.file->write(off, buf, n)) {
            snprintf(msg, msglen, "cannot write %s (the medium is gone or full?)", dr.name);
            return 1;
        }
    }
    if (!dr.file->sync()) {
        snprintf(msg, msglen, "cannot sync %s", dr.name);
        return 1;
    }
    return 0;
}

static int drv_eject(CoreCtx *c, int d, char *msg, int msglen)
{
    PetDrive &dr = pet_state(c)->drv[d];
    if (!dr.file) {
        return 0;
    }
    if ((c->io->rd(PET_DRV_ST) >> d) & 1) {
        if (drv_writeback(c, d, msg, msglen)) {
            return 1;                                   // the medium stays, as the slot contract says
        }
    }
    dr.size = 0;
    wr32(c, PET_DRV_SIZE0 + 4 * d, 0);
    c->io->wr(PET_DRV_MNT, (uint8_t)(1u << d));         // size 0: no disk
    c->fs->close(dr.file);
    dr.file = 0;
    dr.name[0] = 0;
    dr.path[0] = 0;
    mount_notify(c, d);
    return 0;
}

static int drv_mount(CoreCtx *c, int d, const char *path, char *msg, int msglen)
{
    Scratch buf(4096);
    PetState *st = pet_state(c);
    CoreFile *f = c->fs->open(path, true);
    bool ro = false;
    if (!f) {
        f = c->fs->open(path, false);
        ro = true;
    }
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    ro = ro || !f->writable();
    uint64_t n64 = f->size();
    uint32_t n = fs_size32(n64);
    int kind = drv_kind(n);
    if (kind == PET_DRV_OFF || n64 > PET_DRV_IMG_MAX) {
        c->fs->close(f);
        snprintf(msg, msglen, "A PET disk image is a D64 (174,848 bytes, 35 tracks), a D80 (533,248) or a D82 (1,066,496); this one has %u bytes.", (unsigned)n);
        return 1;
    }
    int cur = c->settings->value(PET_S_DRIVE);
    int other = d ^ 1;
    if (cur != kind && st->drv[other].file && drv_kind(st->drv[other].size) != kind) {
        c->fs->close(f);
        snprintf(msg, msglen, "The other drive of the unit holds a %s image; a unit is a 4040 or an 8250.", cur == PET_DRV_4040 ? "D64" : "D80 / D82");
        return 1;
    }
    if (drv_eject(c, d, msg, msglen)) {
        c->fs->close(f);
        return 1;
    }
    for (uint32_t off = 0; off < n; off += buf.n) {
        uint32_t k = n - off < buf.n ? n - off : (uint32_t)buf.n;
        if (!f->read(off, buf, k)) {
            c->fs->close(f);
            snprintf(msg, msglen, "cannot read %s (the medium is gone?)", path);
            return 1;
        }
        c->io->ddr_write(drv_base(d) - 0x01000000u + off, buf, k);
    }
    PetDrive &dr = st->drv[d];
    dr.file = f;
    dr.size = n;
    dr.ro = ro;
    const char *slash = strrchr(path, '/');
    fs_short_name(dr.name, sizeof(dr.name), slash ? slash + 1 : path);
    snprintf(dr.path, sizeof(dr.path), "%s", path);
    wr32(c, PET_DRV_BASE0 + 4 * d, drv_base(d));
    wr32(c, PET_DRV_SIZE0 + 4 * d, n);
    uint8_t rob = c->io->rd(PET_DRV_RO);
    c->io->wr(PET_DRV_RO, (uint8_t)(ro ? rob | (1u << d) : rob & ~(1u << d)));
    c->io->wr(PET_DRV_CLR, (uint8_t)(1u << d));
    c->io->wr(PET_DRV_MNT, (uint8_t)(1u << d));
    mount_notify(c, d);
    printf("[pet] drive 8:%d %s, %u bytes%s\n", d, dr.name, (unsigned)n, ro ? ", write protected" : "");
    if (cur != kind) {                                  // the unit's type follows the image (the ROMs are put by the reset); the other drive was empty or of this kind
        int mi = c->settings->find("Drive 8 (at next reset)");
        if (mi >= 0) {
            c->settings->set(mi, kind);
        }
        return boot_cold_reset(c, msg, msglen);
    }
    return 0;
}
static int op_mount0(CoreCtx *c, const OpArgs &a, OpOut &o) { return drv_mount(c, 0, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_mount1(CoreCtx *c, const OpArgs &a, OpOut &o) { return drv_mount(c, 1, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_eject0(CoreCtx *c, const OpArgs &, OpOut &o) { return drv_eject(c, 0, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_eject1(CoreCtx *c, const OpArgs &, OpOut &o) { return drv_eject(c, 1, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static const char *drv_name0(CoreCtx *c) { PetDrive &d = pet_state(c)->drv[0]; return d.file ? d.name : 0; }
static const char *drv_name1(CoreCtx *c) { PetDrive &d = pet_state(c)->drv[1]; return d.file ? d.name : 0; }

// Written back when the core says dirty and the drive is quiet; CoreDesc::media_flush (T180 D4) does it at once.
static int pet_media_flush(CoreCtx *c, char *msg, int msglen)
{
    uint8_t st = c->io->rd(PET_DRV_ST);
    for (int d = 0; d < 2; d++) {
        if (((st >> d) & 1) && pet_state(c)->drv[d].file && drv_writeback(c, d, msg, msglen)) {
            return 1;
        }
    }
    return 0;
}
static bool pet_media_pending(CoreCtx *c) { return (c->io->rd(PET_DRV_ST) & 3) != 0; }
static void drv_service(CoreCtx *c)
{
    uint8_t st = c->io->rd(PET_DRV_ST);
    if ((st & 3) && !(st & 0x0C)) {
        char msg[96];
        if (pet_media_flush(c, msg, sizeof(msg))) {
            printf("[pet] %s\n", msg);
        }
    }
}

static const char *const slot_names[] = { "Drive 8", "Drive 8.1", "Tape" };
static const char *slot_spec(CoreCtx *c, int i)
{
    PetState *st = pet_state(c);
    if (i == 2) {
        return st->deck.loaded && st->deck.path[0] ? st->deck.path : 0;
    }
    PetDrive &d = st->drv[i];
    return d.file && d.path[0] ? d.path : 0;
}
static int slot_mount(CoreCtx *c, int i, const char *spec, char *msg, int msglen)
{
    return i == 2 ? deck_insert(c, &pet_state(c)->deck, tape_regs, spec, false, false, msg, msglen) : drv_mount(c, i, spec, msg, msglen);
}
static int slot_eject(CoreCtx *c, int i, char *msg, int msglen)
{
    if (i == 2) {
        deck_eject(c, &pet_state(c)->deck, tape_regs);
        return 0;
    }
    return drv_eject(c, i, msg, msglen);
}
static const MountSlots mount_slots = { 3, slot_names, slot_spec, slot_mount, slot_eject };

// ---- control -------------------------------------------------------------------------------------------------------------------
static int pet_init(CoreCtx *c, char *, int)
{
    cbm_run_clear(&pet_state(c)->run);
    deck_stop(c, &pet_state(c)->deck, tape_regs);   // a tape that was playing stops with the machine; it stays where it is
    c->io->mode_reset(true);
    return 0;
}

static int pet_release(CoreCtx *c, char *, int)
{
    c->io->mode_reset(false);
    return 0;
}

// ---- .prg ----------------------------------------------------------------------------------------------------------------------
// The numbers are the model's (pet_models.h): BASIC 2 and 4 start at $0401 with the pointer pairs at $28.., BASIC 1 (the 2001) at $0400 with $7A...
static CbmRunMachine machine(CoreCtx *c)
{
    CbmRunMachine m;
    memset(&m, 0, sizeof(m));
    m.tag = "pet";
    m.prg = pet_prg_params(pet_model(c));
    m.ready_ms = 2200;
    m.settle_ms = 300;
    m.giveup_ms = 4000;
    m.refuse = 0;
    return m;
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue(c, &pet_state(c)->run, machine(c), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_load_now(c, &pet_state(c)->run, machine(c), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_service(CoreCtx *c, const OpArgs &, OpOut &)
{
    drv_service(c);
    deck_service(c, &pet_state(c)->deck, tape_regs);
    return cbm_run_service(c, &pet_state(c)->run, machine(c));
}
static int op_type(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return cbm_run_queue_script(c, &pet_state(c)->run, machine(c), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
// T206: Run Disk = mount in drive 8 (device 8, drive 0 of the unit), reset, type LOAD"*",8 and RUN at READY. LOAD (not DLOAD) is in BASIC 1, 2 and 4;
// the secondary address 0 puts the program at the start of BASIC, so no ",1". The first piece is the ten keys of the keyboard buffer.
static int op_mount_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (drv_mount(c, 0, a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    return cbm_run_queue_script(c, &pet_state(c)->run, machine(c), "LOAD\"*\",8\r|RUN\r", o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
// ---- cassette 1 (the file of the "Tape" slot; the PET's motor gates the play bit, the press of PLAY is the sense line) --------------------------------
static int op_tape_insert(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return deck_insert(c, &pet_state(c)->deck, tape_regs, a.s(0), a.i(1) != 0, false, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}
static int op_tape_play(CoreCtx *c, const OpArgs &, OpOut &o) { return deck_play(c, &pet_state(c)->deck, tape_regs, o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_tape_stop(CoreCtx *c, const OpArgs &, OpOut &) { deck_stop(c, &pet_state(c)->deck, tape_regs); return OPE_OK; }
static int op_tape_rewind(CoreCtx *c, const OpArgs &, OpOut &) { deck_rewind(c, &pet_state(c)->deck, tape_regs); return OPE_OK; }
static int op_tape_eject(CoreCtx *c, const OpArgs &, OpOut &) { deck_eject(c, &pet_state(c)->deck, tape_regs); return OPE_OK; }
static const char *tape_image(CoreCtx *c) { return deck_image(&pet_state(c)->deck); }
static const ArgSpec tape_insert_args[] = { ARG_PATH("path"), ARG_INT("play", 0, 1) };
static const OpBind tape_play_bind[] = { B_FILE("path"), B_INT("play", 1) }, tape_only_bind[] = { B_FILE("path"), B_INT("play", 0) };

static const ArgSpec text_arg[] = { ARG_STRING("text", 63) };
static const ArgSpec path_arg[] = { ARG_PATH("path") };
static const char *prg_name(CoreCtx *c) { return cbm_run_name(&pet_state(c)->run); }
static const char *prg_pending(CoreCtx *c) { return cbm_run_pending(&pet_state(c)->run); }

static const OpBind file_bind[] = { B_FILE("path") };
static const FileAction prg_actions[] = {
    { "Load", "pet.load_prg", file_bind, 1, false, 0, 0 },
    { "Load + run", "pet.run_prg", file_bind, 1, true, 0, 0 },
};
static const FileAction disk_actions[] = {
    { "Mount in Drive 8", "pet.mount_d0", file_bind, 1, false, 0, 0 },
    { "Mount in Drive 8.1", "pet.mount_d1", file_bind, 1, false, 0, 0 },
    { "Run Disk", "pet.mount_run", file_bind, 1, true, 0, 0 },
};
static const FileAction tape_actions[] = {
    // the PET asks for PLAY itself (the sense line is the play bit): "Insert tape and play" presses it, the PET's motor then runs the tape
    { "Insert tape",          "pet.tape_insert", tape_only_bind, 2, false, 0, 0 },
    { "Insert tape and play", "pet.tape_insert", tape_play_bind, 2, true, 0, 0 },
};
static const FileType filetypes[] = {
    { ".prg", prg_actions, 2 },
    { ".p00", prg_actions, 2 },
    { ".d64", disk_actions, 3 },
    { ".d80", disk_actions, 3 },
    { ".d82", disk_actions, 3 },
    { ".tap", tape_actions, 2 },
};

static const Task tasks[] = {
    { "Reset", "machine.cold_reset", 0, 0, true },
    { "Reload ROMs, reset", "machine.reload_roms", 0, 0, true },
    { "Tape: Play", "pet.tape_play", 0, 0, true },
    { "Tape: Stop", "pet.tape_stop", 0, 0, false },
    { "Tape: Rewind", "pet.tape_rewind", 0, 0, false },
    { "Tape: Eject", "pet.tape_eject", 0, 0, false },
};

static const MediaSlot media[] = {
    { "Program", "pet.program", 0, 0, 0 },
    { "Drive 8", "pet.drive8", "pet.eject_d0", 0, 0, 0, ".d64,.d80,.d82", "Mount in Drive 8", 0, 0 },
    { "Drive 8.1", "pet.drive81", "pet.eject_d1", 0, 0, 0, ".d64,.d80,.d82", "Mount in Drive 8.1", 0, 0 },
    { "Tape", "pet.tape.image", "pet.tape_eject", 0, 0, 0, ".tap", "Insert tape", 0, "Tape: Play,Tape: Stop,Tape: Rewind" },
};

// 32 KB of RAM; the 8096's banked 64 KB are not reachable yet (T134 section 5)
static const MemRegion pet_mem_regions[] = { { 0, 0x8000, MK_RAM, 0, 0, "RAM" } };
static const MemMap pet_mem = { pet_mem_regions, 1, true, 0 };

static void pet_register(OpRegistry &r)
{
    r.contributor("pet");
    MEM_OPS(r, pet_mem, 0x7FFF);
    r.add_op("pet.put_basic", rom_arg, 1, op_put_basic);
    r.add_op("pet.put_editor", rom_arg, 1, op_put_editor);
    r.add_op("pet.put_kernal", rom_arg, 1, op_put_kernal);
    r.add_op("pet.put_char", rom_arg, 1, op_put_char);
    r.add_op("pet.run_prg", path_arg, 1, op_run);
    r.add_op("pet.load_prg", path_arg, 1, op_load);
    r.add_op("pet.put_dos4040", rom_arg, 1, op_put_dos4040);
    r.add_op("pet.put_ctl4040", rom_arg, 1, op_put_ctl4040);
    r.add_op("pet.put_dos8250", rom_arg, 1, op_put_dos8250);
    r.add_op("pet.put_ctl8250", rom_arg, 1, op_put_ctl8250);
    r.add_op("pet.put_wl_a", rom_arg, 1, op_put_wl_a);
    r.add_op("pet.put_wl_b", rom_arg, 1, op_put_wl_b);
    r.add_op("pet.put_wl_c", rom_arg, 1, op_put_wl_c);
    r.add_op("pet.put_wl_d", rom_arg, 1, op_put_wl_d);
    r.add_op("pet.put_wl_e", rom_arg, 1, op_put_wl_e);
    r.add_op("pet.put_wl_f", rom_arg, 1, op_put_wl_f);
    r.add_op("pet.mount_d0", path_arg, 1, op_mount0);
    r.add_op("pet.mount_d1", path_arg, 1, op_mount1);
    r.add_op("pet.mount_run", path_arg, 1, op_mount_run);
    r.add_op("pet.eject_d0", 0, 0, op_eject0);
    r.add_op("pet.eject_d1", 0, 0, op_eject1);
    r.add_op("pet.tape_insert", tape_insert_args, 2, op_tape_insert);
    r.add_op("pet.tape_play", 0, 0, op_tape_play);
    r.add_op("pet.tape_stop", 0, 0, op_tape_stop);
    r.add_op("pet.tape_rewind", 0, 0, op_tape_rewind);
    r.add_op("pet.tape_eject", 0, 0, op_tape_eject);
    r.add_op("pet.service", 0, 0, op_service);
    r.add_op("pet.type_script", text_arg, 1, op_type);
    r.add_state("pet.program", prg_name);
    r.add_state("pet.pending", prg_pending);
    r.add_state("pet.drive8", drv_name0);
    r.add_state("pet.drive81", drv_name1);
    r.add_state("pet.tape.image", tape_image);
}

static const char *const caps[] = { "tape" };     // the cassette 1 slot (the capability only lets the word through the menu check)

extern const CoreDesc pet_desc;
const CoreDesc pet_desc = {
    { 'P', 'E', 'T', '1' },
    "Commodore PET",
    "Commodore PET",
    "PET",
    "pet",
    "PET Settings",
    1,
    roms, 14,
    settings, 9,
    tasks, 6,
    filetypes, 6,
    media, 4,
    // 640 x 400 shown 2 x 2 (1280 x 800), centred on 1920 x 1080; the buffers are the core's reset values of FB_BASE / FB_STRIDE
    { 640, 400, 2, 320, 140, PET_FB_ADDR, PET_FB_STRIDE_BYTES, 4, PET_FB_LATEST, PET_FRAME_SEQ, PET_FB_BASE, PET_FB_STRIDE },
    { PET_STATUS, PET_ST_HELD, 200 },
    pet_init, pet_release, 0, 20,
    "pet.service", 20, false,
    0, 0,
    pet_register,
    caps, 1,
    false, false, 0, 0, 0, 0, 0, 0, 0,
    "pet.type_script",
    "pet.run_prg",
    0x8000,
    0, 0, 0, 0,
    0, 0,
    0,
    0, 0,
    0, false, 0, 0, false,
    pet_media_flush, pet_media_pending,           // T180 D4: the drive's write-back
    &mount_slots,                                 // T180 D2, D5: Drive 8, Drive 8.1 and Tape
    &hk_console,
};
