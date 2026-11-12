#include "cbm1541.h"
#include "ops_glue.h"
#include "d1541_drive.h"
#include "d1541_fs.h"
#include "engines.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// T180 D2: a mount or eject of the disk is told to the application (slot 0 of every descriptor with a drive is Drive A)
static void drive_changed(void *user)
{
    mount_notify((CoreCtx *)user, 0);
}

static CbmDrive *make_drive(CoreCtx *c)
{
    CbmDrive *d = new CbmDrive(c->io, c->fs);
    d->on_change(drive_changed, c);
    return d;
}

CbmDrive *cbm1541_drive(CoreCtx *c, char *msg, int msglen)
{
    char local[96];
    if (!msg) {
        msg = local;
        msglen = sizeof(local);
    }
    if (!c->drive) {
        if (!c->io || !c->fs) {
            snprintf(msg, msglen, "No drive: the core is not set up.");
            return 0;
        }
        c->drive = make_drive(c);
    }
    CbmDrive *d = (CbmDrive *)c->drive;
    return d->attach(msg, msglen) ? d : 0;
}

// What the drive told once (a write-back that failed) goes to the screen like any other service's notice.
static void pass_notice(CoreCtx *c)
{
    if (!c->drive) {
        return;
    }
    if (const char *n = ((CbmDrive *)c->drive)->take_notice()) {
        if (!c->notice_pending) {
            snprintf(c->notice, sizeof(c->notice), "%s", n);
            c->notice_pending = true;
        }
    }
}

// The one slot of this library: the IEC bus' drive A (T104.1 D15 `iec:drive_a`).
static bool slot_ok(const char *s, OpOut &o)
{
    if (!strcmp(s, "iec:drive_a") || !strcmp(s, "drive_a")) {
        return true;
    }
    snprintf(o.msg, o.msglen, "No such drive slot \"%s\": this machine has iec:drive_a.", s);
    return false;
}

#define DRIVE_OR_FAIL(c, o, var) CbmDrive *var = cbm1541_drive(c, o.msg, o.msglen); if (!var) return OPE_FAILED
#define SLOT_OR_FAIL(a, k, o) if (!slot_ok(a.s(k), o)) return OPE_FAILED

static const char *const mode_labels[] = { "rw", "ro", "unlinked" };
static const char *const save_labels[] = { "d64", "g64" };
static const char *const create_labels[] = { "d64", "g64", "d81" };

static const ArgSpec slot_arg[]   = { ARG_SLOT("slot") };
static const ArgSpec mount_args[] = { ARG_SLOT("slot"), ARG_PATH("path"), ARG_ENUM("mode", mode_labels, 3) };
static const ArgSpec run_args[]   = { ARG_SLOT("slot"), ARG_PATH("path") };
static const char *const how_labels[] = { "dma", "load" };
static const ArgSpec run_file_args[] = { ARG_SLOT("slot"), ARG_PATH("path"), ARG_ENUM("how", how_labels, 2) };
static const ArgSpec save_args[]  = { ARG_SLOT("slot"), ARG_STRING("path", 255), ARG_ENUM("format", save_labels, 2) };
static const ArgSpec create_args[] = { ARG_STRING("path", 255), ARG_ENUM("format", create_labels, 3), ARG_STRING("name", 24) };
static const ArgSpec rom_file_args[] = { ARG_SLOT("slot"), ARG_PATH("path") };
static const ArgSpec blob_arg[]   = { ARG_BLOB("data") };
static const ArgSpec bool_arg[]   = { ARG_INT("value", 0, 1) };
static const ArgSpec id_arg[]     = { ARG_INT("value", 0, 3) };
static const ArgSpec delay_arg[]  = { ARG_INT("value", 0, 9) };

static int op_mount(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    MediaSaving saving(c, d->pending());                      // T180 D4: the old disk is written back first
    int rc = d->mount(a.s(1), a.i(2), o.msg, o.msglen);
    pass_notice(c);
    return rc ? OPE_FAILED : OPE_OK;
}

static int op_eject(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    MediaSaving saving(c, d->pending());
    int rc = d->eject(o.msg, o.msglen);
    pass_notice(c);
    return rc ? OPE_FAILED : OPE_OK;
}

static int op_save(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    int rc = d->save(a.s(1), a.i(2), o.msg, o.msglen);
    pass_notice(c);
    return rc ? OPE_FAILED : OPE_OK;
}

