#include "sectors.h"
#include "engines.h"
#include "mediamounts.h"
#include "folderhd_api.h"
#include "textcopy.h"
#include <stdio.h>
#include <string.h>

static const FolderHdApi *g_folderhd;

void sec_set_folderhd(const FolderHdApi *api)
{
    g_folderhd = api;
}

static const SectorMediaSpec *spec_of(CoreCtx *c)
{
    return c->desc->sector_media;
}

static const char *base_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

const char *sec_name(CoreCtx *c, int u)
{
    if (!spec_of(c) || !c->eng || u < 0 || u >= SEC_UNITS_MAX || !c->eng->sec.unit[u].mounted) {
        return 0;
    }
    return c->eng->sec.unit[u].name;
}

static bool unit_pending(const SecUnit &h)
{
    return h.mounted && h.file && (h.unsynced || (h.folder && g_folderhd && g_folderhd->dirty(h.file)));
}

// T180 D4: everything pending for the unit is on its medium (a folder disk: the full fold-back). False with the reason in msg: the unit is left as it is.
static bool sync_unit(CoreCtx *c, int u, char *msg, int msglen)
{
    SecUnit &h = engines_of(c)->sec.unit[u];
    if (!unit_pending(h)) {
        return true;
    }
    MediaSaving saving(c, true);
    bool ok = h.file->sync();
    h.unsynced = h.folder ? (g_folderhd && g_folderhd->dirty(h.file)) : !ok;      // a fold-back that failed is tried again at idle
    if (!ok) {
        h.unsynced = true;
        if (msg && msglen) {
            snprintf(msg, msglen, "%s: writing the disk back failed. It stays mounted.", h.name);
        }
    }
    return ok;
}

// The unit is let go: written back first (T180 D4), then announced absent and closed. 5 and the reason in msg when the write-back failed - nothing is closed then.
static int eject_unit(CoreCtx *c, int u, char *msg, int msglen)
{
    const SectorMediaSpec *s = spec_of(c);
    SecUnit &h = engines_of(c)->sec.unit[u];
    if (!sync_unit(c, u, msg, msglen)) {
        return 5;
    }
    c->io->wr(s->mount_reg + s->mount_step * u, 0);
    h.mounted = false;
    h.unsynced = false;
    if (h.file) {
        if (h.folder) {
            g_folderhd->unmount(h.file);
        } else {
            c->fs->close(h.file);
        }
        h.file = 0;
    }
    h.folder = false;
    h.name[0] = 0;
    h.spec[0] = 0;
    return 0;
}

int sec_eject(CoreCtx *c, int u, char *msg, int msglen)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || u < 0 || u >= s->nunits) {
        snprintf(msg, msglen, "no such disk");
        return 1;
    }
    int rc = eject_unit(c, u, msg, msglen);
    if (!rc) {
        mounts_changed(c, true, u);                                // T180 D2: with "Persist medium mounts" the empty unit is stored
    }
    return rc;
}

// What the size register takes: sectors where the gateware says so (feature bit), bytes otherwise.
static uint64_t size_value(CoreCtx *c, uint64_t size)
{
    const SectorMediaSpec *s = spec_of(c);
    return (c->io->rd(s->feat_reg) & s->feat_sectors) ? size / SEC_BYTES : size;
}

// The size, then the present bit.
static void announce(CoreCtx *c, int u, uint32_t reg)
{
    const SectorMediaSpec *s = spec_of(c);
    for (int i = 0; i < 4; i++) {
        c->io->wr(s->mount_reg + s->mount_step * u + 4 + i, (uint8_t)(reg >> (8 * i)));
    }
    c->io->wr(s->mount_reg + s->mount_step * u, 1);
}

int sec_mount(CoreCtx *c, int u, const char *path, char *msg, int msglen)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || u < 0 || u >= s->nunits) {
        snprintf(msg, msglen, "This machine has no such disk.");
        return 1;
    }
    CoreFile *file = c->fs->open(path, true);
    if (!file) {
        snprintf(msg, msglen, "%s: cannot open the file", base_of(path));
        return 2;
    }
    bool writable = file->writable();
    uint64_t size = file->size();                  // 64 bit: an image of 4 GiB and more is the open file's size
    uint64_t v = size_value(c, size);
    if (size < SEC_BYTES || v > 0xFFFFFFFFull) {
        const char *why = size < SEC_BYTES ? "This disk image is smaller than one sector."
                                           : "This disk image is 4 GiB or more: the core needs the size in sectors (feature bit).";
        c->fs->close(file);
        snprintf(msg, msglen, "%s", why);
        return 3;
    }
    if (eject_unit(c, u, msg, msglen)) {              // T180 D1: the old medium is let go completely first; one that cannot be written back stays
        c->fs->close(file);
        return 4;
    }
    SecUnit &h = engines_of(c)->sec.unit[u];
    h.file = file;
    h.folder = false;
    h.writable = writable;
    h.size = size;
    h.unsynced = false;
    h.mounted = true;
    {
        const char *suf = writable ? "" : " (read only)";
        char shown[64];
        fs_short_name(shown, sizeof(h.name) - strlen(suf), base_of(path));
        snprintf(h.name, sizeof(h.name), "%s%s", shown, suf);
    }
    h.spec[0] = 0;
    if (strlen(path) < sizeof(h.spec)) {
        snprintf(h.spec, sizeof(h.spec), "%s", path);              // a path that does not fit is mounted but not stored
    }
    announce(c, u, (uint32_t)v);
    mounts_changed(c, true, u);                                    // T180 D2
    return 0;
}

