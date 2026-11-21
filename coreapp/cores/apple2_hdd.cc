// The ProDOS hard disk of slot 7 (T89.1 D4, D7): the application's half of cores/apple2/rtl/trx/a2_hdd.vhd. The card holds the CPU while a block
// command is open (the machine goes on running); the block is moved between the image file and main RAM here, through the base RAM window.
#include "apple2_disk.h"
#include "apple2_regs.h"
#include "engines.h"
#include "../../cores/apple2/rtl/roms/hdd_rom.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define BLOCK 512u
#define MAX_BLOCKS 65535u

static void tell(CoreCtx *c, const char *text)
{
    snprintf(c->notice, sizeof(c->notice), "%s", text);
    c->notice_pending = true;
    printf("[apple2] %s\n", text);
}

const char *a2_unit_name(CoreCtx *c, int unit)
{
    A2State *st = a2_state(c);
    return unit >= 0 && unit < A2_UNITS && st->unit[unit].file ? st->unit[unit].name : 0;
}

// HDD_CTL, the sizes and the card's ROM, from the state. The ROM is in the core only while a unit is mounted: a zero array has no card
// signature and the Autostart scan skips slot 7 (the same way the Disk II's ROM is handled); a ROM with no unit would boot into slot 6 anyway.
void a2_hdd_write_ctl(CoreCtx *c)
{
    A2State *st = a2_state(c);
    CoreIo *io = c->io;
    uint8_t ctl = 0;
    bool any = false;
    for (int i = 0; i < A2_UNITS; i++) {
        A2Unit *u = &st->unit[i];
        if (u->file) {
            any = true;
            ctl |= (uint8_t)(1u << i);
            if (!u->writable) {
                ctl |= (uint8_t)(4u << i);
            }
            io->wr(A2_HDD_SIZE(i), (uint8_t)(u->blocks & 0xFF));
            io->wr(A2_HDD_SIZE(i) + 1, (uint8_t)(u->blocks >> 8));
        }
    }
    uint8_t rom[A2_HDDROM_BYTES];
    memset(rom, 0, sizeof(rom));
    if (any) {
        memcpy(rom, a2_hdd_rom, sizeof(rom));
    }
    io->wr(A2_WIN_SEL, A2_SEL_HDDROM);
    io->rom_write(0, rom, sizeof(rom));
    io->wr(A2_WIN_SEL, A2_SEL_SYSROM);
    io->wr(A2_HDD_CTL, ctl);
}

static void drop(CoreCtx *c, int unit)
{
    A2State *st = a2_state(c);
    A2Unit *u = &st->unit[unit];
    if (u->file) {
        if (u->needs_sync) {
            u->file->sync();
        }
        (st->fs ? st->fs : c->fs)->close(u->file);
    }
    memset(u, 0, sizeof(*u));
}

// T180 D4: the unit's file is synced before it is let go; false and the reason when the medium refuses (the unit stays mounted).
static bool sync_unit(CoreCtx *c, int unit, char *msg, int msglen)
{
    A2Unit *u = &a2_state(c)->unit[unit];
    if (u->file && u->needs_sync) {
        MediaSaving saving(c, true);
        if (!u->file->sync()) {
            if (msg) {
                snprintf(msg, msglen, "S7.%d: writing the disk back to its file failed. It stays mounted.", unit + 1);
            }
            return false;
        }
        u->needs_sync = false;
    }
    return true;
}

int a2_hdd_eject(CoreCtx *c, int unit, char *msg, int msglen)
{
    if (unit < 0 || unit >= A2_UNITS) {
        return 1;
    }
    if (!sync_unit(c, unit, msg, msglen)) {
        return 1;
    }
    drop(c, unit);
    a2_hdd_write_ctl(c);
    mount_notify(c, A2_DRIVES + unit);                       // T180 D2: the slots of the Apple II are D1, D2, S7.1, S7.2
    return 0;
}

int a2_hdd_mount(CoreCtx *c, const char *path, int unit, char *msg, int msglen)
{
    if (unit < 0 || unit >= A2_UNITS) {
        snprintf(msg, msglen, "no such unit");
        return 1;
    }
    if (!(c->io->rd(A2_CAPS2) & A2_CAPS2_BITS)) {
        snprintf(msg, msglen, "this Apple IIe core has no slot 7 card: load the matching .cor");
        return 1;
    }
    CoreFile *f = c->fs->open(path, true);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint64_t size = f->size();
    uint32_t off = 0;
    uint64_t bytes = size;
    uint8_t head[64];
    if (size >= 64 && f->read(0, head, 64) && !memcmp(head, "2IMG", 4)) {
        uint32_t len;
        uint8_t kind;
        if (!a2_2mg_header(head, size, &off, &len, &kind)) {
            c->fs->close(f);
            snprintf(msg, msglen, "%s: not a valid 2MG image", path);
            return 1;
        }
        if (kind != 1) {
            c->fs->close(f);
            if (len == 409600 || len == 819200) {
                snprintf(msg, msglen, "%s: a 3.5\" image is not a ProDOS block image (2MG format %u); only block images mount in slot 7", path, (unsigned)kind);
            } else {
                snprintf(msg, msglen, "%s: a hard disk volume must be in ProDOS order (2MG format %u)", path, (unsigned)kind);
            }
            return 1;
        }
        bytes = len;
    }
    uint64_t blocks = bytes / BLOCK;
    if (blocks == 0 || (bytes % BLOCK) != 0) {
        c->fs->close(f);
        snprintf(msg, msglen, "%s is %s%u bytes: a ProDOS volume is a whole number of 512 byte blocks", path, bytes > 0xFFFFFFFFull ? "more than " : "", (unsigned)(bytes > 0xFFFFFFFFull ? 0xFFFFFFFFull : bytes));   // T251 D1
        return 1;
    }
    if (blocks > MAX_BLOCKS) {
        blocks = MAX_BLOCKS;
        tell(c, "The volume is larger than 32 MB; the card addresses 65,535 blocks, the rest is not reachable.");
    }
    if (!sync_unit(c, unit, msg, msglen)) {                  // T180 D1, D4: the old volume first; a failure keeps it and refuses the new one
        c->fs->close(f);
        return 1;
    }
    drop(c, unit);
    A2State *st = a2_state(c);
    st->fs = c->fs;
    A2Unit *u = &st->unit[unit];
    u->file = f;
    u->off = off;
    u->blocks = (uint32_t)blocks;
    u->writable = f->writable();
    const char *slash = strrchr(path, '/');
    snprintf(u->name, sizeof(u->name), "%s", slash ? slash + 1 : path);
    snprintf(u->path, sizeof(u->path), "%s", path);
    a2_hdd_write_ctl(c);
    mount_notify(c, A2_DRIVES + unit);                       // T180 D2
    printf("[apple2] S7.%d: %s, %u blocks, %s\n", unit + 1, u->name, (unsigned)u->blocks, u->writable ? "read/write" : "read only");
    return 0;
}