static int op_create(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    // creating a file needs no attached drive (the library's file side); the object is made but not attached
    if (!c->drive) {
        c->drive = make_drive(c);
    }
    CbmDrive *d = (CbmDrive *)c->drive;
    d->set_new_dir(c->media_dir);
    char made[260];
    made[0] = 0;
    int pct = 0;
    int rc = d->create(a.s(0), a.i(1), a.s(2), o.msg, o.msglen, made, sizeof(made), c->job_step, c->job_cancel, &pct);
    if (rc == 3) {
        c->job_cancel = false;
        return OPE_OK;
    }
    if (rc == 2) {                                         // a chunk is written, the caller calls again (CoreCtx::job_step)
        c->job_pct = pct;
        snprintf(c->job_path, sizeof(c->job_path), "%s", made);
        o.more = true;
        o.msg[0] = 0;
        return OPE_OK;
    }
    if (!rc) {
        c->job_pct = 100;
        snprintf(c->job_path, sizeof(c->job_path), "%s", made);
        snprintf(c->notice, sizeof(c->notice), "%s", o.msg);
        c->notice_pending = true;
    }
    return rc ? OPE_FAILED : OPE_OK;
}

// T180 D2, D5: the slot of every core with a drive, "Drive A": the image's path, with the mount mode in front when it is not read/write ("ro <path>",
// "unlinked <path>"; a path starts with '/').
static const char *drive_spec(CoreCtx *c, int)
{
    static char buf[FS_PATH_MAX + 16];
    if (!c->drive) {
        return 0;
    }
    CbmDrive *d = (CbmDrive *)c->drive;
    const char *p = d->image_path();
    if (!p) {
        return 0;
    }
    int m = d->mount_mode();
    snprintf(buf, sizeof(buf), "%s%s", m == MM_RO ? "ro " : m == MM_UNLINKED ? "unlinked " : "", p);
    return buf;
}

static int drive_mount(CoreCtx *c, int, const char *spec, char *msg, int msglen)
{
    int mode = MM_RW;
    if (!strncmp(spec, "ro ", 3)) {
        mode = MM_RO;
        spec += 3;
    } else if (!strncmp(spec, "unlinked ", 9)) {
        mode = MM_UNLINKED;
        spec += 9;
    }
    CbmDrive *d = cbm1541_drive(c, msg, msglen);
    if (!d) {
        return 1;
    }
    MediaSaving saving(c, d->pending());
    int rc = d->mount(spec, mode, msg, msglen);
    pass_notice(c);
    return rc;
}

static int drive_eject(CoreCtx *c, int, char *msg, int msglen)
{
    CbmDrive *d = cbm1541_drive(c, msg, msglen);
    if (!d) {
        return 1;
    }
    MediaSaving saving(c, d->pending());
    int rc = d->eject(msg, msglen);
    pass_notice(c);
    return rc;
}

const char *cbm1541_slot_spec(CoreCtx *c) { return drive_spec(c, 0); }
int cbm1541_slot_mount(CoreCtx *c, const char *spec, char *msg, int msglen) { return drive_mount(c, 0, spec, msg, msglen); }
int cbm1541_slot_eject(CoreCtx *c, char *msg, int msglen) { return drive_eject(c, 0, msg, msglen); }

static const char *const drive_names[] = { "Drive A" };
const MountSlots cbm1541_mount_slots = { 1, drive_names, drive_spec, drive_mount, drive_eject };

// T180 D4: CoreDesc::media_flush / media_pending of every core with a drive.
int cbm1541_media_flush(CoreCtx *c, char *msg, int msglen)
{
    return c->drive ? ((CbmDrive *)c->drive)->flush(msg, msglen) : 0;
}

bool cbm1541_media_pending(CoreCtx *c)
{
    return c->drive && ((CbmDrive *)c->drive)->pending();
}

