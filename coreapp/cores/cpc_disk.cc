#include "cpc_disk.h"
#include "cpc_regs.h"
#include "engines.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

namespace {

const uint16_t dirty_reg[2] = { CPC_D0_DIRTY, CPC_D1_DIRTY };
const uint16_t base_reg[2] = { CPC_D0_BASE, CPC_D1_BASE };
const uint16_t size_reg[2] = { CPC_D0_SIZE, CPC_D1_SIZE };

void wr32(CoreIo *io, uint16_t reg, uint32_t v)
{
    for (int k = 0; k < 4; k++) {
        io->wr(reg + k, (uint8_t)(v >> (8 * k)));
    }
}

void say(CoreCtx *c, const char *text)
{
    snprintf(c->notice, sizeof(c->notice), "%s", text);
    c->notice_pending = true;
}

uint32_t rd32(CoreIo *io, uint16_t reg)
{
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        v |= (uint32_t)io->rd(reg + k) << (8 * k);
    }
    return v;
}

// T92 #17 probe: the registers of the CPC core that tell the hang's cause apart (cpc_regs.v 0A0, 0A4, 0A8, 0AC, 180-191, 1A0, 1B0).
struct Snap {
    uint8_t st, ds, ctl, wp, dy0, dy1;
    uint32_t b0, s0, b1, s1, fr;
    uint16_t pc;
};

void snap(CoreIo *io, Snap &n)
{
    n.st = io->rd(CPC_STATUS);
    n.ds = io->rd(CPC_DISK_STATUS);
    n.ctl = io->rd(CPC_CTRL);
    n.wp = io->rd(CPC_DISK_CTRL);
    n.b0 = rd32(io, CPC_D0_BASE);
    n.s0 = rd32(io, CPC_D0_SIZE);
    n.b1 = rd32(io, CPC_D1_BASE);
    n.s1 = rd32(io, CPC_D1_SIZE);
    n.dy0 = n.dy1 = 0;
    for (int m = 0; m < 8; m++) {
        n.dy0 |= io->rd(CPC_D0_DIRTY + m);
        n.dy1 |= io->rd(CPC_D1_DIRTY + m);
    }
    n.pc = (uint16_t)(io->rd(0x0AC) | (io->rd(0x0AD) << 8));
    n.fr = (uint32_t)(io->rd(0x0A8) | (io->rd(0x0A9) << 8));
}

void print_snap(const char *tag, uint32_t now, const Snap &n)
{
    printf("[cpc] %u %s st=%02x ds=%x ctl=%02x wp=%02x A=%08x/%u B=%08x/%u dy=%02x%02x pc=%04x fr=%u", (unsigned)now, tag, n.st, n.ds, n.ctl, n.wp,
           (unsigned)n.b0, (unsigned)n.s0, (unsigned)n.b1, (unsigned)n.s1, n.dy0, n.dy1, n.pc, (unsigned)n.fr);
}

char letter(int drive) { return (char)('A' + drive); }

// The chunks of drive `d` that are marked go to the file. False when one could not be written; the marks stay.
bool write_back(CoreCtx *c, CpcDisk &disk, int drive)
{
    static uint8_t buf[CPC_DIRTY_CHUNK];
    for (int n = 0; n < 64; n++) {
        if (!(disk.pending & (1ull << n))) {
            continue;
        }
        uint32_t off = (uint32_t)n * CPC_DIRTY_CHUNK;
        if (off >= disk.size) {
            disk.pending &= ~(1ull << n);
            continue;
        }
        uint32_t len = disk.size - off < CPC_DIRTY_CHUNK ? disk.size - off : CPC_DIRTY_CHUNK;
        c->io->ddr_read(CPC_W_DISK(drive) + off, buf, len);
        disk.unsynced = true;                      // from the first write on, the file is not on the medium until a sync says so
        if (!disk.file->write(off, buf, len)) {
            return false;
        }
        disk.pending &= ~(1ull << n);
    }
    if (disk.file->sync()) {
        disk.unsynced = false;                     // T180 D4: a sync that failed is not forgotten with the cleared marks
        return true;
    }
    return false;
}

