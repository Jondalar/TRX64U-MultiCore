// The firmware flash layer (T137 D4, shared with the MultiCore update of T152 D2): the chip check, the staged image that only a checked package can
// become, the write in the T49 order (application header last), the read-back. Pure code over the Ultimate application's `Flash` class, so
// tests/hostapp/test_flashfw.cc runs it against a fake flash with erase semantics. Who may be written is the caller's business (fwrestore.cc decides
// for a restore); what this layer guarantees is that nothing is erased before fw_image_seal() was given a package whose checks passed.
#include "flashfw.h"
#include "flash.h"
#include <string.h>
#include <stdio.h>

static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

uint32_t fw_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    static uint32_t tab[256];
    static bool have;
    if (!have) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            tab[i] = c;
        }
        have = true;
    }
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--) {
        crc = tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

uint32_t fw_crc32(const void *data, uint32_t len) { return fw_crc32_update(0, data, len); }

// Gideon's calc_checksum (update_common.h, tools/trx_pkg.py chk): 32 bit, shift-add with a fold.
uint32_t fw_chk(uint32_t c, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len--) {
        if (c & 0x80000000u) {
            c ^= 0x801618D5u;
        }
        c = (c << 1) + *p++;
    }
    return c;
}

// ---- the AXSS walk of the boot ROM (tools/trx_pkg.py axss, bootrom.c flash_axss) ----------------------------------------------------
#define SYNC_WORD 0xAA995566u
typedef bool (*AxGet)(void *ctx, uint32_t off, uint8_t *b);

static bool ax_word(AxGet get, void *ctx, uint32_t off, uint32_t *w)
{
    uint8_t b[4];
    for (int i = 0; i < 4; i++) {
        if (!get(ctx, off + i, b + i)) {
            return false;
        }
    }
    *w = be32(b);
    return true;
}

// The walk, over a getter of single bytes: memory for the staged bitstream, a page window for the flash.
static bool axss_walk(AxGet get, void *ctx, uint32_t len, uint32_t *value)
{
    uint32_t pos = 0, sh = 0;
    uint8_t c;
    while (sh != SYNC_WORD) {
        if (pos >= 4096 || pos >= len || !get(ctx, pos, &c)) {
            return false;
        }
        sh = (sh << 8) | c;
        pos++;
    }
    for (int n = 0; n < 65536; n++) {
        uint32_t w, count = 0;
        if (pos >= FW_BIT_SLOT_FULL || pos + 4 > len || !ax_word(get, ctx, pos, &w)) {
            break;
        }
        pos += 4;
        if ((w >> 29) == 1) {
            uint32_t op = (w >> 27) & 3, reg = (w >> 13) & 0x3FFF;
            count = w & 0x7FF;
            if (op == 2 && count == 1) {
                uint32_t v;
                if (pos + 4 > len || !ax_word(get, ctx, pos, &v)) {
                    break;
                }
                pos += 4;
                if (reg == 0x0D) {
                    *value = v;
                    return true;
                }
                if (reg == 0x04 && v == 0x0D) {
                    break;                                              // DESYNC
                }
                continue;
            }
        } else if ((w >> 29) == 2) {
            count = w & 0x07FFFFFFu;
        } else {
            break;
        }
        if (count >= (FW_BIT_SLOT_FULL >> 2)) {
            break;
        }
        pos += 4 * count;
    }
    return false;
}

static bool get_mem(void *ctx, uint32_t off, uint8_t *b) { *b = ((const uint8_t *)ctx)[off]; return true; }

bool fw_axss(const uint8_t *bit, uint32_t len, uint32_t *value)
{
    return axss_walk(get_mem, (void *)bit, len, value);
}