bool a2_hdd_sync(CoreCtx *c, char *msg, int msglen)
{
    bool all = true;
    for (int i = 0; i < A2_UNITS; i++) {
        if (!sync_unit(c, i, all ? msg : 0, msglen)) {
            all = false;
        }
    }
    return all;
}

// The block moves through the CPU's memory map byte by byte (the address wraps at 64 KB as the card's does). With language card bank 1
// selected the CPU's $D000-$DFFF is the physical RAM $C000-$CFFF; the window shows the physical RAM. RAMRD/RAMWRT/ALTZP (aux RAM) are not
// followed: the window's main RAM only (T89.1 section 5, open point).
static void hdd_ram(CoreIo *io, uint32_t mem, uint8_t *buf, bool bank1, bool to_ram)
{
    uint32_t i = 0;
    while (i < BLOCK) {
        uint32_t a = (mem + i) & 0xFFFFu;
        uint32_t n = BLOCK - i;
        uint32_t lim = 0x10000u - a;                       // up to the wrap
        if (n > lim) {
            n = lim;
        }
        bool d = a >= 0xD000u && a <= 0xDFFFu;
        uint32_t seg_end = d ? 0xE000u : (a < 0xD000u ? 0xD000u : 0x10000u);
        if (a + n > seg_end) {
            n = seg_end - a;                               // stay inside one side of the $D000-$DFFF edge
        }
        uint32_t pa = (bank1 && d) ? a - 0x1000u : a;
        if (to_ram) {
            io->ram_write(pa, buf + i, n);
        } else {
            io->ram_read(pa, buf + i, n);
        }
        i += n;
    }
}

// ProDOS error codes the card's firmware hands back unchanged: $27 I/O error, $28 no device, $2B write protected.
bool a2_hdd_service(CoreCtx *c)
{
    CoreIo *io = c->io;
    A2State *st = a2_state(c);
    uint8_t s = io->rd(A2_HDD_STATUS);        // also with no unit mounted: a request that was open while the unit was ejected must be answered, or the CPU waits for ever
    if (!(s & 1)) {
        return false;
    }
    uint32_t blk = io->rd(A2_HDD_BLOCK) | (uint32_t)io->rd(A2_HDD_BLOCK + 1) << 8;
    uint32_t mem = io->rd(A2_HDD_MEM) | (uint32_t)io->rd(A2_HDD_MEM + 1) << 8;
    uint8_t s2 = io->rd(A2_HDD_STATUS);
    if (!(s2 & 1) || s2 != s) {
        return true;                               // it changed under us
    }
    int unit = (s >> 2) & 1;
    bool write = (s & 2) != 0;
    bool bank1 = (s & 8) != 0;
    A2Unit *u = &st->unit[unit];
    uint8_t result = 0;
    static uint8_t buf[BLOCK];
    if (!u->file || blk >= u->blocks) {
        result = u->file ? 0x27 : 0x28;
    } else if (write) {
        if (!u->writable) {
            result = 0x2B;
        } else {
            // the 512 bytes at `mem` as the CPU sees them (RAM $C000-$CFFF is language card bank 1 at $D000-$DFFF)
            hdd_ram(io, mem, buf, bank1, false);
            if (!u->file->write(u->off + (uint64_t)blk * BLOCK, buf, BLOCK)) {
                result = 0x27;
            } else {
                u->needs_sync = true;
            }
        }
    } else {
        if (!u->file->read(u->off + (uint64_t)blk * BLOCK, buf, BLOCK)) {
            result = 0x27;
        } else {
            hdd_ram(io, mem, buf, bank1, true);
        }
    }
    io->wr(A2_HDD_RESULT, result);
    io->wr(A2_HDD_ACK, 1);
    // the acknowledge reaches the card through synchronisers: wait until the request is seen closed, or the next poll would serve it again
    for (int i = 0; i < 200 && (io->rd(A2_HDD_STATUS) & 1); i++) {
        io->delay_us(20);
    }
    return true;
}