static int op_reset(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    return d->reset(o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// Reload core (T104 5.14): the registers of the drive are at their configuration values; nothing to do when no drive was ever made.
static int op_resync(CoreCtx *c, const OpArgs &, OpOut &)
{
    if (c->drive) {
        ((CbmDrive *)c->drive)->resync_hw();
    }
    return OPE_OK;
}

static int op_set_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    return d->set_rom_file(a.s(1), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

static int op_put_rom(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    return d->put_rom(DT_1541, a.p(0), a.len(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

static int op_put_rom1581(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    return d->put_rom(DT_1581, a.p(0), a.len(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// The settings. A value that cannot be applied (no ROM yet: the drive stays off) is told, not an error of the setting.
static int op_set_power(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    d->set_power(a.i(0) != 0, o.msg, o.msglen);
    return OPE_OK;
}
static int op_set_bus_id(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    d->set_bus_id(8 + a.i(0));
    return OPE_OK;
}
static int op_set_ram(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    d->set_extra_ram(a.i(0) != 0);
    return OPE_OK;
}
static int op_set_swap(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    d->set_swap_delay(a.i(0) + 1);
    return OPE_OK;
}

void cbm1541_run_script(int bus_id, char *out, int outlen)
{
    // LOAD"*",8,1 and RUN: the keyboard buffer holds ten keys, so the line goes in as LOAD"*",8 and ,1<CR>, then RUN<CR>
    snprintf(out, outlen, "LOAD\"*\",%d|,1\r|RUN\r", bus_id);
}

static int op_mount_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    if (!c->desc->type_op) {
        snprintf(o.msg, o.msglen, "%s cannot type into the machine: Mount & Run is not available.", c->desc->title);
        return OPE_FAILED;
    }
    if (!d->powered()) {
        snprintf(o.msg, o.msglen, "The drive is off (the Drive setting) or has no ROM (1541.rom or 1541.bin in the core's roms folder or in the shared roms folder): Mount & Run would find no disk.");
        return OPE_FAILED;
    }
    if (d->mount(a.s(1), MM_RW, o.msg, o.msglen)) {
        pass_notice(c);
        return OPE_FAILED;
    }
    char script[64];
    cbm1541_run_script(d->bus_id(), script, sizeof(script));
    OpIn in = in_str("text", script);
    OpResult r;
    int rc = ops_call(c, c->desc->type_op, &in, 1, &r);
    if (rc) {
        snprintf(o.msg, o.msglen, "%s", r.msg[0] ? r.msg : "the machine did not take the typed start");
        return OPE_FAILED;
    }
    return OPE_OK;
}

// LOAD"NAME",8,1 and RUN for a file of the disk: the name as the disk holds it (PETSCII), in pieces of at most ten keys. A character that cannot be
// typed inside the quotes (the quote itself, the piece separator, a return) becomes the wildcard.
void cbm1541_load_script(int bus_id, const uint8_t *pet, int len, char *out, int outlen)
{
    int n = snprintf(out, (size_t)outlen, "LOAD\"|");
    for (int i = 0; i < len && n < outlen - 24; i++) {
        uint8_t ch = pet[i];
        if (ch == '"' || ch == '|' || ch == '\r' || ch == 0) {
            ch = '?';
        }
        if (i > 0 && i % 10 == 0) {
            out[n++] = '|';
        }
        out[n++] = (char)ch;
    }
    if (len > 0) {
        out[n++] = '|';                                    // the closing quote starts a piece of its own: the last name piece may be full
    }
    snprintf(out + n, (size_t)(outlen - n), "\",%d,1\r|RUN\r", bus_id);
}

// Mount & Run (dma) and Real Run (load) of one PRG inside a disk image: the image is mounted, then either the core runs the file it reads out of the
// image (the same Run PRG as for a file of a stick) or the machine is reset and LOADs it from the drive.
static int op_mount_run_file(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    DRIVE_OR_FAIL(c, o, d);
    SLOT_OR_FAIL(a, 0, o);
    bool dma = a.i(2) == 0;
    const char *op = dma ? c->desc->run_op : c->desc->type_op;
    if (!op) {
        snprintf(o.msg, o.msglen, "%s cannot %s a program: not available.", c->desc->title, dma ? "run" : "type into the machine to load");
        return OPE_FAILED;
    }
    if (!d->powered()) {
        snprintf(o.msg, o.msglen, "The drive is off (the Drive setting) or has no ROM (1541.rom or 1541.bin in the core's roms folder or in the shared roms folder): the disk would not be there.");
        return OPE_FAILED;
    }
    DiskEntry e;
    char image[FS_PATH_MAX];
    if (!disk_lookup(c->fs, a.s(1), &e, image, sizeof(image))) {
        snprintf(o.msg, o.msglen, "%s is no file of a disk image.", a.s(1));
        return OPE_FAILED;
    }
    if (e.type != 2) {
        snprintf(o.msg, o.msglen, "Only a PRG can be run (this is not one).");
        return OPE_FAILED;
    }
    if (d->mount(image, MM_RW, o.msg, o.msglen)) {
        pass_notice(c);
        return OPE_FAILED;
    }
    OpResult r;
    int rc;
    if (dma) {
        OpIn in = in_str("path", a.s(1));
        rc = ops_call(c, op, &in, 1, &r);
    } else {
        char script[64];
        cbm1541_load_script(d->bus_id(), e.pet, e.pet_len, script, sizeof(script));
        OpIn in = in_str("text", script);
        rc = ops_call(c, op, &in, 1, &r);
    }
    if (rc) {
        snprintf(o.msg, o.msglen, "%s", r.msg[0] ? r.msg : "the machine did not take the start");
        return OPE_FAILED;
    }
    return OPE_OK;
}

static int op_poll(CoreCtx *c, const OpArgs &, OpOut &o)
{
    o.more = cbm1541_service(c);
    return OPE_OK;
}

bool cbm1541_service(CoreCtx *c)
{
    if (!c->drive) {
        return false;
    }
    bool did = ((CbmDrive *)c->drive)->poll();
    pass_notice(c);
    return did;
}

// The free name the menu's name prompt starts with (T104.1 D15 scope): no drive needs to be attached for it
template <int FMT, bool SAVE> static const char *state_suggest(CoreCtx *c)
{
    static char buf[48];
    if (!c->io || !c->fs) {
        return 0;
    }
    if (!c->drive) {
        c->drive = make_drive(c);
    }
    CbmDrive *d = (CbmDrive *)c->drive;
    d->set_new_dir(c->media_dir);
    char stem[48];
    if (SAVE) {
        char base[40];
        snprintf(stem, sizeof(stem), "%s_saved", d->image_stem(base, sizeof(base)));
    } else {
        snprintf(stem, sizeof(stem), "blank");
    }
    d->suggest_name(stem, FMT == DF_G64 ? ".g64" : FMT == DF_D81 ? ".d81" : ".d64", buf, sizeof(buf));
    return buf;
}

// the full path of the image in the drive, for the Library's [A] mark (an unlinked or blank disk has none that is a file)
static const char *state_path(CoreCtx *c)
{
    static char p[FS_PATH_MAX];
    if (!c->drive) {
        return 0;
    }
    CbmDrive *d = (CbmDrive *)c->drive;
    if (!d->image_name() || !d->last_dir()[0]) {
        return 0;
    }
    snprintf(p, sizeof(p), "%s/%s", d->last_dir(), d->image_name());
    return p;
}

static const char *state_image(CoreCtx *c)
{
    return c->drive ? ((CbmDrive *)c->drive)->image_name() : 0;
}
static const char *state_mode(CoreCtx *c)
{
    return c->drive ? ((CbmDrive *)c->drive)->mode_name() : "empty";
}

void cbm1541_register(OpRegistry &r)
{
    r.contributor("cbm1541");
    r.add_op("drive.mount", mount_args, 3, op_mount);
    r.add_op("drive.eject", slot_arg, 1, op_eject);
    r.add_op("drive.save", save_args, 3, op_save);
    r.add_op("drive.create", create_args, 3, op_create);
    r.add_op("drive.reset", slot_arg, 1, op_reset);
    r.add_op("drive.set_rom", rom_file_args, 2, op_set_rom);
    r.add_op("drive.mount_run", run_args, 2, op_mount_run);
    r.add_op("drive.mount_run_file", run_file_args, 3, op_mount_run_file);
    r.add_op("drive.put_rom", blob_arg, 1, op_put_rom);
    r.add_op("drive.put_rom1581", blob_arg, 1, op_put_rom1581);
    r.add_op("drive.set_power", bool_arg, 1, op_set_power);
    r.add_op("drive.set_bus_id", id_arg, 1, op_set_bus_id);
    r.add_op("drive.set_ram", bool_arg, 1, op_set_ram);
    r.add_op("drive.set_swap_delay", delay_arg, 1, op_set_swap);
    r.add_op("drive.poll", 0, 0, op_poll);
    r.add_op("drive.resync", 0, 0, op_resync);
    r.add_state("drive.drive_a.path", state_path);
    r.add_state("drive.drive_a.image", state_image);
    r.add_state("drive.drive_a.mode", state_mode);
    r.add_state("drive.suggest.d64", state_suggest<DF_D64, false>);
    r.add_state("drive.suggest.g64", state_suggest<DF_G64, false>);
    r.add_state("drive.suggest.d81", state_suggest<DF_D81, false>);
    r.add_state("drive.suggest.save_d64",state_suggest<DF_D64, true>);
    r.add_state("drive.suggest.save_g64", state_suggest<DF_G64, true>);
}