// ---- the flash ----------------------------------------------------------------------------------------------------------------------
bool fw_chip_ok(const char *type)
{
    if (!type) {
        return false;
    }
    if (!strcmp(type, "Winbond W25Q128")) {
        return true;
    }
#ifdef HOSTAPP_EMU_FLASH
    if (!strcmp(type, "Infineon S25FL128L")) {                  // ue2emu's flash (the updater's TRX_EMU_TEST rule): never in a build for a board
        return true;
    }
#endif
    return false;
}

bool fw_flash_check(Flash *fl, const char **type, const char **why)
{
    *type = "none";
    if (!fl) {
        *why = "no flash chip answered";
        return false;
    }
    *type = fl->get_type_string();
    if (!fw_chip_ok(*type)) {
        *why = "not the flash chip this firmware is written for (W25Q128)";
        return false;
    }
    if (fl->get_page_size() != (int)FW_PAGE || fl->get_sector_size(0) != (int)FW_SECTOR || !fl->need_erase()) {
        *why = "unexpected flash geometry";
        return false;
    }
    return true;
}

static uint8_t pg_buf[FW_PAGE] __attribute__((aligned(4)));
static uint8_t rd_buf[FW_PAGE] __attribute__((aligned(4)));

// bytes [0, len) of a region at `addr` against `want`; the tail of the last page beyond len is not looked at unless tail_ff (the slot behind the image)
static bool region_equals(Flash *fl, uint32_t addr, const uint8_t *want, uint32_t len, uint32_t *bad)
{
    for (uint32_t off = 0; off < len; off += FW_PAGE) {
        if (!fl->read_page((int)((addr + off) / FW_PAGE), rd_buf)) {
            *bad = addr + off;
            return false;
        }
        uint32_t n = len - off < FW_PAGE ? len - off : FW_PAGE;
        if (memcmp(rd_buf, want + off, n)) {
            *bad = addr + off;
            return false;
        }
    }
    return true;
}

bool fw_flash_equals(Flash *fl, const FwImage &img)
{
    return fw_flash_equals_bit(fl, img) && fw_flash_equals_app(fl, img);
}

bool fw_flash_equals_bit(Flash *fl, const FwImage &img)
{
    uint32_t bad;
    return img.bit && region_equals(fl, FW_BIT_ADDR, img.bit, img.bit_len, &bad);
}

bool fw_flash_equals_app(Flash *fl, const FwImage &img)
{
    uint32_t bad;
    return img.app && region_equals(fl, FW_APP_ADDR, img.app, img.app_len, &bad);
}

struct Wr {
    Flash *fl;
    FwWriteResult *r;
    FwProgress cb;
    void *ctx;
};

// FNV-1a over a sector as the flash holds it: tells an erase that took nothing from one that changed the sector
static bool sector_sum(Flash *fl, uint32_t addr, uint32_t *sum)
{
    uint32_t h = 2166136261u;
    for (uint32_t off = 0; off < FW_SECTOR; off += FW_PAGE) {
        if (!fl->read_page((int)((addr + off) / FW_PAGE), rd_buf)) {
            return false;
        }
        for (uint32_t i = 0; i < FW_PAGE; i++) {
            h = (h ^ rd_buf[i]) * 16777619u;
        }
    }
    *sum = h;
    return true;
}

static bool erase_sector_at(Wr &w, uint32_t addr, int phase)
{
    // The very first erase of a write: a locked flash (Protect Flash, status register lock-down; it holds until the power is removed) refuses it and
    // keeps its data. Then nothing is changed: started stays false, the page says "nothing was written" and not "do not switch off".
    const bool first = !w.r->started;
    uint32_t before = 0;
    const bool have_before = first && sector_sum(w.fl, addr, &before);
    w.r->started = true;
    int sector = w.fl->page_to_sector((int)(addr / FW_PAGE));
    if (!w.fl->erase_sector(sector)) {
        w.r->phase = phase; w.r->sector = addr; w.r->why = "the flash did not finish an erase";
        return false;
    }
    w.r->erases++;
    // an erased sector reads 0xFF; anything else means the erase did not take (write protection)
    for (uint32_t off = 0; off < FW_SECTOR; off += FW_PAGE) {
        if (!w.fl->read_page((int)((addr + off) / FW_PAGE), rd_buf)) {
            w.r->phase = phase; w.r->sector = addr; w.r->why = "the flash could not be read after an erase";
            return false;
        }
        for (uint32_t i = 0; i < FW_PAGE; i++) {
            if (rd_buf[i] != 0xFF) {
                uint32_t after;
                if (have_before && sector_sum(w.fl, addr, &after) && after == before) {
                    w.r->started = false;
                    w.r->why = "the flash is locked (Protect Flash): switch the power off and on, then update again";
                } else {
                    w.r->why = "the erase did not take (is the flash write protected?)";
                }
                w.r->phase = phase; w.r->sector = addr;
                return false;
            }
        }
    }
    return true;
}

