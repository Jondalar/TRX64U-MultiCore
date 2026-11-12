#include "boot.h"
#include "ops_glue.h"
#include "video.h"
#include "regset.h"
#include "engines.h"
#include "mediamounts.h"
#include "partial.h"
#include "writeback.h"
#include <stdio.h>
#include <string.h>
#include "corefolder.h"

// T148: a core reads its ROMs from its own folder only. What lies in the old places is copied there first (rom_import: it does nothing once
// every spec has its file), then the folder is scanned. Without a .cor path (a developer start) the old search over the roots stays.
static RomStatus scan_roms(CoreCtx *c, RomResult *res, char *msg, int msglen)
{
    const CoreDesc *d = c->desc;
    char own[96], old[96];
    const char *own_dir = rom_own_dir(c->cor_path, own, sizeof(own)) ? own : 0;
    const char *first = c->board ? c->board->rom_dir : 0;
    if (own_dir) {
        rom_import(c->fs, d->folder, d->roms, d->nroms, own_dir, first, corefolder_old_roms(c->cor_path, old, sizeof(old)) ? old : 0);
    }
    return rom_scan(c->fs, d->folder, d->roms, d->nroms, res, msg, msglen, first, own_dir);
}

static uint8_t rombuf[1u << 19];

int boot_put_roms(CoreCtx *c, const RomResult *res, char *msg, int msglen)
{
    const CoreDesc *d = c->desc;
    for (int i = 0; i < d->nroms; i++) {
        const RomSpec &s = d->roms[i];
        if (res[i].why[0]) {                      // T185 S3: an optional file that was refused: told on the screen's notice line and the console
            printf("[core] %s\n", res[i].why);
            if (!c->notice_pending) {
                snprintf(c->notice, sizeof(c->notice), "%s", res[i].why);
                c->notice_pending = true;
            }
        }
        if (res[i].status != ROM_FOUND) {
            continue;
        }
        if (res[i].hit.size > sizeof(rombuf) || !rom_read(c->fs, &res[i].hit, rombuf)) {
            snprintf(msg, msglen, "%s: cannot read %s/%s (the medium is gone?)", s.role, res[i].hit.dir, res[i].hit.name);
            return 1;
        }
        OpIn in = in_blob("data", rombuf, res[i].hit.size);
        OpResult r;
        int rc = ops_call(c, s.put_op, &in, 1, &r);
        if (rc) {
            snprintf(msg, msglen, "%s", r.msg[0] ? r.msg : "the core did not take the ROM");
            return rc;
        }
        printf("[core] %s: %s/%s (%u bytes)\n", s.role, res[i].hit.dir, res[i].hit.name, (unsigned)res[i].hit.size);
    }
    // the ROM set (T104 5.2): the member the choice names, read like a named file and handed to the base's own placement
    if (d->romset) {
        RomHit hit;
        if (!romset_hit(c, &hit)) {
            snprintf(msg, msglen, "%s not found.", d->romset->role);
            return 1;
        }
        if (hit.size > sizeof(rombuf) || !rom_read(c->fs, &hit, rombuf)) {
            snprintf(msg, msglen, "%s: cannot read %s/%s", d->romset->role, hit.dir, hit.name);
            return 1;
        }
        OpIn in = in_blob("data", rombuf, hit.size);
        OpResult r;
        int rc = ops_call(c, "romset.put", &in, 1, &r);
        if (rc) {
            snprintf(msg, msglen, "%s", r.msg[0] ? r.msg : "the core did not take the ROM");
            return rc;
        }
    }
    return 0;
}

void boot_apply_settings(CoreCtx *c, bool all)
{
    regset_apply(c, all);
}

int boot_finish(CoreCtx *c, char *msg, int msglen)
{
    const CoreDesc *d = c->desc;
    if (c->video && !c->video->ready()) {
        c->video->init();
    }
    boot_apply_settings(c, true);
    if (c->media_restoring) {
        return 0;                                 // T180 D2: the start (or the media set) releases the machine once, after the media are in; the settings that depend on the program are in
    }
    c->io->delay_ms(d->reset_ms);
    d->release(c, msg, msglen);
    c->started = true;
    return 0;
}