bool flush_drive(CoreCtx *c, CpcDisk &disk, int drive)
{
    if (!disk.mounted || (!disk.pending && !disk.unsynced)) {
        return true;
    }
    MediaSaving saving(c, true);
    if (write_back(c, disk, drive)) {
        disk.err_told = false;
        return true;
    }
    if (!disk.err_told) {
        char t[120];
        snprintf(t, sizeof(t), "%c: could not be written to its file (medium read only or gone). The disk stays in memory.", letter(drive));
        say(c, t);
        disk.err_told = true;
    }
    return false;
}

void ctrl(CoreCtx *c, const CpcDisks *s, uint8_t actions)
{
    c->io->wr(CPC_DISK_CTRL, (uint8_t)(actions | (s->wp_bits << 4)));
}

}  // namespace

int cpcd_mount(CoreCtx *c, CpcDisks *s, int drive, const char *path, char *msg, int msglen)
{
    static uint8_t chunk[32768];
    CoreFile *f = c->fs->open(path, true);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint64_t size = f->size();
    uint8_t head[16] = { 0 };
    if (size < 256 || !f->read(0, head, sizeof(head))) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s is not a DSK file (too short or unreadable)", path);
        return 1;
    }
    if (memcmp(head, "MV - CPC", 8) != 0 && memcmp(head, "EXTENDED CPC DSK", 16) != 0) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s is not a DSK file (no MV - CPC or EXTENDED CPC DSK header)", path);
        return 1;
    }
    if (size > CPC_DSK_MAX) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s is larger than the 3 inch drive's 880 KB", path);
        return 1;
    }
    CpcDisk &d = s->d[drive];
    if (d.mounted) {
        cpcd_poll(c, s, false);                    // T180 D1: the last marks of the FDC are in `pending` before the old disk goes
    }
    if (d.mounted && !flush_drive(c, d, drive)) {
        c->fs->close(f);
        snprintf(msg, msglen, "%c: could not be written back to its file - the new disk is not mounted", letter(drive));
        return 1;
    }
    bool gone = d.mounted;
    if (d.mounted) {
        cpcd_event(c, s, 'e', "eject %c (mount over)", letter(drive));
        ctrl(c, s, CPC_DISK_EJECT(drive));
        c->fs->close(d.file);
        memset(&d, 0, sizeof(d));
    }
    // the image, byte for byte, into the slot; a read that fails half way leaves the drive empty
    bool ok = true;
    for (uint32_t o = 0; o < (uint32_t)size && ok; o += sizeof(chunk)) {
        uint32_t n = (uint32_t)size - o < sizeof(chunk) ? (uint32_t)size - o : (uint32_t)sizeof(chunk);
        ok = f->read(o, chunk, n);
        if (ok) {
            c->io->ddr_write(CPC_W_DISK(drive) + o, chunk, n);
        }
    }
    if (!ok) {
        c->fs->close(f);
        snprintf(msg, msglen, "cannot read %s (the medium is gone?)", path);
        if (gone) {
            mount_notify(c, drive);                // the old disk is out and the drive is empty
        }
        return 1;
    }
    for (int m = 0; m < 8; m++) {                  // marks of an earlier image are not this one's
        c->io->wr(dirty_reg[drive] + m, 0xFF);
    }
    wr32(c->io, base_reg[drive], 0x01000000u + CPC_W_DISK(drive));
    wr32(c->io, size_reg[drive], (uint32_t)size);
    d.file = f;
    d.mounted = true;
    d.writable = f->writable();
    d.size = (uint32_t)size;
    d.pending = 0;
    d.err_told = false;
    const char *slash = strrchr(path, '/');
    fs_short_name(d.name, sizeof(d.name), slash ? slash + 1 : path);
    snprintf(d.path, sizeof(d.path), "%s", path);
    if (s->wp_setting || !d.writable) {
        s->wp_bits |= (uint8_t)(1 << drive);
    } else {
        s->wp_bits &= (uint8_t)~(1 << drive);
    }
    // T92 #17 probe: is the image in the DDR2 as written (first and last 16 bytes read back from the slot)?
    uint8_t rb[16], tail[16], tailb[16];
    c->io->ddr_read(CPC_W_DISK(drive), rb, sizeof(rb));
    bool head_ok = memcmp(rb, head, sizeof(rb)) == 0, tail_ok = false;
    if (f->read(d.size - sizeof(tail), tail, sizeof(tail))) {
        c->io->ddr_read(CPC_W_DISK(drive) + d.size - sizeof(tail), tailb, sizeof(tailb));
        tail_ok = memcmp(tail, tailb, sizeof(tail)) == 0;
    }
    cpcd_event(c, s, 'm', "pre-mount %c base=%08x size=%u wp=%u ddr head=%s tail=%s", letter(drive), (unsigned)rd32(c->io, base_reg[drive]),
               (unsigned)rd32(c->io, size_reg[drive]), (s->wp_bits >> drive) & 1, head_ok ? "ok" : "DIFF", tail_ok ? "ok" : "DIFF");
    ctrl(c, s, CPC_DISK_MOUNT(drive));
    cpcd_event(c, s, 'o', "mount-ctrl %c written (%02x)", letter(drive), (unsigned)(CPC_DISK_MOUNT(drive) | (s->wp_bits << 4)));
    printf("[cpc] %c: %s, %u bytes%s\n", letter(drive), d.name, (unsigned)d.size, (s->wp_bits >> drive) & 1 ? " (write protected)" : "");
    mount_notify(c, drive);                        // T180 D2: the slots are A, B, Tape
    return 0;
}