// one page of src (padded with 0xFF behind len), three tries, read back
static bool program_page(Wr &w, uint32_t addr, const uint8_t *src, uint32_t len, uint32_t index, int phase)
{
    uint32_t off = index * FW_PAGE, n = off >= len ? 0 : (len - off < FW_PAGE ? len - off : FW_PAGE);
    memset(pg_buf, 0xFF, FW_PAGE);
    if (n) {
        memcpy(pg_buf, src + off, n);
    }
    int page = (int)((addr + off) / FW_PAGE);
    for (int tries = 0; tries < 3; tries++) {
        w.r->programs++;
        if (!w.fl->write_page(page, pg_buf)) {
            continue;
        }
        if (!w.fl->read_page(page, rd_buf)) {
            continue;
        }
        if (!memcmp(pg_buf, rd_buf, FW_PAGE)) {
            return true;
        }
    }
    w.r->phase = phase; w.r->sector = addr + off; w.r->why = "the flash did not accept the data (3 tries)";
    return false;
}

// Writes src[0, len) to the flash at addr (sector aligned). header_last: the first sector is erased first and programmed last, its pages
// descending, so the first page - the application's record - is the very last thing written (T49 5.1: the ROM never boots a half application).
static bool write_range(Wr &w, uint32_t addr, const uint8_t *src, uint32_t len, bool header_last, int phase)
{
    const uint32_t sectors = (len + FW_SECTOR - 1) / FW_SECTOR, spp = FW_SECTOR / FW_PAGE, pages = (len + FW_PAGE - 1) / FW_PAGE;
    uint32_t done = 0;
    if (w.cb) {
        w.cb(w.ctx, phase, 0, sectors, "erasing");
    }
    if (header_last && !erase_sector_at(w, addr, phase)) {
        return false;
    }
    for (uint32_t s = header_last ? 1 : 0; s < sectors; s++) {
        if (w.cb) {
            w.cb(w.ctx, phase, s, sectors, "erasing");
        }
        if (!erase_sector_at(w, addr + s * FW_SECTOR, phase)) {
            return false;
        }
        if (w.cb) {
            w.cb(w.ctx, phase, s, sectors, "programming");
        }
        for (uint32_t i = s * spp; i < (s + 1) * spp && i < pages; i++) {
            if (!program_page(w, addr, src, len, i, phase)) {
                return false;
            }
        }
        done = s + 1;
    }
    if (header_last) {
        if (w.cb) {
            w.cb(w.ctx, phase, done, sectors, "programming the first sector");
        }
        for (uint32_t i = spp; i-- > 0;) {
            if (i < pages && !program_page(w, addr, src, len, i, phase)) {
                return false;
            }
        }
    }
    if (w.cb) {
        w.cb(w.ctx, phase, sectors, sectors, "done");
    }
    return true;
}

