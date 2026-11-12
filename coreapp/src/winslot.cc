#include "winslot.h"
#include "engines.h"
#include "mediamounts.h"
#include <stdio.h>
#include <string.h>

#define CHUNK 32768
#define TRACK_MAX 8192

static uint8_t chunk[CHUNK];                 // the image in blocks, the zero fill
static uint8_t track_buf[TRACK_MAX];         // one track on its way to the file

static const WindowMediaSpec *spec_of(CoreCtx *c)
{
    return c->desc->window_media;
}

static const char *base_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void notice(CoreCtx *c, const char *text)
{
    snprintf(c->notice, sizeof(c->notice), "%s", text);
    c->notice_pending = true;
}

void win_mark_dirty(CoreCtx *c, int n, int track)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w || n < 0 || n >= w->nslots) {
        return;
    }
    WinSlot &f = engines_of(c)->win.slot[n];
    if (!f.mounted || !f.writable || track < 0 || track >= w->ntracks) {
        return;
    }
    uint32_t now = c->io->now_ms();
    if (!(f.dirty[track >> 3] & (1 << (track & 7)))) {
        f.dirty[track >> 3] |= (uint8_t)(1 << (track & 7));
        if (f.ndirty++ == 0) {
            f.first_ms = now;
        }
    }
    f.last_ms = now;
}

static void mark_all_dirty(CoreCtx *c)
{
    const WindowMediaSpec *w = spec_of(c);
    for (int n = 0; n < w->nslots; n++) {
        for (int t = 0; t < w->ntracks; t++) {
            win_mark_dirty(c, n, t);
        }
    }
}

// Each marked track from the window to the file at track * track_bytes; one sync at the end.
static bool flush_drive(CoreCtx *c, int n)
{
    const WindowMediaSpec *w = spec_of(c);
    WinSlot &f = engines_of(c)->win.slot[n];
    if (!f.mounted || !f.writable || (f.ndirty == 0 && !f.unsynced)) {
        return true;
    }
    MediaSaving saving(c, true);
    bool ok = true;
    for (int t = 0; t < w->ntracks; t++) {
        if (!(f.dirty[t >> 3] & (1 << (t & 7)))) {
            continue;
        }
        uint32_t off = (uint32_t)t * w->track_bytes;
        if (off + w->track_bytes > f.size) {
            f.dirty[t >> 3] &= (uint8_t)~(1 << (t & 7));      // beyond the file: nothing to write it to
            f.ndirty--;
            continue;
        }
        c->io->ddr_read(w->base + (uint32_t)n * w->stride + off, track_buf, w->track_bytes);
        if (!f.file->write(off, track_buf, w->track_bytes)) {
            ok = false;
            break;
        }
        f.unsynced = true;
        f.dirty[t >> 3] &= (uint8_t)~(1 << (t & 7));
        f.ndirty--;
    }
    if (ok) {
        ok = f.file->sync();
        f.unsynced = !ok;
    }
    if (!ok && !f.err_told) {
        char m[120];
        snprintf(m, sizeof(m), "%s%d: writing the disk back to its file failed. The image stays in memory.", w->prefix, n);
        notice(c, m);
        f.err_told = true;
    } else if (ok) {
        f.err_told = false;
    }
    return ok;
}

bool win_pending(CoreCtx *c)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w || !c->eng) {
        return false;
    }
    for (int n = 0; n < w->nslots; n++) {
        WinSlot &f = c->eng->win.slot[n];
        if (f.mounted && f.writable && (f.ndirty || f.unsynced)) {
            return true;
        }
    }
    return false;
}

static void failed(CoreCtx *c, int n, char *msg, int msglen)
{
    if (msg && msglen) {
        snprintf(msg, msglen, "%s%d: writing the disk back to its file failed. It stays mounted.", spec_of(c)->prefix, n);
    }
}

bool win_flush_all(CoreCtx *c, char *msg, int msglen)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w || !c->eng) {
        return true;
    }
    bool all = true;
    for (int n = 0; n < w->nslots; n++) {
        if (!flush_drive(c, n)) {
            if (all) {
                failed(c, n, msg, msglen);
            }
            all = false;
        }
    }
    return all;
}

void win_reannounce(CoreCtx *c)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w || !c->eng) {
        return;
    }
    for (int n = 0; n < w->nslots; n++) {
        if (c->eng->win.slot[n].mounted) {
            c->io->wr(w->mount_reg + w->mount_step * n, 1);
        }
    }
}

const char *win_name(CoreCtx *c, int n)
{
    if (!spec_of(c) || !c->eng || n < 0 || n >= WIN_SLOTS_MAX || !c->eng->win.slot[n].mounted) {
        return 0;
    }
    return c->eng->win.slot[n].name;
}