int cpcd_eject(CoreCtx *c, CpcDisks *s, int drive, char *msg, int msglen)
{
    CpcDisk &d = s->d[drive];
    if (!d.mounted) {
        mount_notify(c, drive);                    // T180 D2: an eject of the empty drive is the person's word: a stored mount of it goes
        return 0;
    }
    cpcd_poll(c, s, false);                        // the last marks of the FDC
    if (!flush_drive(c, d, drive)) {
        snprintf(msg, msglen, "%c: could not be written back to its file (medium read only or gone) - not ejected", letter(drive));
        return 1;
    }
    cpcd_event(c, s, 'e', "eject %c", letter(drive));
    ctrl(c, s, CPC_DISK_EJECT(drive));
    wr32(c->io, size_reg[drive], 0);
    c->fs->close(d.file);
    memset(&d, 0, sizeof(d));
    mount_notify(c, drive);                        // T180 D2
    return 0;
}

void cpcd_poll(CoreCtx *c, CpcDisks *s, bool force)
{
    uint32_t now = c->io->now_ms();
    for (int drive = 0; drive < 2; drive++) {
        CpcDisk &d = s->d[drive];
        if (!d.mounted) {
            continue;
        }
        for (int m = 0; m < 8; m++) {
            uint8_t b = c->io->rd(dirty_reg[drive] + m);
            if (!b) {
                continue;
            }
            c->io->wr(dirty_reg[drive] + m, b);    // taken: the core sets a bit again if the chunk is written once more
            if (d.writable) {
                if (!d.pending) {
                    d.first_ms = now;
                }
                d.pending |= (uint64_t)b << (8 * m);
                d.last_ms = now;
            }
        }
        if (!d.pending && !d.unsynced) {
            continue;
        }
        bool due = force || now - d.last_ms >= CPCD_IDLE_MS || now - d.first_ms >= CPCD_MAX_DIRTY_MS;
        if (due && (force || d.err_told == false || now - d.try_ms >= 1000)) {
            d.try_ms = now;
            uint64_t pend = d.pending;
            bool ok = flush_drive(c, d, drive);
            cpcd_event(c, s, 'f', "flush %c pend=%08x%08x force=%d ok=%d", letter(drive), (unsigned)(pend >> 32), (unsigned)pend, force, ok);
        }
    }
}

