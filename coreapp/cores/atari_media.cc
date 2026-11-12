#include "atari_media.h"
#include "atari800_regs.h"
#include "atari_atr.h"
#include "atari_xex.h"
#include "atari_cart.h"
#include "boot.h"
#include "engines.h"
#include <stdio.h>
#include <string.h>

static void tell(CoreCtx *c, const char *text)
{
    snprintf(c->notice, sizeof(c->notice), "%s", text);
    c->notice_pending = true;
    printf("[atari] %s\n", text);
}

static void say_cb(void *user, const char *text)
{
    tell((CoreCtx *)user, text);
}

AtariState *a8_state(CoreCtx *c)
{
    if (!c->state) {
        AtariState *st = new AtariState(c->io);
        st->sio.on_message(say_cb, c);
        c->state = st;
    }
    return (AtariState *)c->state;
}

// T180 D1, D4: the disk in the drive goes back to its file first, then it is let go. false and the reason in msg when the medium refuses: the old disk
// stays in the drive, the caller drops the new one.
static bool set_disk(CoreCtx *c, int drive, AtariDisk *d, const char *spec, char *msg, int msglen)
{
    AtariState *st = a8_state(c);
    MediaSaving saving(c, st->sio.pending());
    if (!st->sio.flush_drive(drive, msg, msglen)) {
        return false;
    }
    delete st->disk[drive];
    st->disk[drive] = d;
    snprintf(st->disk_spec[drive], sizeof(st->disk_spec[drive]), "%s", d && spec ? spec : "");
    mount_notify(c, drive);                                   // T180 D2: the slots are D1..D4, then the cartridge
    return true;
}

// CoreDesc::media_flush / media_pending (T180 D4)
int a8_media_flush(CoreCtx *c, char *msg, int msglen)
{
    return a8_state(c)->sio.flush_all(msg, msglen) ? 0 : 1;
}

bool a8_media_pending(CoreCtx *c)
{
    return a8_state(c)->sio.pending();
}

int a8_mount(CoreCtx *c, const char *path, int drive, bool boot, char *msg, int msglen)
{
    if (drive < 0 || drive >= SIO_DRIVES) {
        return 1;
    }
    AtariDisk *d = atr_open(c->fs, path, msg, msglen);
    if (!d) {
        return 1;
    }
    if (!set_disk(c, drive, d, path, msg, msglen)) {
        delete d;
        return 1;
    }
    printf("[atari] D%d: %s, %u sectors of %u bytes, %s\n", drive + 1, d->name(), (unsigned)d->sectors(), d->image_sector_size(),
           d->writable() ? "read/write" : "read only");
    // T108 D2 (Alex, 2026-10-16: "Ja das bequeme"): a disk put into D1: is one you want to start - the
    // machine asks D1: at a cold start and nothing else boots a disk. The caller says so with `boot`: the
    // same disk goes into D1: without a reset when a running game asks for its next disk (Alex, 2026-10-20).
    if (boot) {
        return boot_cold_reset(c, msg, msglen);
    }
    return 0;
}

int a8_eject(CoreCtx *c, int drive, char *msg, int msglen)
{
    return set_disk(c, drive, 0, 0, msg, msglen) ? 0 : 1;
}

int a8_run_xex(CoreCtx *c, const char *path, int, char *msg, int msglen)
{
    AtariDisk *d = xex_open(c->fs, path, msg, msglen);
    if (!d) {
        return 1;
    }
    char spec[FS_PATH_MAX + 8];
    snprintf(spec, sizeof(spec), "xex %s", path);
    if (!set_disk(c, 0, d, spec, msg, msglen)) {
        delete d;
        return 1;
    }
    printf("[atari] D1: %s as a boot disk (%u sectors)\n", d->name(), (unsigned)d->sectors());
    return boot_cold_reset(c, msg, msglen);
}

void a8_write_cart_reg(CoreCtx *c)
{
    AtariState *st = a8_state(c);
    c->io->wr(A8_CART1, st->cart_mode);
    c->io->wr(A8_CART2, 0);
}