// What a shorter image leaves behind in its slot is erased again: the slot reads as image + 0xFF (a raw restore needs it, the walk of the boot ROM
// ends at DESYNC anyway). Only sectors that hold something are erased.
static bool clear_tail(Wr &w, uint32_t from, uint32_t slot_end, int phase)
{
    for (uint32_t a = (from + FW_SECTOR - 1) / FW_SECTOR * FW_SECTOR; a < slot_end; a += FW_SECTOR) {
        bool blank = true;
        for (uint32_t off = 0; off < FW_SECTOR && blank; off += FW_PAGE) {
            if (!w.fl->read_page((int)((a + off) / FW_PAGE), rd_buf)) {
                w.r->phase = phase; w.r->sector = a; w.r->why = "the flash could not be read";
                return false;
            }
            for (uint32_t i = 0; i < FW_PAGE; i++) {
                if (rd_buf[i] != 0xFF) {
                    blank = false;
                    break;
                }
            }
        }
        if (!blank && !erase_sector_at(w, a, phase)) {
            return false;
        }
    }
    return true;
}

// the stamp walked in the flash itself, through a one-page window (the boot ROM's walk reads the flash the same way)
struct FlashWin {
    Flash *fl;
    uint32_t page;
    bool have, io_err;
    uint8_t buf[FW_PAGE] __attribute__((aligned(4)));
};

static bool get_flash_byte(void *ctx, uint32_t off, uint8_t *b)
{
    FlashWin *w = (FlashWin *)ctx;
    uint32_t pg = (FW_BIT_ADDR + off) / FW_PAGE;
    if (!w->have || w->page != pg) {
        if (!w->fl->read_page((int)pg, w->buf)) {
            w->io_err = true;
            return false;
        }
        w->page = pg;
        w->have = true;
    }
    *b = w->buf[(FW_BIT_ADDR + off) % FW_PAGE];
    return true;
}

static bool flash_axss(Flash *fl, uint32_t *value, bool *found)
{
    static FlashWin w;
    w.fl = fl; w.have = false; w.io_err = false;
    *found = axss_walk(get_flash_byte, &w, FW_BIT_SLOT_FULL, value);
    return !w.io_err;
}

bool fw_write(Flash *fl, const FwImage &img, FwProgress cb, void *ctx, FwWriteResult *res)
{
    if (!img.bit || !img.app) {                                                   // this write is for both regions
        memset(res, 0, sizeof(*res));
        res->why = "the image was not checked";
        return false;
    }
    return fw_write_parts(fl, img, FW_PART_BIT | FW_PART_APP, cb, ctx, res);
}

