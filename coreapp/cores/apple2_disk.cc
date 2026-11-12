#include "apple2_disk.h"
#include "apple2_regs.h"
#include "engines.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void tell(CoreCtx *c, const char *text)
{
    snprintf(c->notice, sizeof(c->notice), "%s", text);
    c->notice_pending = true;
    printf("[apple2] %s\n", text);
}

A2State *a2_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new A2State();
    }
    return (A2State *)c->state;
}

const char *a2_drive_name(CoreCtx *c, int drive)
{
    A2State *st = a2_state(c);
    return drive >= 0 && drive < A2_DRIVES && st->drive[drive].file ? st->drive[drive].name : 0;
}

void a2_write_ctl(CoreCtx *c)
{
    A2State *st = a2_state(c);
    a2_hdd_write_ctl(c);
    uint8_t v = 0;
    for (int i = 0; i < A2_DRIVES; i++) {
        if (st->drive[i].file) {
            v |= (uint8_t)(1u << i);
            if (!st->drive[i].writable) {
                v |= (uint8_t)(4u << i);
            }
        }
    }
    c->io->wr(A2_DISK_CTL, v);
}

// The core counts the head in quarter tracks of its stepper: magnet 0 holds the head at 2, so whole track N is at 4N + 2 (the table of drive_ii.vhd,
// the same offset flux_drive.v of the donor subtracts), and the positions 0 and 1 are the stop below track 0. The WOZ file and the image tracks
// count from track 0 = 0.
int a2_qindex(int q) { return q >= 2 ? q - 2 : 0; }

// The track a quarter track q of a sector or nibble image shows (T89.1 D7): the tracks 0..34, the head within a quarter of a whole track
// reads it (index 4t-1, 4t, 4t+1); a half track (4t+2) and everything beyond the last track is unformatted.
static int image_track(int q)
{
    int i = a2_qindex(q);
    if (q < 0 || (i & 3) == 2) {
        return -1;
    }
    int t = (i + 1) / 4;
    return t < A2N_TRACKS ? t : -1;
}

static A2Order order_of(const A2Drive *d) { return d->fmt == A2_FMT_PRODOS ? A2_ORDER_PRODOS : A2_ORDER_DOS; }

static void put_regs(CoreCtx *c, int drive, uint32_t nbits, bool noise)
{
    CoreIo *io = c->io;
    io->wr(A2_DRV(drive) + A2_DRV_BITS, (uint8_t)(nbits & 0xFF));
    io->wr(A2_DRV(drive) + A2_DRV_BITS + 1, (uint8_t)(nbits >> 8));
    io->wr(A2_DRV(drive) + A2_DRV_FLAGS, noise ? A2_DRVF_NOISE : 0);
}

// The track for quarter track q into the window of the drive: the bits of the image's track (sector images get 10-bit sync), the nibbles of
// a .nib as 8-bit cells, the bits of a WOZ track as they are; a quarter track the image has no track for plays random flux (the noise flag).
// A buffer that already holds the track is not written again (the head stepping between the quarter positions of one track).
static bool put_track(CoreCtx *c, int drive, int q)
{
    static uint8_t bits[A2N_BUF_BITS / 8], sect[A2N_TRACK_DATA], ring[A2N_TRACK];
    A2Drive *d = &a2_state(c)->drive[drive];
    uint32_t nbits = 1000;
    bool noise = true, ok = true;
    int id = -1;
    if (d->fmt == A2_FMT_WOZ) {
        int i = a2_qindex(q);
        id = (q >= 0 && i < 160 && d->woz.tmap[i] != 0xFF) ? d->woz.tmap[i] : -1;
    } else {
        id = image_track(q);
    }
    if (id >= 0 && id == d->loaded_id) {
        put_regs(c, drive, d->loaded_bits, false);                 // the buffer holds it
        return true;
    }
    if (id < 0 && d->loaded_id == -2) {
        put_regs(c, drive, 1000, true);
        return true;
    }
    if (id >= 0) {
        if (d->fmt == A2_FMT_WOZ) {
            ok = a2w_track(&d->woz, a2_qindex(q), bits, &nbits, &noise);
        } else if (d->fmt == A2_FMT_NIB) {
            ok = d->file->read(d->off + (uint64_t)id * A2N_TRACK, ring, A2N_TRACK);
            if (ok) {
                nbits = a2n_nib_bits(ring, bits);
                noise = false;
            }
        } else {
            ok = d->file->read(d->off + (uint64_t)id * A2N_TRACK_DATA, sect, A2N_TRACK_DATA);
            if (ok) {
                nbits = a2n_sector_bits(sect, order_of(d), id, bits);
                noise = false;
            }
        }
    }
    if (!ok) {
        memset(bits, 0, sizeof(bits));
        nbits = 1000;
        noise = true;
        id = -1;
        tell(c, "A disk track could not be read (the medium is gone?).");
    }
    if (noise) {
        id = -2;
    } else {
        c->io->wr(A2_WIN_SEL, (uint8_t)(A2_SEL_TRACK1 + drive));
        c->io->rom_write(0, bits, (nbits + 7) / 8);
    }
    d->loaded_id = id;
    d->loaded_bits = nbits;
    put_regs(c, drive, nbits, noise);
    return ok;
}