static int need_program(CoreCtx *c, char *msg, int msglen)
{
    if (c->desc->needs_program && !c->program_loaded) {
        snprintf(msg, msglen, "%s first.", c->desc->needs_program);
        return 1;
    }
    return 0;
}

BootResult boot_start(CoreCtx *c, BootUi *ui)
{
    const CoreDesc *d = c->desc;
    char msg[240];
    if (d->romset || d->window_media || d->sector_media || d->mouse) {
        engines_of(c);                            // made here, on the one task there is, before any other task asks for it
    }
    if (d->init) {
        d->init(c, msg, sizeof(msg));
    }
    // step 4: the ROMs, as often as it takes. The core is never released with a required ROM missing or a file that did
    // not go in; inserting the medium is all it takes to go on.
    RomResult res[ROMS_MAX];
    RomWait wait;
    bool told = false;
    bool refused = false;                         // a ROM set found and refused: told at once, looked at again after 5 s (T104 5.1)
    while (d->nroms || d->romset) {
        RomStatus st = d->nroms ? scan_roms(c, res, msg, sizeof(msg)) : ROM_FOUND;
        if (st == ROM_FOUND && d->romset) {
            st = romset_scan(c, msg, sizeof(msg));
        }
        if (st == ROM_FOUND) {
            if (d->romset) {
                romset_clear(c);                  // a cold start leaves nothing of the last run in the machine's RAM
            }
            int rc = boot_put_roms(c, res, msg, sizeof(msg));
            if (rc == 0) {
                break;
            }
            st = ROM_BAD_CONTENT;                 // the file was found but did not go in: told like a bad file
        }
        refused = d->romset && st >= ROM_WRONG_SIZE;
        if (wait.step(st, msg)) {
            ui->message("ROM files", msg, true);
            told = true;
        }
        ui->tick();
        c->io->delay_ms(refused ? 5000 : wait.delay_ms());
    }
    if (told) {
        ui->clear();
    }
    // steps 5 and 6: the window (the layer is the run loop's to hand over), the settings
    if (c->video) {
        c->video->init();
    }
    boot_apply_settings(c, true);
    mounts_restore(c, ui);                        // T180 D2: with "Persist medium mounts" the media of the last run, so that the core sees them from its first cycle
    if (d->needs_program && !c->program_loaded) {
        return BOOT_WAITING_PROGRAM;
    }
    // step 7
    d->release(c, msg, sizeof(msg));
    c->started = true;
    return BOOT_RUNNING;
}

static int reload(CoreCtx *c, char *msg, int msglen, bool rescan, bool cold = false, bool partial = false);

// T154, T180 D4: a reset, a reload and a restart come after the media: everything pending for a writable medium is written back first (media_writeback).
// A write-back that fails stops the action, the medium stays mounted and the reason is the answer (no silent loss).
static int flush_first(CoreCtx *c, WbWhy why, char *msg, int msglen)
{
    if (media_writeback(c, why, msg, msglen)) {
        printf("[core] write-back failed, the action stops: %s\n", msg);
        return 1;
    }
    return 0;
}

int boot_cold_reset(CoreCtx *c, char *msg, int msglen)
{
    if (c->media_restoring) {
        boot_apply_settings(c, false);            // T180 D2: a mount that wants a reset gets its reset-time items; the machine is held and released once by the caller
        return 0;
    }
    if (flush_first(c, WB_WHY_RESET, msg, msglen)) {
        return 1;
    }
    const CoreDesc *d = c->desc;
    if (need_program(c, msg, msglen)) {
        return 1;                                 // nothing to run yet: the machine stays as it is
    }
    if (d->romset && romset_stale(c)) {
        return reload(c, msg, msglen, false);     // the choice names another file than the one in the window (reload_on_change)
    }
    printf("[core] cold reset\n");
    c->paused = false;                            // a reset runs the machine
    d->init(c, msg, msglen);                      // hold
    boot_apply_settings(c, false);                // the reset-time items go in while the core is held
    c->io->delay_ms(d->reset_ms);
    d->release(c, msg, msglen);
    c->started = true;
    return 0;
}

int boot_reload(CoreCtx *c, char *msg, int msglen)
{
    if (flush_first(c, WB_WHY_RESET, msg, msglen)) {
        return 1;
    }
    return reload(c, msg, msglen, true);
}