bool fw_write_parts(Flash *fl, const FwImage &img, uint32_t parts, FwProgress cb, void *ctx, FwWriteResult *res)
{
    memset(res, 0, sizeof(*res));
    res->why = "";
    const bool do_bit = (parts & FW_PART_BIT) != 0, do_app = (parts & FW_PART_APP) != 0;
    if (img.sealed != FW_SEALED || (!do_bit && !do_app) || (parts & ~(FW_PART_BIT | FW_PART_APP)) || (do_bit && !img.bit) || (do_app && !img.app) ||
        img.bit_len > (img.bit_slot ? img.bit_slot : FW_BIT_SLOT) || img.app_len > FW_APP_SLOT) {
        res->why = "the image was not checked";                                   // before any erase: started stays false
        return false;
    }
    {
        const char *type, *cwhy;
        if (!fw_flash_check(fl, &type, &cwhy)) {
            res->why = cwhy;
            return false;
        }
    }
    const uint32_t bslot = img.bit_slot ? img.bit_slot : FW_BIT_SLOT;
    Wr w = { fl, res, cb, ctx };
    // 1. the application, header last
    if (do_app && !write_range(w, FW_APP_ADDR, img.app, img.app_len, true, FWP_APP)) {
        return false;
    }
    // 2. the bitstream, front to back, then the rest of its slot erased
    if (do_bit) {
        if (!write_range(w, FW_BIT_ADDR, img.bit, img.bit_len, false, FWP_BIT)) {
            return false;
        }
        if (cb) {
            cb(ctx, FWP_BIT, 0, 0, "clearing the rest of the slot");
        }
        if (!clear_tail(w, FW_BIT_ADDR + img.bit_len, bslot, FWP_BIT)) {
            return false;
        }
    }
    // 3. the written regions read back whole, and the stamp walked in the flash
    uint32_t total = ((do_bit ? img.bit_len : 0) + (do_app ? img.app_len : 0) + FW_PAGE - 1) / FW_PAGE, done = 0, bad = 0;
    if (cb) {
        cb(ctx, FWP_READBACK, 0, total, "checking");
    }
    const struct { bool on; uint32_t addr; const uint8_t *p; uint32_t len; } reg[2] = { { do_app, FW_APP_ADDR, img.app, img.app_len },
                                                                                       { do_bit, FW_BIT_ADDR, img.bit, img.bit_len } };
    for (int k = 0; k < 2; k++) {
        if (!reg[k].on) {
            continue;
        }
        for (uint32_t off = 0; off < reg[k].len; off += 16 * FW_PAGE) {
            uint32_t n = reg[k].len - off < 16 * FW_PAGE ? reg[k].len - off : 16 * FW_PAGE;
            if (!region_equals(fl, reg[k].addr + off, reg[k].p + off, n, &bad)) {
                res->phase = FWP_READBACK; res->sector = bad; res->why = "the flash does not hold what was written";
                return false;
            }
            done += (n + FW_PAGE - 1) / FW_PAGE;
            if (cb) {
                cb(ctx, FWP_READBACK, done, total, "checking");
            }
        }
    }
    // the slot behind the bitstream reads blank
    for (uint32_t a = (do_bit ? (img.bit_len + FW_PAGE - 1) / FW_PAGE * FW_PAGE : bslot); a < bslot; a += FW_PAGE) {
        if (!fl->read_page((int)((FW_BIT_ADDR + a) / FW_PAGE), rd_buf)) {
            res->phase = FWP_READBACK; res->sector = a; res->why = "the flash could not be read";
            return false;
        }
        for (uint32_t i = 0; i < FW_PAGE; i++) {
            if (rd_buf[i] != 0xFF) {
                res->phase = FWP_READBACK; res->sector = a + i; res->why = "the rest of the bitstream slot is not blank";
                return false;
            }
        }
    }
    if (do_bit && img.axss) {
        uint32_t v = 0;
        bool found = false;
        if (!flash_axss(fl, &v, &found) || !found || v != img.axss) {
            res->phase = FWP_READBACK; res->sector = FW_BIT_ADDR; res->why = "the boot ROM's walk does not find the new stamp in the flash";
            return false;
        }
    }
    res->ok = true;
    return true;
}

// ---- reading a part into the staging buffer while it is hashed ------------------------------------------------------------------------
#define CHUNK 16384u

void fw_hash_begin(FwHash *h, uint32_t skip) { h->crc = 0; h->chk = 0; sha256_init(&h->sha); h->seen = 0; h->skip = skip; }

void fw_hash_feed(FwHash *h, const uint8_t *d, uint32_t n)
{
    h->crc = fw_crc32_update(h->crc, d, n);
    h->chk = fw_chk(h->chk, d, n);
    uint32_t s = h->seen < h->skip ? (h->skip - h->seen < n ? h->skip - h->seen : n) : 0;
    if (n > s) {
        sha256_update(&h->sha, d + s, n - s);
    }
    h->seen += n;
}

void fw_hash_sha_hex(FwHash *h, char out[65]) { uint8_t d[32]; sha256_final(&h->sha, d); sha256_hex(d, out); }