// One track's bits into the file (T180 D4): 1 written, 0 dropped on purpose (read only, undecodable: said on the screen), -1 the medium refused it (full, gone):
// that one is kept by the caller and tried again at the next flush.
static int commit_track(CoreCtx *c, int drive, int q, int id, const uint8_t *bits, uint32_t nbits)
{
    static uint8_t sect[A2N_TRACK_DATA], ring[A2N_TRACK];
    A2Drive *d = &a2_state(c)->drive[drive];
    bool ok;
    if (d->fmt == A2_FMT_WOZ) {
        ok = a2w_save(&d->woz, a2_qindex(q), bits, nbits);
    } else {
        int t = id;
        static uint8_t nib[A2N_TRACK + 1];
        uint32_t n = a2n_bits_nibbles(bits, nbits, nib, A2N_TRACK);
        if (d->fmt == A2_FMT_NIB) {
            memset(ring, 0xFF, sizeof(ring));
            memcpy(ring, nib, n);
            ok = d->file->write(d->off + (uint64_t)t * A2N_TRACK, ring, A2N_TRACK);
        } else {
            // what decodes goes into the sectors of the file, the rest stays as it was (a track the guest formatted decodes whole)
            if (!d->file->read(d->off + (uint64_t)t * A2N_TRACK_DATA, sect, A2N_TRACK_DATA)) {
                tell(c, "A disk track could not be read back for the write (the medium is gone?).");
                return -1;
            }
            int got = a2n_merge_ring(nib, n, order_of(d), t, sect);
            if (got == 0) {
                tell(c, "A written disk track could not be decoded (not DOS 3.3 / ProDOS 6-and-2); it was dropped. Use a .nib or .woz image for such disks.");
                return 0;
            }
            if (got < A2N_SECTORS) {
                char m[160];
                snprintf(m, sizeof(m), "Track %d: %d of 16 written sectors could be decoded; the others keep their old contents. A .nib or .woz image keeps such a track whole.", t, got);
                tell(c, m);
            }
            ok = d->file->write(d->off + (uint64_t)t * A2N_TRACK_DATA, sect, A2N_TRACK_DATA);
        }
    }
    if (!ok) {
        tell(c, "A disk track could not be written (the medium is full or gone?).");
        return -1;
    }
    d->needs_sync = true;
    return 1;
}

// The track a write could not put into the file: its bits wait here (one per drive) until a flush gets them out.
static uint8_t g_kept[A2_DRIVES][A2N_BUF_BITS / 8];

// The core's buffer back into the file. The buffer holds the track of the request's quarter track q (the one put in last).
static bool save_track(CoreCtx *c, int drive, int q)
{
    static uint8_t bits[A2N_BUF_BITS / 8];
    A2Drive *d = &a2_state(c)->drive[drive];
    if (!d->file || d->loaded_id < 0 || d->loaded_bits == 0 || d->loaded_bits > A2N_BUF_BITS) {
        return false;                                 // random flux was playing: nothing to keep
    }
    uint32_t nbits = d->loaded_bits;
    c->io->wr(A2_WIN_SEL, (uint8_t)(A2_SEL_TRACK1 + drive));
    c->io->rom_read(0, bits, (nbits + 7) / 8);
    if (!d->writable) {
        tell(c, "The disk image is read only; the written track was dropped.");
        return false;
    }
    int r = commit_track(c, drive, q, d->loaded_id, bits, nbits);
    if (r < 0) {
        memcpy(g_kept[drive], bits, (nbits + 7) / 8);   // not lost: the flush before the medium is let go tries again
        d->kept = true;
        d->kept_q = q;
        d->kept_id = d->loaded_id;
        d->kept_bits = nbits;
    }
    return r > 0;
}