int a8_insert_cart(CoreCtx *c, const char *path, int, char *msg, int msglen)
{
    AtariState *st = a8_state(c);
    if (media_writeback(c, WB_WHY_RESET, msg, msglen)) {          // T180 D4: the machine is held for the image and restarts; the disks first
        return 1;
    }
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint8_t head[16];
    memset(head, 0, sizeof(head));
    uint32_t size = fs_size32(f->size());
    if (size >= 16 && !f->read(0, head, 16)) {
        c->fs->close(f);
        snprintf(msg, msglen, "cannot read %s", path);
        return 1;
    }
    CartInfo ci = cart_probe(head, size);
    if (!ci.ok || ci.size > 0x400000u) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s", ci.ok ? "a cartridge image is 4 MB at most" : ci.err);
        return 1;
    }
    // the core is held while the image goes in; the window is the DDR2 behind the core's cartridge cache
    c->desc->init(c, msg, msglen);
    static uint8_t chunk[32768];
    uint32_t sum = 0;
    for (uint32_t off = 0; off < ci.size; off += sizeof(chunk)) {
        uint32_t n = ci.size - off < sizeof(chunk) ? ci.size - off : sizeof(chunk);
        if (!f->read(ci.data_off + off, chunk, n)) {
            c->fs->close(f);
            snprintf(msg, msglen, "cannot read %s (the medium is gone?)", path);
            return 1;                                 // held: the menu stays up, Reset or another image gets it going
        }
        sum += cart_checksum(chunk, n);
        c->io->ddr_write(A8_W_CART + off, chunk, n);
    }
    c->fs->close(f);
    if (ci.car_type) {
        uint32_t want = ((uint32_t)head[8] << 24) | (head[9] << 16) | (head[10] << 8) | head[11];
        if (want != sum) {
            printf("[atari] cartridge checksum %08x, the .car says %08x (loaded anyway)\n", (unsigned)sum, (unsigned)want);
            tell(c, "The cartridge file's checksum does not match its header; loaded anyway.");
        }
    }
    st->cart_mode = ci.mode;
    const char *slash = strrchr(path, '/');
    fs_short_name(st->cart_name, sizeof(st->cart_name), slash ? slash + 1 : path);
    snprintf(st->cart_path, sizeof(st->cart_path), "%s", path);
    a8_write_cart_reg(c);                                     // the machine is held: the type is in the core for the reset that follows (T180 D2: the stored media are mounted at the start, and a8_init has run before)
    mount_notify(c, SIO_DRIVES);                              // T180 D2
    printf("[atari] cartridge %s: mode %02x, %u bytes\n", st->cart_name, ci.mode, (unsigned)ci.size);
    return boot_cold_reset(c, msg, msglen);
}

int a8_remove_cart(CoreCtx *c, char *msg, int msglen)
{
    AtariState *st = a8_state(c);
    st->cart_mode = 0;
    st->cart_name[0] = 0;
    st->cart_path[0] = 0;
    if (c->media_restoring) {
        a8_write_cart_reg(c);                                 // no reset follows in this call: the type is taken at the caller's
    }
    mount_notify(c, SIO_DRIVES);                              // T180 D2
    return boot_cold_reset(c, msg, msglen);
}

// ---- T180 D2, D5: the slots D1..D4 and the cartridge for the stored mounts and the media sets
const char *a8_slot_spec(CoreCtx *c, int slot)
{
    AtariState *st = a8_state(c);
    const char *p = slot < SIO_DRIVES ? (st->disk[slot] ? st->disk_spec[slot] : "") : (st->cart_mode ? st->cart_path : "");
    return p[0] ? p : 0;
}

int a8_slot_mount(CoreCtx *c, int slot, const char *spec, char *msg, int msglen)
{
    if (slot >= SIO_DRIVES) {
        return a8_insert_cart(c, spec, 0, msg, msglen);
    }
    if (!strncmp(spec, "xex ", 4)) {
        return slot == 0 ? a8_run_xex(c, spec + 4, 0, msg, msglen) : (snprintf(msg, msglen, "a .xex boot disk goes into D1 only"), 1);
    }
    return a8_mount(c, spec, slot, false, msg, msglen);
}

int a8_slot_eject(CoreCtx *c, int slot, char *msg, int msglen)
{
    return slot >= SIO_DRIVES ? a8_remove_cart(c, msg, msglen) : a8_eject(c, slot, msg, msglen);
}

bool a8_service(CoreCtx *c)
{
    AtariState *st = a8_state(c);
    bool busy = st->sio.poll();
    st->sio.flush_idle(1000);
    return busy;
}