bool fw_read_part(CorSource &src, uint32_t off, uint8_t *dst, uint32_t n, uint32_t have, FwHash &h, FwStageProgress cb, void *ctx,
                  uint32_t *done_all, uint32_t total_all, bool (*abort)(void *))
{
    uint32_t done = 0;
    while (done < have) {
        if (abort && abort(ctx)) {
            return false;
        }
        uint32_t c = have - done > CHUNK ? CHUNK : have - done;
        if (!src.read(off + done, dst + done, c)) {
            return false;
        }
        fw_hash_feed(&h, dst + done, c);
        done += c;
        *done_all += c;
        if (cb) {
            cb(ctx, *done_all, total_all);
        }
    }
    if (n > have) {
        memset(dst + have, 0, n - have);
        fw_hash_feed(&h, dst + have, n - have);
    }
    return true;
}

static bool seal_impl(FwImage *img, bool checked, const uint8_t *bit, uint32_t bit_len, const uint8_t *app, uint32_t app_len, uint32_t axss, uint32_t bit_slot, const char **why);

// ---- the gate ------------------------------------------------------------------------------------------------------------------------
bool fw_image_seal(FwImage *img, bool checked, const uint8_t *bit, uint32_t bit_len, const uint8_t *app, uint32_t app_len, uint32_t axss, const char **why)
{
    if (!bit || !app) {                                                           // this gate is for both parts
        memset(img, 0, sizeof(*img));
        *why = !checked ? "the package did not pass its check" : (!bit ? "the bitstream does not fit its slot" : "the application does not fit its slot");
        return false;
    }
    return seal_impl(img, checked, bit, bit_len, app, app_len, axss, FW_BIT_SLOT_FULL, why);
}

bool fw_image_seal_parts(FwImage *img, bool checked, const uint8_t *bit, uint32_t bit_len, const uint8_t *app, uint32_t app_len, uint32_t axss, const char **why)
{
    return seal_impl(img, checked, bit, bit_len, app, app_len, axss, FW_BIT_SLOT, why);
}

static bool seal_impl(FwImage *img, bool checked, const uint8_t *bit, uint32_t bit_len, const uint8_t *app, uint32_t app_len, uint32_t axss, uint32_t bit_slot, const char **why)
{
    memset(img, 0, sizeof(*img));
    img->bit_slot = bit_slot;
    if (!checked) {
        *why = "the package did not pass its check";
        return false;
    }
    if (!bit && !app) {
        *why = "the package holds no part";
        return false;
    }
    if (bit && (bit_len == 0 || bit_len > bit_slot)) {
        *why = "the bitstream does not fit its slot";
        return false;
    }
    if (app && (app_len < FW_REC_HDR + 4 || app_len > FW_APP_SLOT)) {
        *why = "the application does not fit its slot";
        return false;
    }
    if (!app) {
        img->bit = bit; img->bit_len = bit_len; img->axss = axss; img->sealed = FW_SEALED;
        *why = "";
        return true;
    }
    // tools/trx_pkg.py check_app_record: what the boot ROM accepts
    uint32_t dest = le32(app), len = le32(app + 4), run = le32(app + 8);
    if (len == 0 || len > FW_APP_SLOT - FW_REC_HDR) {
        *why = "the application does not fit its slot";
        return false;
    }
    if ((dest & 3) || dest < 0x10000u || dest > 0x04000000u - len) {
        *why = "the application's load address is not one the boot ROM takes";
        return false;
    }
    if (run < dest || run >= dest + len) {
        *why = "the application's start address lies outside the image";
        return false;
    }
    if (app_len != FW_REC_HDR + ((len + 3u) & ~3u)) {
        *why = "the application record and its length disagree";
        return false;
    }
    img->bit = bit;
    img->bit_len = bit ? bit_len : 0;
    img->app = app;
    img->app_len = app_len;
    img->axss = bit ? axss : 0;
    img->sealed = FW_SEALED;
    *why = "";
    return true;
}

// ---- T265: the guard's record, the recovery program, the application as the flash holds it ---------------------------------------------------
uint32_t fw_guard_chk(uint32_t len, uint32_t crc) { return ~(FW_GUARD_MAGIC ^ len ^ crc); }