// The disk is let go: written back first, announced absent, closed. 5 and the reason when the write-back failed. Stores nothing (the callers do).
static int eject_slot(CoreCtx *c, int n, char *msg, int msglen)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w || n < 0 || n >= w->nslots) {
        snprintf(msg, msglen, "no such drive");
        return 1;
    }
    Engines *e = engines_of(c);
    WinSlot &f = e->win.slot[n];
    if (f.mounted && !flush_drive(c, n)) {         // this disk only; the hard disks are not a floppy's business (a folder disk's fold-back is the idle work and takes seconds)
        failed(c, n, msg, msglen);                 // T180 D4: the disk stays mounted, nothing is dropped
        return 5;
    }
    c->io->wr(w->mount_reg + w->mount_step * n, 0);
    f.mounted = false;
    if (f.file) {
        c->fs->close(f.file);
        f.file = 0;
    }
    memset(f.dirty, 0, sizeof(f.dirty));
    f.ndirty = 0;
    f.unsynced = false;
    f.name[0] = 0;
    f.spec[0] = 0;
    return 0;
}

int win_eject(CoreCtx *c, int n, char *msg, int msglen)
{
    int rc = eject_slot(c, n, msg, msglen);
    if (!rc) {
        mounts_changed(c, false, n);                // T180 D2: with "Persist medium mounts" the empty slot is stored
    }
    return rc;
}

int win_mount(CoreCtx *c, int n, const char *path, char *msg, int msglen)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w || n < 0 || n >= w->nslots) {
        snprintf(msg, msglen, "This machine has no such drive.");
        return 1;
    }
    uint64_t size = 0;
    bool dir = false;
    if (!c->fs->stat(path, &size, &dir) || dir) {
        snprintf(msg, msglen, "%s: cannot open the file", base_of(path));
        return 2;
    }
    if (size == 0 || size > w->max_image) {
        snprintf(msg, msglen, "%s: %u bytes. A disk image is %u bytes at most.", base_of(path), (unsigned)fs_size32(size), (unsigned)w->max_image);
        return 3;
    }
    CoreFile *file = c->fs->open(path, true);      // a read only file or medium gives a read only file
    if (!file) {
        snprintf(msg, msglen, "%s: cannot open the file", base_of(path));
        return 2;
    }
    bool writable = file->writable();
    if (eject_slot(c, n, msg, msglen)) {           // the old disk: flushed, announced absent, closed (T180 D1); one that cannot be written back stays
        c->fs->close(file);
        return 5;
    }
    uint32_t slot = w->base + (uint32_t)n * w->stride;
    memset(chunk, 0, sizeof(chunk));
    for (uint32_t o = 0; o < w->stride; o += sizeof(chunk)) {
        c->io->ddr_write(slot + o, chunk, w->stride - o < sizeof(chunk) ? w->stride - o : sizeof(chunk));
    }
    for (uint64_t done = 0; done < size;) {
        uint32_t want = size - done > CHUNK ? CHUNK : (uint32_t)(size - done);
        if (!file->read(done, chunk, want)) {
            c->fs->close(file);
            snprintf(msg, msglen, "%s: reading the file failed", base_of(path));
            return 4;
        }
        c->io->ddr_write(slot + (uint32_t)done, chunk, want);
        done += want;
    }
    WinSlot &f = engines_of(c)->win.slot[n];
    f.file = file;
    f.writable = writable;
    f.size = (uint32_t)size;
    f.err_told = false;
    f.unsynced = false;
    memset(f.dirty, 0, sizeof(f.dirty));
    f.ndirty = 0;
    f.mounted = true;
    {
        const char *suf = writable ? "" : " (read only)";
        char shown[64];
        fs_short_name(shown, sizeof(f.name) - strlen(suf), base_of(path));     // the status line shows it: a long name keeps its start and its end
        snprintf(f.name, sizeof(f.name), "%s%s", shown, suf);
    }
    f.spec[0] = 0;
    if (strlen(path) < sizeof(f.spec)) {
        snprintf(f.spec, sizeof(f.spec), "%s", path);   // a path that does not fit is mounted but not stored
    }
    c->io->wr(w->mount_reg + w->mount_step * n, 1);
    mounts_changed(c, false, n);                   // T180 D2
    return 0;
}

void win_service(CoreCtx *c)
{
    const WindowMediaSpec *w = spec_of(c);
    if (!w) {
        return;
    }
    if (c->io->rd(w->dlog_status) & 2) {           // lost entries: every track of every writable drive is suspect
        c->io->wr(w->dlog_status, 0x02);
        mark_all_dirty(c);
    }
    for (int i = 0; i < 64 && (c->io->rd(w->dlog_status) & 1); i++) {
        int track = c->io->rd(w->dlog_track);
        int drive = c->io->rd(w->dlog_drive) & 3;
        c->io->wr(w->dlog_pop, 1);
        c->io->delay_us(10);                       // the head moves on through two synchronisers
        win_mark_dirty(c, drive, track);
    }
    // a burst of writes is over (idle_flush_ms without a new entry), or the drive has been dirty for max_dirty_ms
    uint32_t now = c->io->now_ms();
    for (int n = 0; n < w->nslots; n++) {
        WinSlot &f = c->eng->win.slot[n];
        if (f.mounted && f.writable && f.ndirty && (now - f.last_ms >= w->idle_flush_ms || now - f.first_ms >= w->max_dirty_ms)) {
            flush_drive(c, n);
        }
    }
}