int boot_restart(CoreCtx *c, char *msg, int msglen)
{
    if (flush_first(c, WB_WHY_CORE, msg, msglen)) {
        return 1;
    }
    if (need_program(c, msg, msglen)) {
        return 1;                                 // nothing to restart yet: the machine stays as it is
    }
    if (reload_is_dfx(c)) {
        return reload(c, msg, msglen, true, true, true);   // "Reload core": the partial bitstream loaded again (T104 5.14)
    }
    if (c->desc->cold_start) {
        int rc = c->desc->cold_start(c, msg, msglen);      // a flat build: the core's own cold start, then the same sequence
        if (rc) {
            return rc;
        }
    }
    return reload(c, msg, msglen, true, true);
}

// The machine RAM to zero, through the RAM window (the machine is held).
static void clear_machine_ram(CoreCtx *c)
{
    static const uint8_t zero[4096] = { 0 };
    for (uint32_t a = 0; a < c->desc->cold_ram_bytes; a += sizeof(zero)) {
        uint32_t n = c->desc->cold_ram_bytes - a;
        c->io->ram_write(a, zero, n < sizeof(zero) ? n : (uint32_t)sizeof(zero));
    }
}

// After a partial bitstream every flip-flop and block RAM of the core is at its configuration value: what the start wrote into the registers is
// gone. The libraries that keep a copy of it write it again (the drive, the window and sector media, the picture window).
static void after_partial(CoreCtx *c)
{
    OpRegistry *reg = ops_of(c);
    if (reg && reg->find_op("drive.resync")) {
        OpResult r;
        ops_call(c, "drive.resync", 0, 0, &r);
    }
    engines_after_reload(c);
}

static int reload(CoreCtx *c, char *msg, int msglen, bool rescan, bool cold, bool partial)
{
    const CoreDesc *d = c->desc;
    RomResult res[ROMS_MAX];
    RomStatus st = scan_roms(c, res, msg, msglen);
    if (st == ROM_FOUND && d->romset && rescan) {
        st = romset_scan(c, msg, msglen);
    }
    if (st != ROM_FOUND) {
        return 1;                                 // the machine keeps running; msg says what is missing
    }
    CorInfo ci;
    if (partial && partial_check(c, &ci, msg, msglen)) {
        return 1;                                 // nothing touched: the machine keeps running
    }
    d->init(c, msg, msglen);                      // hold
    if (partial) {
        if (partial_stream(c, &ci, msg, msglen)) {
            return 1;
        }
        d->init(c, msg, msglen);                  // the core is new: held again, then what the start puts into it
        after_partial(c);
        if (c->video) {
            c->video->init();                     // the frame buffers black, FB_BASE / FB_STRIDE told to the core
        }
    }
    if (d->romset && c->started) {
        romset_clear(c);                          // a cold start: no leftovers in chip, slow and fast RAM
    }
    if (cold && d->cold_ram_bytes) {
        clear_machine_ram(c);
    }
    int rc = boot_put_roms(c, res, msg, msglen);
    if (rc) {
        return rc;                                // held, with the ROMs half in: the menu stays up and says so
    }
    boot_apply_settings(c, cold);                 // a restart writes every setting, the live ones too, as the start does
    c->io->delay_ms(d->reset_ms);
    d->release(c, msg, msglen);
    c->started = true;
    return 0;
}

int boot_warm_reset(CoreCtx *c, char *msg, int msglen)
{
    if (c->media_restoring) {
        return boot_cold_reset(c, msg, msglen);
    }
    if (flush_first(c, WB_WHY_RESET, msg, msglen)) {
        return 1;
    }
    if (need_program(c, msg, msglen)) {
        return 1;
    }
    if (c->desc->warm_reset) {
        c->desc->warm_reset(c, msg, msglen);
        return 0;
    }
    return boot_cold_reset(c, msg, msglen);
}

void boot_register(OpRegistry &r)
{
    r.add_op("machine.warm_reset", 0, 0, op_hook<boot_warm_reset>);
    r.add_op("machine.cold_reset", 0, 0, op_hook<boot_cold_reset>);
    r.add_op("machine.reload_roms", 0, 0, op_hook<boot_reload>);
    r.add_op("machine.restart", 0, 0, op_hook<boot_restart>);
}