// The most cylinders (cylinder 0 included) the core's size register can announce for a disk made of whole cylinders.
static uint32_t folder_max_cylinders(CoreCtx *c)
{
    const SectorMediaSpec *s = spec_of(c);
    uint64_t max_bytes = (c->io->rd(s->feat_reg) & s->feat_sectors) ? 0xFFFFFFFFull * SEC_BYTES : 0xFFFFFFFFull;
    return g_folderhd->max_cylinders(max_bytes);
}

const char *sec_folder_offer(CoreCtx *c)
{
    static char text[16];
    text[0] = 0;
    if (!spec_of(c) || !g_folderhd) {
        return text;                                          // no folder disk library in this app: nothing to suggest
    }
    if (!c->media_dir[0]) {
        snprintf(text, sizeof(text), "1");                    // no folder chosen yet: nothing to check it against
        return text;
    }
    uint64_t one = 1ull << 30;
    uint64_t bytes = g_folderhd->offer_bytes(c->fs, c->media_dir, folder_max_cylinders(c), one);
    if (bytes > one) bytes = one;                          // the suggestion is 1 GB; the offer above that is checked when the person types more
    if (bytes >= one) {
        snprintf(text, sizeof(text), "1");
    } else if (bytes >= one / 100) {
        snprintf(text, sizeof(text), "0.%02u", (unsigned)(bytes * 100 / one));
    }
    return text;
}

int sec_mount_folder(CoreCtx *c, int u, const char *spec, char *msg, int msglen)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || u < 0 || u >= s->nunits) {
        snprintf(msg, msglen, "This machine has no such disk.");
        return 1;
    }
    if (!g_folderhd) {
        snprintf(msg, msglen, "This app has no folder disk.");
        return 1;
    }
    char folder[260];
    const char *slash = strrchr(spec, '/');
    if (!slash || slash == spec) {
        snprintf(msg, msglen, "Choose a folder on a medium.");
        return 2;
    }
    nf_copy(folder, sizeof(folder), spec, (size_t)(slash - spec));
    const char *gb = slash + 1;
    uint32_t maxcyl = folder_max_cylinders(c);
    uint64_t want;
    if (!g_folderhd->parse_gb(gb, 1ull << 30, &want)) {
        snprintf(msg, msglen, "\"%s\" is not a size in GB.", gb);
        return 3;
    }
    if (!sync_unit(c, u, msg, msglen)) {                         // T180 D1/D4: what the old medium holds is on its medium before the new tree is looked at (the same folder may be mounted again)
        return 6;
    }
    uint64_t offer = g_folderhd->offer_bytes(c->fs, folder, maxcyl, want);
    if (!gb[0] && offer < want) {
        want = offer;                                           // nothing typed: the suggestion, which is less than 1 GB where the medium has less
    }
    if (want > offer) {
        snprintf(msg, msglen, "%s GB is more than this medium and machine can back: at most %u MB.", gb, (unsigned)(offer >> 20));
        return 3;
    }
    CoreFile *disk = g_folderhd->mount(c->fs, c->io, u, folder, want, maxcyl, msg, msglen);
    if (!disk) {
        return 4;
    }
    uint64_t size = disk->size();
    uint64_t v = size_value(c, size);
    if (v > 0xFFFFFFFFull) {                                  // the maximum above keeps this from happening; a defect would show here, not as a wrong size
        g_folderhd->unmount(disk);
        snprintf(msg, msglen, "This disk is too large for the machine's size register.");
        return 5;
    }
    if (eject_unit(c, u, msg, msglen)) {                         // T180 D1: the old medium is let go completely; a refused new folder left it where it was
        g_folderhd->unmount(disk);
        return 6;
    }
    SecUnit &h = engines_of(c)->sec.unit[u];
    h.file = disk;
    h.folder = true;
    h.writable = true;
    h.size = size;
    h.unsynced = false;
    h.mounted = true;
    FolderHdInfo fi;
    g_folderhd->info(disk, &fi);
    snprintf(h.name, sizeof(h.name), "%s%s", base_of(folder), fi.host_writable ? " (folder)" : " (read only, changes lost at eject)");
    // T180 D2: what is stored for it keeps the size as typed ("folder /usb0/Amiga/DH0, 2 GB"); no size typed: the default again at the next start
    h.spec[0] = 0;
    if (strlen(folder) + strlen(gb) + 16 < sizeof(h.spec)) {
        if (gb[0]) {
            snprintf(h.spec, sizeof(h.spec), "folder %s, %s GB", folder, gb);
        } else {
            snprintf(h.spec, sizeof(h.spec), "folder %s", folder);
        }
    }
    announce(c, u, (uint32_t)v);
    mounts_changed(c, true, u);
    printf("[sectors] unit %d: folder %s, %u cylinders, %u entries; %s\n", u, folder, (unsigned)fi.cylinders, (unsigned)fi.nodes,
           fi.report[0] ? fi.report : "nothing left out");
    snprintf(msg, msglen, "%s: %u MB, %u entries", base_of(folder), (unsigned)(size >> 20), (unsigned)fi.nodes);
    return 0;
}