// Serves the open request of one drive. The status is read twice around the track number: the three bytes come from sampled fields
// of the core, and a request that ended while we read must not be served from stale numbers.
static bool serve(CoreCtx *c, int drive)
{
    CoreIo *io = c->io;
    uint8_t st = io->rd(A2_DRV(drive) + A2_DRV_STATUS);
    if (!(st & A2_DRVS_REQ)) {
        return false;
    }
    uint8_t trk = io->rd(A2_DRV(drive) + A2_DRV_REQ);
    uint8_t st2 = io->rd(A2_DRV(drive) + A2_DRV_STATUS);
    if (!(st2 & A2_DRVS_REQ) || ((st ^ st2) & A2_DRVS_SAVE)) {
        return true;                               // it changed under us: look again at once
    }
    if (st2 & A2_DRVS_SAVE) {
        save_track(c, drive, trk);
    } else {
        put_track(c, drive, trk);
    }
    io->wr(A2_DRV(drive) + A2_DRV_ACK, 1);
    // the acknowledge reaches the core's state machine through two synchronisers: a second ack for the same request would
    // close the NEXT one, so wait until the request is seen closed
    for (int i = 0; i < 200 && (io->rd(A2_DRV(drive) + A2_DRV_STATUS) & A2_DRVS_REQ); i++) {
        io->delay_us(20);
    }
    return true;
}

// What the core measured about the track window (T89.1 D6): the console line that says whether a copy utility's read error comes from a track
// that was late. Printed at every eject and every 30 s while an image is in.
static void report(CoreCtx *c, const char *why)
{
    CoreIo *io = c->io;
    uint32_t lat0 = 0, lat1 = 0, noise = 0;
    for (int i = 0; i < 4; i++) {
        lat0 |= (uint32_t)io->rd(A2_LAT_MAX(0) + (uint32_t)i) << (8 * i);
        lat1 |= (uint32_t)io->rd(A2_LAT_MAX(1) + (uint32_t)i) << (8 * i);
        noise |= (uint32_t)io->rd(A2_NOISE_CELLS + (uint32_t)i) << (8 * i);
    }
    unsigned r0 = io->rd(A2_REQS(0)) | io->rd(A2_REQS(0) + 1) << 8, r1 = io->rd(A2_REQS(1)) | io->rd(A2_REQS(1) + 1) << 8;
    unsigned starved = io->rd(A2_STARVED) | io->rd(A2_STARVED + 1) << 8;
    printf("[apple2] disk (%s): requests D1 %u D2 %u, longest open D1 %u us D2 %u us, latch reads without a track %u, noise cells %u\n", why, r0, r1,
           (unsigned)((uint64_t)lat0 * 1000 / 32727), (unsigned)((uint64_t)lat1 * 1000 / 32727), starved, (unsigned)noise);
}

bool a2_service(CoreCtx *c)
{
    A2State *st = a2_state(c);
    if ((st->drive[0].file || st->drive[1].file) && (int32_t)(c->io->now_ms() - st->last_report) >= 30000) {
        st->last_report = c->io->now_ms();
        report(c, "30 s");
    }
    bool more = false;
    for (int i = 0; i < A2_DRIVES; i++) {
        more |= serve(c, i);
    }
    more |= a2_hdd_service(c);
    return more;
}

int a2_flush(CoreCtx *c, int drive, char *msg, int msglen)
{
    A2State *st = a2_state(c);
    A2Drive *d = &st->drive[drive];
    if (!d->file) {
        return 0;
    }
    CoreIo *io = c->io;
    MediaSaving saving(c, d->kept || d->needs_sync || (io->rd(A2_DRV(drive) + A2_DRV_STATUS) & (A2_DRVS_DIRTY | A2_DRVS_REQ)));   // T180 D4: "Saving media..."
    if (io->rd(A2_DRV(drive) + A2_DRV_STATUS) & (A2_DRVS_DIRTY | A2_DRVS_REQ)) {
        io->wr(A2_DISK_CMD, (uint8_t)(4u << drive));         // nothing to hand over: no flush request left standing in the core
    }
    int ms = 0;
    for (;;) {
        a2_service(c);
        uint8_t s = io->rd(A2_DRV(drive) + A2_DRV_STATUS);
        if (!(s & (A2_DRVS_DIRTY | A2_DRVS_REQ))) {
            break;
        }
        if (++ms > 500) {
            snprintf(msg, msglen, "D%d: the core did not hand over its written track. It stays mounted.", drive + 1);
            return 1;
        }
        io->delay_ms(1);
    }
    if (d->kept) {                                           // T180 D4: a track an earlier write could not place
        if (commit_track(c, drive, d->kept_q, d->kept_id, g_kept[drive], d->kept_bits) < 0) {
            snprintf(msg, msglen, "D%d: writing the disk back to its file failed. It stays mounted.", drive + 1);
            return 1;
        }
        d->kept = false;
    }
    if (d->needs_sync) {
        if (!d->file->sync()) {
            snprintf(msg, msglen, "D%d: writing the disk back to its file failed. It stays mounted.", drive + 1);
            return 1;
        }
        d->needs_sync = false;
    }
    return 0;
}

