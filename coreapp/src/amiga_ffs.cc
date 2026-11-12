#include "amiga_ffs.h"
#include <string.h>

uint32_t ffs_sum(const uint8_t *b, int longs)
{
    uint32_t s = 0;
    for (int i = 0; i < longs; i++) {
        s += ffs_rd(b, i);
    }
    return s;
}

void ffs_seal(uint8_t *b, int chk_long, int longs)
{
    ffs_wr(b, chk_long, 0);
    ffs_wr(b, chk_long, (uint32_t)(0u - ffs_sum(b, longs)));
}

bool ffs_sum_ok(const uint8_t *b, int longs)
{
    return ffs_sum(b, longs) == 0;
}

uint8_t ffs_upper(uint8_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
        return (uint8_t)(c - 0x20);
    }
    return c;
}

uint32_t ffs_hash(const uint8_t *name, int len)
{
    uint32_t h = (uint32_t)len;
    for (int i = 0; i < len; i++) {
        h = (h * 13 + ffs_upper(name[i])) & 0x7FF;
    }
    return h % FFS_HT;
}

bool ffs_name_eq(const uint8_t *a, int al, const uint8_t *b, int bl)
{
    if (al != bl) {
        return false;
    }
    for (int i = 0; i < al; i++) {
        if (ffs_upper(a[i]) != ffs_upper(b[i])) {
            return false;
        }
    }
    return true;
}

// ---- dates: days since 1978-01-01 (a civil calendar, no time zone: the FAT time is taken as it is) ----
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

#define AMIGA_EPOCH_DAYS 2922          // 1978-01-01 in days since 1970-01-01

AmDate ffs_date_from_fat(uint16_t fdate, uint16_t ftime)
{
    int y = 1980 + (fdate >> 9), mo = (fdate >> 5) & 15, d = fdate & 31;
    int h = ftime >> 11, mi = (ftime >> 5) & 63, s = (ftime & 31) * 2;
    if (mo < 1 || mo > 12) mo = 1;
    if (d < 1) d = 1;
    if (h > 23) h = 23;
    if (mi > 59) mi = 59;
    if (s > 59) s = 59;
    AmDate a;
    int64_t days = days_from_civil(y, mo, d) - AMIGA_EPOCH_DAYS;
    a.days = days < 0 ? 0 : (uint32_t)days;
    a.mins = (uint32_t)(h * 60 + mi);
    a.ticks = (uint32_t)(s * 50);
    return a;
}

void ffs_date_to_fat(const AmDate &a, uint16_t *fdate, uint16_t *ftime)
{
    int y, mo, d, h, mi, s, hund;
    ffs_date_civil(a, &y, &mo, &d, &h, &mi, &s, &hund);
    if (y < 1980) { y = 1980; mo = 1; d = 1; h = mi = s = 0; }
    if (y > 2107) { y = 2107; mo = 12; d = 31; h = 23; mi = 59; s = 58; }
    *fdate = (uint16_t)(((y - 1980) << 9) | (mo << 5) | d);
    *ftime = (uint16_t)((h << 11) | (mi << 5) | (s / 2));
}

void ffs_date_civil(const AmDate &a, int *y, int *mo, int *dd, int *h, int *mi, int *s, int *hund)
{
    civil_from_days((int64_t)a.days + AMIGA_EPOCH_DAYS, y, mo, dd);
    *h = (int)(a.mins / 60);
    *mi = (int)(a.mins % 60);
    *s = (int)(a.ticks / 50);
    *hund = (int)(a.ticks % 50) * 2;
}

bool ffs_date_from_civil(int y, int mo, int dd, int h, int mi, int s, int hund, AmDate *out)
{
    if (y < 1978 || mo < 1 || mo > 12 || dd < 1 || dd > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59 || hund < 0 || hund > 99) {
        return false;
    }
    out->days = (uint32_t)(days_from_civil(y, mo, dd) - AMIGA_EPOCH_DAYS);
    out->mins = (uint32_t)(h * 60 + mi);
    out->ticks = (uint32_t)(s * 50 + hund / 2);
    return true;
}

