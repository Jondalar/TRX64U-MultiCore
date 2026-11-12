#include "newdisk.h"
#include "newfile.h"
#include "ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// ---- Apple II: DOS 3.3 data disk ---------------------------------------------------------------------------------------------------------
// The .dsk file is the sectors in DOS 3.3 logical order, track after track; the drive nibblizes it with the same order (apple2_disk.cc).
uint32_t nd_apple_dos33(uint8_t *out, uint32_t cap)
{
    if (cap < ND_APPLE_BYTES) {
        return 0;
    }
    memset(out, 0, ND_APPLE_BYTES);
    const uint32_t trk = 17 * 16 * 256;
    uint8_t *vtoc = out + trk;
    vtoc[0x01] = 17;                 // first catalog sector: track 17, sector 15
    vtoc[0x02] = 15;
    vtoc[0x03] = 3;                  // DOS release
    vtoc[0x06] = 254;                // volume number
    vtoc[0x27] = 122;                // track/sector pairs in a T/S list sector
    vtoc[0x30] = 17;                 // last track allocated, and the direction DOS goes on in
    vtoc[0x31] = 1;
    vtoc[0x34] = 35;                 // tracks
    vtoc[0x35] = 16;                 // sectors per track
    vtoc[0x36] = 0x00;               // bytes per sector, 256, little endian
    vtoc[0x37] = 0x01;
    for (int t = 0; t < 35; t++) {   // four bytes a track, a set bit is a free sector: byte 0 sectors 15..8, byte 1 sectors 7..0
        bool used = t <= 2 || t == 17;
        uint8_t *e = vtoc + 0x38 + 4 * t;
        e[0] = e[1] = used ? 0x00 : 0xFF;
    }
    for (int s = 15; s >= 1; s--) {  // the catalog: sector 15 down to 1, each one pointing at the next, the last at nothing
        uint8_t *cat = out + trk + (uint32_t)s * 256;
        cat[1] = s > 1 ? 17 : 0;
        cat[2] = s > 1 ? (uint8_t)(s - 1) : 0;
    }
    return ND_APPLE_BYTES;
}

// ---- Amiga: empty OFS volume --------------------------------------------------------------------------------------------------------------
// Block 0 and 1 the boot block, the root block in the middle of the disk (880), the bitmap behind it (881). All numbers big endian.
#define ADF_ROOT   880
#define ADF_BITMAP 881
#define ADF_BLOCKS 1760

// a block's checksum field makes the sum of its 128 longs zero
static void block_sum(uint8_t *b, int field)
{
    put_be32(b + 4 * field, 0);
    uint32_t sum = 0;
    for (int i = 0; i < 128; i++) {
        sum += get_be32(b + 4 * i);
    }
    put_be32(b + 4 * field, (uint32_t)(0u - sum));
}

uint32_t nd_adf_ofs(uint8_t *out, uint32_t cap, const char *name)
{
    if (cap < ND_ADF_BYTES) {
        return 0;
    }
    memset(out, 0, ND_ADF_BYTES);
    // The boot block as Format writes it without Install: "DOS", flags 0 (OFS), checksum 0, the root block, nothing else. The checksum is not
    // valid, so the Kickstart never executes anything from the disk: a data disk, not bootable.
    out[0] = 'D';
    out[1] = 'O';
    out[2] = 'S';
    out[3] = 0;
    put_be32(out + 8, ADF_ROOT);

    uint8_t *root = out + (uint32_t)ADF_ROOT * 512;
    put_be32(root + 0, 2);                 // T_HEADER
    put_be32(root + 12, 72);               // hash table size
    put_be32(root + 4 * 78, 0xFFFFFFFFu);  // the bitmap is valid
    put_be32(root + 4 * 79, ADF_BITMAP);   // the first (and only) bitmap block
    char vol[32];
    size_t n = 0;
    for (const char *p = name ? name : ""; *p && n < 30; p++) {
        if (*p != ':' && *p != '/' && (unsigned char)*p >= 0x20) {
            vol[n++] = *p;
        }
    }
    if (!n) {
        memcpy(vol, "Empty", 5);
        n = 5;
    }
    root[4 * 108] = (uint8_t)n;
    memcpy(root + 4 * 108 + 1, vol, n);
    put_be32(root + 4 * 127, 1);           // ST_ROOT
    block_sum(root, 5);

    uint8_t *bm = out + (uint32_t)ADF_BITMAP * 512;
    for (uint32_t b = 2; b < ADF_BLOCKS; b++) {      // a set bit is a free block; block 2 is bit 0 of the first long
        if (b == ADF_ROOT || b == ADF_BITMAP) {
            continue;
        }
        uint32_t bit = b - 2;
        bm[4 + 4 * (bit / 32) + 3 - (bit % 32) / 8] |= (uint8_t)(1u << (bit % 8));
    }
    block_sum(bm, 0);
    return ND_ADF_BYTES;
}