bool fw_read_record(Flash *fl, FwGuardRecord *rec)
{
    if (!fl->read_page((int)(FW_RECORD_ADDR / FW_PAGE), rd_buf)) {
        return false;
    }
    if (le32(rd_buf) != FW_GUARD_MAGIC || le32(rd_buf + 12) != fw_guard_chk(le32(rd_buf + 4), le32(rd_buf + 8))) {
        return false;
    }
    rec->len = le32(rd_buf + 4);
    rec->crc = le32(rd_buf + 8);
    return true;
}

bool fw_write_record(Flash *fl, uint32_t len, uint32_t crc, FwWriteResult *res)
{
    memset(res, 0, sizeof(*res));
    res->why = "";
    const char *type, *cwhy;
    if (!fw_flash_check(fl, &type, &cwhy)) {
        res->why = cwhy;
        return false;
    }
    Wr w = { fl, res, 0, 0 };
    if (!erase_sector_at(w, FW_RECORD_ADDR, FWP_APP)) {
        return false;
    }
    uint8_t rec[16];
    const uint32_t v[4] = { FW_GUARD_MAGIC, len, crc, fw_guard_chk(len, crc) };
    for (int i = 0; i < 16; i++) {
        rec[i] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
    }
    if (!program_page(w, FW_RECORD_ADDR, rec, sizeof(rec), 0, FWP_APP)) {
        return false;
    }
    res->ok = true;
    return true;
}

bool fw_recovery_equals(Flash *fl, const uint8_t *img, uint32_t len)
{
    uint32_t bad;
    return img && len && len <= FW_RECOVERY_SLOT && region_equals(fl, FW_RECOVERY_ADDR, img, len, &bad);
}

bool fw_write_recovery(Flash *fl, const uint8_t *img, uint32_t len, FwWriteResult *res)
{
    memset(res, 0, sizeof(*res));
    res->why = "";
    if (!img || len < FW_REC_HDR + 4 || len > FW_RECOVERY_SLOT) {
        res->why = "the recovery program does not fit its slot";
        return false;
    }
    const char *type, *cwhy;
    if (!fw_flash_check(fl, &type, &cwhy)) {
        res->why = cwhy;
        return false;
    }
    Wr w = { fl, res, 0, 0 };
    if (!write_range(w, FW_RECOVERY_ADDR, img, len, true, FWP_APP) || !clear_tail(w, FW_RECOVERY_ADDR + len, FW_RECORD_ADDR, FWP_APP)) {
        return false;
    }
    uint32_t bad;
    if (!region_equals(fl, FW_RECOVERY_ADDR, img, len, &bad)) {
        res->phase = FWP_READBACK; res->sector = bad; res->why = "the flash does not hold what was written";
        return false;
    }
    res->ok = true;
    return true;
}

bool fw_flash_app_crc(Flash *fl, uint32_t *len, uint32_t *crc)
{
    if (!fl->read_page((int)(FW_APP_ADDR / FW_PAGE), rd_buf)) {
        return false;
    }
    uint32_t dest = le32(rd_buf), dlen = le32(rd_buf + 4), run = le32(rd_buf + 8);
    if (dlen == 0 || dlen > FW_APP_SLOT - FW_REC_HDR || (dest & 3) || dest < 0x10000u || dest > 0x04000000u - dlen || run < dest || run >= dest + dlen) {
        return false;                                                           // the rules fw_image_seal_parts takes from the boot ROM
    }
    const uint32_t total = FW_REC_HDR + ((dlen + 3u) & ~3u);
    uint32_t c = 0;
    for (uint32_t off = 0; off < total; off += FW_PAGE) {
        if (!fl->read_page((int)((FW_APP_ADDR + off) / FW_PAGE), rd_buf)) {
            return false;
        }
        c = fw_crc32_update(c, rd_buf, total - off < FW_PAGE ? total - off : FW_PAGE);
    }
    *len = total;
    *crc = c;
    return true;
}
