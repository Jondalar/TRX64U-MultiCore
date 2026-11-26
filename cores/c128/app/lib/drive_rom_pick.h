// Copy of coreapp/cores/drive_rom_pick.h at commit 2fd95fc63 (T239.5 D16: the original stays for the cores that have not moved; this copy is the C128's own).
// The choice between the stock and the JiffyDOS DOS ROM of one drive type, shared by the cores that offer JiffyDOS (the SuperCPU core: the 1541; the
// C128 core: the 1541 and the 1581, T185 D2). A core keeps one DriveRomPick per drive type. Both ROMs the person supplied are held here: which one the
// drive has depends on the JiffyDOS setting, and the setting can change at any time (a put of a ROM and a change of the setting both end in
// drive_pick_apply). The bytes go to the drive library's operation `put_op` (drive.put_rom, drive.put_rom1581).
#ifndef COREAPP_DRIVE_ROM_PICK_H
#define COREAPP_DRIVE_ROM_PICK_H

#include "ctx.h"
#include "ops_glue.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

#define DRIVE_ROM_MAX 32768u
enum { DR_NONE = 0, DR_STOCK = 1, DR_JIFFY = 2 };

struct DriveRomPick {
    uint8_t applied;                                    // which ROM the drive holds now (DR_*)
    uint32_t n_stock, n_jiffy;                          // 0: not supplied
    uint8_t stock[DRIVE_ROM_MAX];
    uint8_t jiffy_rom[DRIVE_ROM_MAX];
};

// What differs per drive type: the library operation that takes the bytes, the tag of the console line, the name of the type, and whether only the
// 32 KiB form is a ROM of this type (the 1581; the 1541 comes in 16 and 32 KiB).
struct DriveRomKind {
    const char *put_op;
    const char *tag;
    const char *what;
    bool only32k;
    bool quiet;                                         // the core tells what is missing itself: no "not supplied" line from the pick
};

static inline bool drive_pick_size_ok(const DriveRomKind &k, uint64_t n)
{
    return n == DRIVE_ROM_MAX || (n == 16384u && !k.only32k);
}

static inline void drive_pick_sha_hex(const uint8_t *d, uint32_t n, char *out)
{
    sha256_t h;
    uint8_t dig[32];
    sha256_init(&h);
    sha256_update(&h, d, n);
    sha256_final(&h, dig);
    sha256_hex(dig, out);
}

// The drive holds the JiffyDOS ROM when `jiffy` (the setting) is On and the file was supplied, else the stock ROM when that was supplied. Called after
// each of the two ROMs came in and after each change of the setting; the drive gets bytes only when the choice changed (or a ROM was put again).
// Returns a line for the person in msg when the setting wants a ROM that is not there.
static inline void drive_pick_apply(CoreCtx *c, DriveRomPick &s, const DriveRomKind &k, bool jiffy, char *msg, int msglen)
{
    int want = (jiffy && s.n_jiffy) ? DR_JIFFY : (s.n_stock ? DR_STOCK : DR_NONE);
    if (jiffy && !s.n_jiffy && !k.quiet && msg && msglen) {
        snprintf(msg, msglen, "JiffyDOS %s ROM not supplied", k.what);
    }
    if (want == DR_NONE || want == s.applied) {
        return;
    }
    const uint8_t *p = want == DR_JIFFY ? s.jiffy_rom : s.stock;
    uint32_t n = want == DR_JIFFY ? s.n_jiffy : s.n_stock;
    OpIn in = in_blob("data", p, n);
    OpResult r;
    if (ops_call(c, k.put_op, &in, 1, &r)) {
        if (msg && msglen) {
            snprintf(msg, msglen, "%s", r.msg[0] ? r.msg : "the drive did not take the ROM");
        }
        s.applied = DR_NONE;
        return;
    }
    s.applied = (uint8_t)want;
    char h[65];
    drive_pick_sha_hex(p, n, h);
    printf("[%s] drive A ROM: %s %s, %u bytes, sha256 %s\n", k.tag, want == DR_JIFFY ? "JiffyDOS" : "stock", k.what, (unsigned)n, h);
}

// A ROM the person supplied: kept, then the drive gets the one the setting chooses. `is_jiffy` says which of the two this is.
static inline int drive_pick_keep(CoreCtx *c, DriveRomPick &s, const DriveRomKind &k, const uint8_t *data, uint32_t n, bool is_jiffy, bool jiffy, OpOut &o)
{
    if (!drive_pick_size_ok(k, n)) {
        snprintf(o.msg, o.msglen, "%s%s %s: %u bytes, %s expected", is_jiffy ? "JiffyDOS " : "", k.what, is_jiffy ? "ROM" : "drive ROM", (unsigned)n,
                 k.only32k ? "32768" : "16384 or 32768");
        return OPE_FAILED;
    }
    memcpy(is_jiffy ? s.jiffy_rom : s.stock, data, n);
    (is_jiffy ? s.n_jiffy : s.n_stock) = n;
    s.applied = DR_NONE;                                // a ROM put again is written again
    char m[OP_MSG_MAX];
    m[0] = 0;
    drive_pick_apply(c, s, k, jiffy, m, sizeof(m));
    if (s.applied == DR_NONE && ((jiffy && s.n_jiffy) || s.n_stock)) {
        snprintf(o.msg, o.msglen, "%s", m);              // the drive refused the bytes
        return OPE_FAILED;
    }
    return OPE_OK;
}

// The same for a file picked in the browser (the library's own set_rom would bypass the choice). The file is read whole, its size checked first.
static inline int drive_pick_keep_file(CoreCtx *c, DriveRomPick &s, const DriveRomKind &k, const char *path, bool jiffy, OpOut &o)
{
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(o.msg, o.msglen, "cannot open %s", path);
        return OPE_FAILED;
    }
    static uint8_t buf[DRIVE_ROM_MAX];
    uint64_t n = f->size();
    bool ok = drive_pick_size_ok(k, n) && f->read(0, buf, (uint32_t)n);
    c->fs->close(f);
    if (!ok) {
        snprintf(o.msg, o.msglen, "A %s ROM is %s bytes (this file: %u, or it cannot be read).", k.what, k.only32k ? "32768" : "16384 or 32768",
                 (unsigned)fs_size32(n));
        return OPE_FAILED;
    }
    return drive_pick_keep(c, s, k, buf, (uint32_t)n, false, jiffy, o);
}

#endif