// ---- Atari 800 XL: DOS 2.0S data disk -----------------------------------------------------------------------------------------------------
// Single density, 720 sectors of 128 bytes behind a 16 byte ATR header. The boot sectors 1-3 stay empty (no DOS.SYS: not bootable), the VTOC is
// sector 360, the directory 361-368. DOS 2.0S counts 707 free: sector 0 does not exist and sector 720 is outside its 90 byte bitmap.
uint32_t nd_atr_dos2(uint8_t *out, uint32_t cap)
{
    if (cap < ND_ATR_BYTES) {
        return 0;
    }
    memset(out, 0, ND_ATR_BYTES);
    out[0] = 0x96;
    out[1] = 0x02;
    out[2] = 5760 & 0xFF;                 // the size in paragraphs of 16 bytes: 92160 / 16
    out[3] = 5760 >> 8;
    out[4] = 128;                         // sector size
    out[5] = 0;
    uint8_t *vtoc = out + 16 + 359 * 128;
    vtoc[0] = 2;                          // DOS 2.0S
    vtoc[1] = 707 & 0xFF;                 // sectors on the disk
    vtoc[2] = 707 >> 8;
    vtoc[3] = 707 & 0xFF;                 // free
    vtoc[4] = 707 >> 8;
    memset(vtoc + 10, 0xFF, 90);          // sector n is bit 7 - n % 8 of byte 10 + n / 8, a set bit is a free sector
    static const int used[] = { 0, 1, 2, 3, 360, 361, 362, 363, 364, 365, 366, 367, 368 };
    for (unsigned i = 0; i < sizeof(used) / sizeof(used[0]); i++) {
        vtoc[10 + used[i] / 8] &= (uint8_t)~(0x80u >> (used[i] % 8));
    }
    return ND_ATR_BYTES;
}

// ---- Amstrad CPC: standard DSK, DATA format ----------------------------------------------------------------------------------------------
uint32_t nd_cpc_data(uint8_t *out, uint32_t cap)
{
    if (cap < ND_CPC_BYTES) {
        return 0;
    }
    memset(out, 0, 256);
    memcpy(out, "MV - CPCEMU Disk-File\r\nDisk-Info\r\n", 34);
    memcpy(out + 0x22, "TRX64U        ", 14);
    out[0x30] = 40;                       // tracks
    out[0x31] = 1;                        // sides
    out[0x32] = 0x00;                     // bytes in a track block: 0x1300
    out[0x33] = 0x13;
    for (int t = 0; t < 40; t++) {
        uint8_t *tb = out + 256 + (uint32_t)t * 0x1300;
        memset(tb, 0, 0x100);
        memcpy(tb, "Track-Info\r\n", 12);
        tb[0x10] = (uint8_t)t;
        tb[0x11] = 0;
        tb[0x14] = 2;                     // 512 bytes a sector
        tb[0x15] = 9;
        tb[0x16] = 0x4E;                  // GAP3
        tb[0x17] = 0xE5;                  // filler
        for (int s = 0; s < 9; s++) {
            uint8_t *si = tb + 0x18 + 8 * s;
            si[0] = (uint8_t)t;
            si[1] = 0;
            si[2] = (uint8_t)(0xC1 + s);
            si[3] = 2;
            si[6] = 0x00;                 // the sector's size, 512
            si[7] = 0x02;
        }
        memset(tb + 0x100, 0xE5, 9 * 512);
    }
    return ND_CPC_BYTES;
}

// ---- the formats by number -----------------------------------------------------------------------------------------------------------------
const char *nd_ext(int fmt)
{
    static const char *const ext[ND_COUNT] = { ".dsk", ".adf", ".atr", ".dsk" };
    return fmt >= 0 && fmt < ND_COUNT ? ext[fmt] : "";
}

uint32_t nd_bytes(int fmt)
{
    static const uint32_t n[ND_COUNT] = { ND_APPLE_BYTES, ND_ADF_BYTES, ND_ATR_BYTES, ND_CPC_BYTES };
    return fmt >= 0 && fmt < ND_COUNT ? n[fmt] : 0;
}

uint32_t nd_build(int fmt, uint8_t *out, uint32_t cap, const char *name)
{
    switch (fmt) {
    case ND_APPLE_DSK: return nd_apple_dos33(out, cap);
    case ND_ADF:       return nd_adf_ofs(out, cap, name);
    case ND_ATR:       return nd_atr_dos2(out, cap);
    case ND_CPC_DSK:   return nd_cpc_data(out, cap);
    }
    return 0;
}