void a2_flush_all(CoreCtx *c)
{
    char msg[96];
    for (int i = 0; i < A2_DRIVES; i++) {
        if (a2_flush(c, i, msg, sizeof(msg))) {
            tell(c, msg);
        }
    }
    a2_hdd_sync(c, msg, sizeof(msg));
}

// T180 D4 (CoreDesc::media_flush): both drives and both slot 7 units written back; 0, or 1 with the first reason.
int a2_media_flush(CoreCtx *c, char *msg, int msglen)
{
    char m[120];
    int bad = 0;
    for (int i = 0; i < A2_DRIVES; i++) {
        m[0] = 0;
        if (a2_flush(c, i, m, sizeof(m)) && !bad++) {
            snprintf(msg, msglen, "%s", m);
        }
    }
    m[0] = 0;
    if (!a2_hdd_sync(c, m, sizeof(m)) && !bad++) {
        snprintf(msg, msglen, "%s", m);
    }
    return bad ? 1 : 0;
}

bool a2_media_pending(CoreCtx *c)
{
    A2State *st = a2_state(c);
    for (int i = 0; i < A2_DRIVES; i++) {
        const A2Drive *d = &st->drive[i];
        if (d->file && (d->kept || d->needs_sync || (c->io->rd(A2_DRV(i) + A2_DRV_STATUS) & A2_DRVS_DIRTY))) {
            return true;
        }
        const A2Unit *u = &st->unit[i];
        if (u->file && u->needs_sync) {
            return true;
        }
    }
    return false;
}

static void drop(CoreCtx *c, int drive)
{
    A2State *st = a2_state(c);
    A2Drive *d = &st->drive[drive];
    if (d->file) {
        (st->fs ? st->fs : c->fs)->close(d->file);
    }
    memset(d, 0, sizeof(*d));
}

int a2_eject(CoreCtx *c, int drive, char *msg, int msglen)
{
    if (drive < 0 || drive >= A2_DRIVES) {
        return 1;
    }
    if (a2_flush(c, drive, msg, msglen)) {                   // T180 D4: written back first; a failure keeps the disk in the drive
        return 1;
    }
    report(c, "eject");
    drop(c, drive);
    a2_write_ctl(c);
    c->io->wr(A2_DISK_CMD, (uint8_t)(1u << drive));
    mount_notify(c, drive);                                  // T180 D2: with "Persist medium mounts" the empty drive is stored
    return 0;
}