// ---- RDB ----
static void put_str(uint8_t *b, int byteoff, const char *s, int n)
{
    int i = 0;
    for (; i < n && s[i]; i++) {
        b[byteoff + i] = (uint8_t)s[i];
    }
    for (; i < n; i++) {
        b[byteoff + i] = ' ';
    }
}

void ffs_build_rdb(uint8_t *b, uint32_t cylinders)
{
    memset(b, 0, FFS_BLOCK);
    memcpy(b, "RDSK", 4);
    ffs_wr(b, 1, 64);
    ffs_wr(b, 3, 7);
    ffs_wr(b, 4, FFS_BLOCK);
    ffs_wr(b, 5, 0x12);
    for (int i = 6; i <= 15; i++) {
        ffs_wr(b, i, 0xFFFFFFFFu);
    }
    ffs_wr(b, 7, 1);                                   // the partition list: block 1
    ffs_wr(b, 16, cylinders);
    ffs_wr(b, 17, FFS_SECS);
    ffs_wr(b, 18, FFS_HEADS);
    ffs_wr(b, 19, 1);
    ffs_wr(b, 20, cylinders);
    ffs_wr(b, 24, cylinders);
    ffs_wr(b, 25, cylinders);
    ffs_wr(b, 26, 3);
    ffs_wr(b, 32, 0);                                  // RDB blocks 0 .. 1007 (cylinder 0)
    ffs_wr(b, 33, FFS_CYL_BLOCKS - 1);
    ffs_wr(b, 34, 1);
    ffs_wr(b, 35, cylinders - 1);
    ffs_wr(b, 36, FFS_CYL_BLOCKS);
    ffs_wr(b, 38, 1);
    put_str(b, 40 * 4, "TRX64", 8);
    put_str(b, 42 * 4, "Folder disk", 16);
    put_str(b, 46 * 4, "1.0", 4);
    ffs_seal(b, 2, 64);
}

void ffs_build_part(uint8_t *b, uint32_t cylinders, const char *drive_name)
{
    memset(b, 0, FFS_BLOCK);
    memcpy(b, "PART", 4);
    ffs_wr(b, 1, 64);
    ffs_wr(b, 3, 7);
    ffs_wr(b, 4, 0xFFFFFFFFu);
    ffs_wr(b, 5, 1);                                   // PBFF_BOOTABLE
    int n = (int)strlen(drive_name);
    if (n > 30) n = 30;
    b[9 * 4] = (uint8_t)n;
    memcpy(b + 9 * 4 + 1, drive_name, (size_t)n);
    ffs_wr(b, 32, 16);                                 // DosEnvec
    ffs_wr(b, 33, 128);
    ffs_wr(b, 34, 0);
    ffs_wr(b, 35, FFS_HEADS);
    ffs_wr(b, 36, 1);
    ffs_wr(b, 37, FFS_SECS);
    ffs_wr(b, 38, FFS_RESERVED);
    ffs_wr(b, 39, 0);
    ffs_wr(b, 40, 0);
    ffs_wr(b, 41, 1);
    ffs_wr(b, 42, cylinders - 1);
    ffs_wr(b, 43, 30);
    ffs_wr(b, 44, 0);
    ffs_wr(b, 45, 0x00FFFFFF);
    ffs_wr(b, 46, 0x7FFFFFFE);
    ffs_wr(b, 47, 0);
    ffs_wr(b, 48, FFS_DOSTYPE);
    ffs_seal(b, 2, 64);
}

void ffs_build_boot(uint8_t *b, uint32_t root)
{
    memset(b, 0, 2 * FFS_BLOCK);
    ffs_wr(b, 0, FFS_DOSTYPE);
    ffs_wr(b, 2, root);
    uint32_t sum = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t v = ffs_rd(b, i);
        sum += v;
        if (sum < v) {
            sum++;
        }
    }
    ffs_wr(b, 1, ~sum);
}