bool sec_pending(CoreCtx *c)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || !c->eng) {
        return false;
    }
    for (int u = 0; u < s->nunits; u++) {
        if (unit_pending(c->eng->sec.unit[u])) {
            return true;
        }
    }
    return false;
}

bool sec_sync_all(CoreCtx *c, char *msg, int msglen)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || !c->eng) {
        return true;
    }
    bool all = true;
    MediaSaving saving(c, sec_pending(c));
    for (int u = 0; u < s->nunits; u++) {
        if (!sync_unit(c, u, all ? msg : 0, msglen)) {
            all = false;
        }
    }
    return all;
}

void sec_reannounce(CoreCtx *c)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || !c->eng) {
        return;
    }
    for (int u = 0; u < s->nunits; u++) {
        if (c->eng->sec.unit[u].mounted) {
            announce(c, u, (uint32_t)size_value(c, c->eng->sec.unit[u].size));   // the size too: after a Reload core the register is empty
        }
    }
}

// One request of the mailbox: 512 bytes between the file and the staging sector.
static bool serve_one(CoreCtx *c)
{
    const SectorMediaSpec *s = spec_of(c);
    uint8_t st = c->io->rd(s->mbox_status);
    if (!(st & 1)) {
        return false;
    }
    int u = (st >> 2) & 1;
    bool wr = (st & 2) != 0;
    uint32_t lba = 0;
    for (int i = 3; i >= 0; i--) {
        lba = (lba << 8) | c->io->rd(s->mbox_lba + i);
    }
    SecUnit &h = c->eng->sec.unit[u < s->nunits ? u : 0];
    uint8_t sec[SEC_BYTES];
    uint32_t t0 = c->io->now_ms();
    uint64_t off = (uint64_t)lba * SEC_BYTES;                   // 64 bit: a sector past 4 GiB
    bool inside = u < s->nunits && h.mounted && off + SEC_BYTES <= h.size;
    if (wr) {
        if (inside && h.writable) {
            c->io->ddr_read(s->stage_off, sec, SEC_BYTES);
            if (!h.file->write(off, sec, SEC_BYTES)) {
                printf("[sectors] unit %d: write of sector %u failed\n", u, (unsigned)lba);
            } else {
                h.unsynced = true;
                h.last_write_ms = c->io->now_ms();
            }
        }
    } else {
        memset(sec, 0, sizeof(sec));                            // beyond the file: zeros
        if (inside && !h.file->read(off, sec, SEC_BYTES)) {
            printf("[sectors] unit %d: read of sector %u failed\n", u, (unsigned)lba);
            memset(sec, 0, sizeof(sec));
        }
        c->io->ddr_write(s->stage_off, sec, SEC_BYTES);
    }
    {   // a request that took long is what a machine with its own timeout (the Next's firmware gives a block about 65 ms) gives up on: say which one, 20 lines at most
        static int slow_lines = 0;
        uint32_t dt = c->io->now_ms() - t0;
        if (dt >= 20 && slow_lines < 20) {
            slow_lines++;
            printf("[sectors] unit %d: sector %u %s took %u ms\n", u, (unsigned)lba, wr ? "write" : "read", (unsigned)dt);
        }
    }
    c->io->wr(s->mbox_done, (uint8_t)(st & s->done_tag_mask));       // the status read at the start names the request this answers
    return true;
}

int sec_service(CoreCtx *c, int max)
{
    if (!spec_of(c)) {
        return 0;
    }
    engines_of(c);
    int n = 0;
    while (n < max && serve_one(c)) {
        n++;
    }
    return n;
}

void sec_idle(CoreCtx *c)
{
    const SectorMediaSpec *s = spec_of(c);
    if (!s || !c->eng) {
        return;
    }
    uint32_t now = c->io->now_ms();
    for (int u = 0; u < s->nunits; u++) {
        SecUnit &h = c->eng->sec.unit[u];
        if (h.mounted && h.unsynced && now - h.last_write_ms >= s->idle_sync_ms) {
            if (h.folder) {
                if (!g_folderhd->idle_slice(h.file)) {           // the fold-back runs in slices of FH_SLICE_MS between two polls
                    h.unsynced = g_folderhd->dirty(h.file);
                }
            } else {
                h.file->sync();
                h.unsynced = false;
            }
        }
    }
}