bool cpcd_flush_all(CoreCtx *c, CpcDisks *s, char *msg, int msglen)
{
    cpcd_poll(c, s, true);
    bool all = true;
    for (int drive = 0; drive < 2; drive++) {
        const CpcDisk &d = s->d[drive];
        if (d.mounted && (d.pending || d.unsynced)) {
            if (all && msg && msglen) {
                snprintf(msg, msglen, "%c: could not be written back to its file (medium read only or gone). It stays mounted.", letter(drive));
            }
            all = false;
        }
    }
    return all;
}

bool cpcd_pending(CoreCtx *c, const CpcDisks *s)
{
    for (int drive = 0; drive < 2; drive++) {
        const CpcDisk &d = s->d[drive];
        if (!d.mounted || !d.writable) {
            continue;
        }
        if (d.pending || d.unsynced) {
            return true;
        }
        for (int m = 0; m < 8; m++) {
            if (c->io->rd(dirty_reg[drive] + m)) {
                return true;                       // the FDC wrote, the poll has not taken the marks yet
            }
        }
    }
    return false;
}

const char *cpcd_name(const CpcDisks *s, int drive)
{
    return s->d[drive].mounted ? s->d[drive].name : 0;
}

void cpcd_event(CoreCtx *c, CpcDisks *s, char kind, const char *fmt, ...)
{
    CpcProbe &p = s->probe;
    uint32_t now = c->io->now_ms();
    if (kind == 'm') { p.n_mount++; p.last_mount_ms = now; }
    if (kind == 'e') p.n_eject++;
    if (kind == 'r') { p.n_reset++; p.last_reset_ms = now; }
    if (kind == 'f') p.n_flush++;
    char text[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    Snap n;
    snap(c->io, n);
    print_snap(text, now, n);
    printf("\n");
}

void cpcd_probe(CoreCtx *c, CpcDisks *s)
{
    CpcProbe &p = s->probe;
    uint32_t now = c->io->now_ms();
    Snap n;
    snap(c->io, n);
    p.ticks++;
    if (n.ds & 1) p.busy_ticks++;
    if (n.ds & 2) p.oob_seen = true;
    // changes of the registers (not pc and frame counter), at most one line per 250 ms; a change that waits is printed when the time is up
    bool changed = !p.have || n.st != p.st || n.ds != p.ds || n.ctl != p.ctl || n.wp != p.wp || n.dy0 != p.dy0 || n.dy1 != p.dy1 ||
                   n.b0 != p.b0 || n.s0 != p.s0 || n.b1 != p.b1 || n.s1 != p.s1;
    if (changed && (!p.have || now - p.chg_ms >= 250)) {
        print_snap("chg", now, n);
        printf("\n");
        p.have = true;
        p.chg_ms = now;
        p.st = n.st; p.ds = n.ds; p.ctl = n.ctl; p.wp = n.wp; p.dy0 = n.dy0; p.dy1 = n.dy1;
        p.b0 = n.b0; p.s0 = n.s0; p.b1 = n.b1; p.s1 = n.s1;
    }
    // the Z80's address bus sampled every tick: a window of 2 s in which it stayed inside 64 bytes is "no progress"
    if (!p.win_ms) {
        p.win_ms = now;
        p.lo = p.hi = n.pc;
    }
    if (n.pc < p.lo) p.lo = n.pc;
    if (n.pc > p.hi) p.hi = n.pc;
    if (now - p.win_ms >= 2000) {
        if (p.hi - p.lo < 64) {
            p.hb_n++;
            if (p.hb_n <= 5 || p.hb_n % 5 == 0) {      // after five lines one in five: the ring is 4 KB
                print_snap("HB", now, n);
                printf(" span=%04x-%04x n=%u bz=%u/%u oob=%d ev=m%ue%ur%uf%u lm=%u lr=%u\n", p.lo, p.hi, (unsigned)p.hb_n, (unsigned)p.busy_ticks,
                       (unsigned)p.ticks, p.oob_seen, p.n_mount, p.n_eject, p.n_reset, p.n_flush, (unsigned)p.last_mount_ms, (unsigned)p.last_reset_ms);
            }
        } else {
            p.hb_n = 0;
        }
        p.win_ms = now;
        p.lo = p.hi = n.pc;
    }
}