static bool ext_is(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    return n >= e && !strcasecmp(path + n - e, ext);
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// The 64 byte header of a .2mg (https://apple2.org.za/gswv/a2zine/Docs/DiskImage_2MG_Info.txt, read 2026-10-29): "2IMG", creator, header size,
// version, format at 0x0C (0 DOS order, 1 ProDOS order, 2 nibbles), flags, blocks at 0x14, data offset 0x18, data length 0x1C.
bool a2_2mg_header(const uint8_t *h, uint64_t fsize, uint32_t *off, uint32_t *len, uint8_t *fmt)
{
    if (memcmp(h, "2IMG", 4) != 0) {
        return false;
    }
    uint32_t o = le32(h + 0x18), l = le32(h + 0x1C);
    if (o < 64 || o > fsize || l == 0 || o + (uint64_t)l > fsize) {
        return false;
    }
    *off = o;
    *len = l;
    *fmt = (uint8_t)le32(h + 0x0C);
    return true;
}

A2Kind a2_kind_of(uint64_t size, const char *ext, const uint8_t *head)
{
    if (!strcmp(ext, ".dsk") || !strcmp(ext, ".do") || !strcmp(ext, ".nib") || !strcmp(ext, ".woz")) {
        return A2K_FLOPPY;
    }
    if (!strcmp(ext, ".hdv")) {
        return A2K_HDD;
    }
    if (!strcmp(ext, ".po")) {
        return size == (uint64_t)A2N_TRACKS * A2N_TRACK_DATA ? A2K_FLOPPY : A2K_HDD;
    }
    if (!strcmp(ext, ".2mg") && head) {
        uint32_t o, l;
        uint8_t f;
        if (a2_2mg_header(head, size, &o, &l, &f)) {
            return (l == (uint32_t)A2N_TRACKS * A2N_TRACK_DATA || l == (uint32_t)A2N_TRACKS * A2N_TRACK) ? A2K_FLOPPY : A2K_HDD;
        }
    }
    return A2K_NONE;
}

static bool core_is_current(CoreCtx *c, char *msg, int msglen)
{
    if (!(c->io->rd(A2_CAPS2) & A2_CAPS2_BITS)) {
        snprintf(msg, msglen, "this Apple IIe core is older than the application (no bit level disk engine): load the matching .cor");
        return false;
    }
    return true;
}

int a2_mount(CoreCtx *c, const char *path, int drive, char *msg, int msglen)
{
    if (drive < 0 || drive >= A2_DRIVES) {
        snprintf(msg, msglen, "no such drive");
        return 1;
    }
    if (!core_is_current(c, msg, msglen)) {
        return 1;
    }
    CoreFile *f = c->fs->open(path, true);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint64_t size = f->size();
    uint32_t off = 0;
    A2Format fmt = A2_FMT_NONE;
    A2Woz woz;
    uint8_t head[64];
    bool have_head = size >= 64 && f->read(0, head, 64);
    if (ext_is(path, ".woz") || (have_head && !memcmp(head, "WOZ", 3))) {
        if (a2w_open(f, &woz, msg, msglen)) {
            c->fs->close(f);
            return 1;
        }
        fmt = A2_FMT_WOZ;
    } else if (ext_is(path, ".2mg") && have_head) {
        uint32_t len;
        uint8_t kind;
        if (!a2_2mg_header(head, size, &off, &len, &kind)) {
            c->fs->close(f);
            snprintf(msg, msglen, "%s: not a valid 2MG image", path);
            return 1;
        }
        if (len == (uint32_t)A2N_TRACKS * A2N_TRACK_DATA) {
            fmt = kind == 1 ? A2_FMT_PRODOS : A2_FMT_DOS;
        } else if (len == (uint32_t)A2N_TRACKS * A2N_TRACK) {
            fmt = A2_FMT_NIB;
        } else {
            c->fs->close(f);
            snprintf(msg, msglen, "%s holds %u bytes: that is a hard disk volume, mount it in slot 7", path, (unsigned)len);
            return 1;
        }
    } else if (size == (uint64_t)A2N_TRACKS * A2N_TRACK_DATA) {
        fmt = ext_is(path, ".po") ? A2_FMT_PRODOS : A2_FMT_DOS;
    } else if (size == (uint64_t)A2N_TRACKS * A2N_TRACK) {
        fmt = A2_FMT_NIB;
    } else {
        c->fs->close(f);
        if (size == 409600 || size == 819200) {
            snprintf(msg, msglen, "%s is a 3.5\" image (%llu bytes): the 3.5\" drive is not supported; a ProDOS block image (.po, .2mg) mounts in slot 7", path, (unsigned long long)size);
        } else {
            snprintf(msg, msglen, "%s is %llu bytes: a floppy image is 143360 (.dsk .do .po) or 232960 (.nib), or a .woz", path, (unsigned long long)size);
        }
        return 1;
    }
    if (a2_flush(c, drive, msg, msglen)) {                   // T180 D1, D4: the old disk goes back to its file first; a failure keeps it and refuses the new one
        c->fs->close(f);
        return 1;
    }
    drop(c, drive);
    A2State *st = a2_state(c);
    st->fs = c->fs;
    A2Drive *d = &st->drive[drive];
    d->file = f;
    d->fmt = fmt;
    d->off = off;
    d->loaded_id = -1;
    d->loaded_bits = 0;
    d->writable = f->writable();
    if (fmt == A2_FMT_WOZ) {
        d->woz = woz;
        if (woz.write_protect) {
            d->writable = false;
        }
    }
    const char *slash = strrchr(path, '/');
    fs_short_name(d->name, sizeof(d->name), slash ? slash + 1 : path);
    snprintf(d->path, sizeof(d->path), "%s", path);
    a2_write_ctl(c);
    c->io->wr(A2_DISK_CMD, (uint8_t)(1u << drive));
    mount_notify(c, drive);                                  // T180 D2
    printf("[apple2] D%d: %s, %s, %s\n", drive + 1, d->name,
           fmt == A2_FMT_WOZ ? "WOZ bits" : fmt == A2_FMT_NIB ? "nibbles" : fmt == A2_FMT_PRODOS ? "ProDOS order" : "DOS 3.3 order",
           d->writable ? "read/write" : "read only");
    return 0;
}