// ---- objects ----
static void put_common(uint8_t *b, const FfsObj &o)
{
    ffs_wr(b, FL_PROTECT, o.protect);
    int cl = o.clen > FFS_COMMENT_MAX ? FFS_COMMENT_MAX : o.clen;
    if (cl > 0) {
        b[FL_COMMENT * 4] = (uint8_t)cl;
        memcpy(b + FL_COMMENT * 4 + 1, o.comment, (size_t)cl);
    }
    ffs_wr(b, FL_DAYS, o.date.days);
    ffs_wr(b, FL_MINS, o.date.mins);
    ffs_wr(b, FL_TICKS, o.date.ticks);
    int nl = o.nlen > FFS_NAME_MAX ? FFS_NAME_MAX : o.nlen;
    b[FL_NAME * 4] = (uint8_t)nl;
    memcpy(b + FL_NAME * 4 + 1, o.name, (size_t)nl);
    ffs_wr(b, FL_HASHCHAIN, o.hash_next);
    ffs_wr(b, FL_PARENT, o.parent);
}

void ffs_build_file(uint8_t *b, uint32_t own, const FfsObj &o, uint32_t first_data, uint32_t ndata, uint32_t ext)
{
    memset(b, 0, FFS_BLOCK);
    ffs_wr(b, FL_TYPE, FFS_T_HEADER);
    ffs_wr(b, FL_KEY, own);
    uint32_t n = ndata > FFS_HT ? FFS_HT : ndata;
    ffs_wr(b, FL_HIGHSEQ, n);
    ffs_wr(b, FL_FIRSTDATA, ndata ? first_data : 0);
    for (uint32_t i = 0; i < n; i++) {
        ffs_wr(b, FL_TABLE + FFS_HT - 1 - (int)i, first_data + i);
    }
    put_common(b, o);
    ffs_wr(b, FL_BYTESIZE, o.size);
    ffs_wr(b, FL_EXTENSION, ext);
    ffs_wr(b, FL_SECTYPE, FFS_ST_FILE);
    ffs_seal(b, FL_CHK, FFS_LONGS);
}

void ffs_build_ext(uint8_t *b, uint32_t own, uint32_t header, uint32_t first_data, uint32_t ndata, uint32_t e, uint32_t ext)
{
    memset(b, 0, FFS_BLOCK);
    ffs_wr(b, FL_TYPE, FFS_T_LIST);
    ffs_wr(b, FL_KEY, own);
    uint32_t base = FFS_HT * (e + 1);
    uint32_t n = ndata > base ? ndata - base : 0;
    if (n > FFS_HT) n = FFS_HT;
    ffs_wr(b, FL_HIGHSEQ, n);
    for (uint32_t i = 0; i < n; i++) {
        ffs_wr(b, FL_TABLE + FFS_HT - 1 - (int)i, first_data + base + i);
    }
    ffs_wr(b, FL_PARENT, header);
    ffs_wr(b, FL_EXTENSION, ext);
    ffs_wr(b, FL_SECTYPE, FFS_ST_FILE);
    ffs_seal(b, FL_CHK, FFS_LONGS);
}

void ffs_build_dir(uint8_t *b, uint32_t own, const FfsObj &o, const uint32_t heads[FFS_HT])
{
    memset(b, 0, FFS_BLOCK);
    ffs_wr(b, FL_TYPE, FFS_T_HEADER);
    ffs_wr(b, FL_KEY, own);
    for (int i = 0; i < FFS_HT; i++) {
        ffs_wr(b, FL_TABLE + i, heads[i]);
    }
    put_common(b, o);
    ffs_wr(b, FL_SECTYPE, FFS_ST_DIR);
    ffs_seal(b, FL_CHK, FFS_LONGS);
}