// ---- the operation -----------------------------------------------------------------------------------------------------------------------
static const char *const create_labels[ND_COUNT] = { "apple2_dsk", "adf", "atr", "cpc_dsk" };
static const ArgSpec create_args[] = { ARG_STRING("path", 255), ARG_ENUM("format", create_labels, ND_COUNT), ARG_STRING("name", 24) };

// The job of a create that is stepped (CoreCtx::job_step): the image stays in this buffer and the file open in this writer between two calls of
// the operation. One at a time; a new create, a cancel or a call with other arguments drops the old one and removes its partial file.
static NfWriter *g_job;
static uint8_t *g_job_buf;
static char g_job_key[300];

static void job_drop()
{
    if (g_job) {
        g_job->abort();
    }
    free(g_job_buf);
    g_job_buf = 0;
    g_job_key[0] = 0;
}

static int op_create(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int fmt = a.i(1);
    char key[sizeof(g_job_key)];
    snprintf(key, sizeof(key), "%d|%s|%s", fmt, a.s(0), a.s(2));
    bool going = g_job && g_job_buf && !strcmp(key, g_job_key);
    if (going && c->job_cancel) {
        job_drop();
        c->job_cancel = false;
        snprintf(o.msg, o.msglen, "Cancelled: nothing was written.");
        return OPE_OK;
    }
    int rc;
    if (going && c->job_step) {
        rc = g_job->step(o.msg, o.msglen);
    } else {
        job_drop();
        c->job_cancel = false;
        uint32_t cap = nd_bytes(fmt);
        if (!c->fs || !cap) {
            snprintf(o.msg, o.msglen, "Cannot create an image: no medium.");
            return OPE_FAILED;
        }
        g_job_buf = (uint8_t *)malloc(cap);            // malloc, not new (nothrow): the board image has no libstdc++
        if (!g_job_buf) {
            snprintf(o.msg, o.msglen, "Not enough memory to build the image.");
            return OPE_FAILED;
        }
        // the volume name: the one given, or the file's name up to its first dot
        char vol[40];
        if (a.s(2)[0]) {
            snprintf(vol, sizeof(vol), "%s", a.s(2));
        } else {
            const char *b = nf_base(a.s(0)[0] ? a.s(0) : "blank");
            if (!b[0]) {
                b = "blank";                                // a folder with no name behind it: the free name will be blank[N]
            }
            nf_copy(vol, sizeof(vol), b, strcspn(b, "."));
        }
        uint32_t n = nd_build(fmt, g_job_buf, cap, vol);
        if (!n) {
            job_drop();
            snprintf(o.msg, o.msglen, "Cannot build the image.");
            return OPE_FAILED;
        }
        if (!g_job) {
            g_job = new NfWriter();
        }
        rc = g_job->begin(c->fs, c->media_dir, a.s(0), "blank", nd_ext(fmt), g_job_buf, n, o.msg, o.msglen);
        snprintf(g_job_key, sizeof(g_job_key), "%s", key);
    }
    if (!c->job_step) {
        while (rc == NF_MORE) {
            rc = g_job->step(o.msg, o.msglen);
        }
    }
    if (rc == NF_MORE) {
        c->job_pct = g_job->percent();
        snprintf(c->job_path, sizeof(c->job_path), "%s", g_job->full());
        o.more = true;
        o.msg[0] = 0;
        return OPE_OK;
    }
    char full[260];
    snprintf(full, sizeof(full), "%s", g_job->full());
    job_drop();
    if (rc == NF_FAIL) {
        return OPE_FAILED;
    }
    c->job_pct = 100;
    snprintf(c->job_path, sizeof(c->job_path), "%s", full);
    snprintf(o.msg, o.msglen, "Created %s.", full);
    snprintf(c->notice, sizeof(c->notice), "%s", o.msg);
    c->notice_pending = true;
    return OPE_OK;
}

// The free name the menu's name prompt starts with
template <int FMT> static const char *state_suggest(CoreCtx *c)
{
    static char buf[48];
    if (!c->fs) {
        return 0;
    }
    nf_suggest(c->fs, c->media_dir, "blank", nd_ext(FMT), buf, sizeof(buf));
    return buf;
}

void newdisk_register(OpRegistry &r)
{
    r.contributor("newdisk");
    r.add_op("disk.create", create_args, 3, op_create);
    r.add_state("disk.suggest.apple2_dsk", state_suggest<ND_APPLE_DSK>);
    r.add_state("disk.suggest.adf", state_suggest<ND_ADF>);
    r.add_state("disk.suggest.atr", state_suggest<ND_ATR>);
    r.add_state("disk.suggest.cpc_dsk", state_suggest<ND_CPC_DSK>);
}