void ffs_build_root(uint8_t *b, const char *name, const AmDate &now, const uint32_t heads[FFS_HT], uint32_t bm_valid,
                    const uint32_t *bm_pages, int nbm_pages, uint32_t bm_ext)
{
    memset(b, 0, FFS_BLOCK);
    ffs_wr(b, FL_TYPE, FFS_T_HEADER);
    ffs_wr(b, FL_HTSIZE, FFS_HT);
    for (int i = 0; i < FFS_HT; i++) {
        ffs_wr(b, FL_TABLE + i, heads[i]);
    }
    ffs_wr(b, FL_BM_FLAG, bm_valid);
    for (int i = 0; i < nbm_pages && i < FFS_BM_PER_ROOT; i++) {
        ffs_wr(b, FL_BM_PAGES + i, bm_pages[i]);
    }
    ffs_wr(b, FL_BM_EXT, bm_ext);
    ffs_wr(b, FL_DAYS, now.days);
    ffs_wr(b, FL_MINS, now.mins);
    ffs_wr(b, FL_TICKS, now.ticks);
    int nl = (int)strlen(name);
    if (nl > FFS_NAME_MAX) nl = FFS_NAME_MAX;
    b[FL_NAME * 4] = (uint8_t)nl;
    memcpy(b + FL_NAME * 4 + 1, name, (size_t)nl);
    ffs_wr(b, FL_DISK_DAYS, now.days);
    ffs_wr(b, FL_DISK_DAYS + 1, now.mins);
    ffs_wr(b, FL_DISK_DAYS + 2, now.ticks);
    ffs_wr(b, FL_CREATE_DAYS, now.days);
    ffs_wr(b, FL_CREATE_DAYS + 1, now.mins);
    ffs_wr(b, FL_CREATE_DAYS + 2, now.ticks);
    ffs_wr(b, FL_SECTYPE, FFS_ST_ROOT);
    ffs_seal(b, FL_CHK, FFS_LONGS);
}

void ffs_build_bitmap(uint8_t *b, const uint32_t *longs)
{
    memset(b, 0, FFS_BLOCK);
    for (int i = 0; i < FFS_BM_PER_BLOCK; i++) {
        ffs_wr(b, 1 + i, longs[i]);
    }
    ffs_seal(b, 0, FFS_LONGS);
}

void ffs_build_bm_ext(uint8_t *b, const uint32_t *ptrs, int n, uint32_t next)
{
    memset(b, 0, FFS_BLOCK);
    for (int i = 0; i < n && i < FFS_BM_EXT_PTRS; i++) {
        ffs_wr(b, i, ptrs[i]);
    }
    ffs_wr(b, FFS_LONGS - 1, next);
}

bool ffs_parse_hdr(const uint8_t *b, FfsHdr *h)
{
    memset(h, 0, sizeof(*h));
    if (!ffs_sum_ok(b, FFS_LONGS) || ffs_rd(b, FL_TYPE) != FFS_T_HEADER) {
        return false;
    }
    h->type = ffs_rd(b, FL_TYPE);
    h->sectype = ffs_rd(b, FL_SECTYPE);
    h->key = ffs_rd(b, FL_KEY);
    h->highseq = ffs_rd(b, FL_HIGHSEQ);
    h->first_data = ffs_rd(b, FL_FIRSTDATA);
    h->protect = ffs_rd(b, FL_PROTECT);
    h->size = ffs_rd(b, FL_BYTESIZE);
    h->parent = ffs_rd(b, FL_PARENT);
    h->chain = ffs_rd(b, FL_HASHCHAIN);
    h->ext = ffs_rd(b, FL_EXTENSION);
    int nl = b[FL_NAME * 4];
    if (nl > FFS_NAME_MAX) nl = FFS_NAME_MAX;
    h->nlen = nl;
    memcpy(h->name, b + FL_NAME * 4 + 1, (size_t)nl);
    int cl = b[FL_COMMENT * 4];
    if (cl > FFS_COMMENT_MAX) cl = FFS_COMMENT_MAX;
    h->clen = cl;
    memcpy(h->comment, b + FL_COMMENT * 4 + 1, (size_t)cl);
    h->date.days = ffs_rd(b, FL_DAYS);
    h->date.mins = ffs_rd(b, FL_MINS);
    h->date.ticks = ffs_rd(b, FL_TICKS);
    return true;
}
