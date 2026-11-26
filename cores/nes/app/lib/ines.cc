#include "ines.h"
#include <stdio.h>
#include <string.h>

// The mapper numbers of the ten built modules (T95 5.10): Mapper28 (0, 2, 3, 7, 28, 94, 97, 180, 185), MMC1 (1, 155, 171),
// MMC2 (9), MMC4 (10), MMC3 (4, 33, 37, 45, 47, 48, 52, 74, 76, 80, 82, 88, 95, 112, 118, 119, 154, 189, 191, 192, 194-196,
// 205-208, 268), VRC24 (21, 22, 23, 25, 27), Mapper66 (11, 38, 46, 66, 86, 87, 101, 140, 144, 145, 149), Mapper34 (34),
// Mapper71 (71, 232), Mapper79 (79, 113, 133, 146, 148).
static const uint16_t supported[] = {
    0, 2, 3, 7, 28, 94, 97, 180, 185, 1, 155, 171, 9, 10,
    4, 33, 37, 45, 47, 48, 52, 74, 76, 80, 82, 88, 95, 112, 118, 119, 154, 189, 191, 192, 194, 195, 196, 205, 206, 207, 208, 268,
    21, 22, 23, 25, 27, 11, 38, 46, 66, 86, 87, 101, 140, 144, 145, 149, 34, 71, 232, 79, 113, 133, 146, 148,
};

bool ines_mapper_supported(unsigned m)
{
    for (uint16_t s : supported) {
        if (s == m) {
            return true;
        }
    }
    return false;
}

static unsigned size3(unsigned pages)
{
    return pages <= 1 ? 0 : pages <= 2 ? 1 : pages <= 4 ? 2 : pages <= 8 ? 3 : pages <= 16 ? 4 : pages <= 32 ? 5 : pages <= 64 ? 6 : 7;
}

// Expression for expression what nes_flags.v assigns (ines[i] = header byte i).
uint64_t ines_mapper_flags(const uint8_t *ines)
{
    unsigned prgrom = ines[4], chrrom = ines[5], chrram = ines[11] & 15;
    bool is_nes20 = ((ines[7] >> 2) & 3) == 2;
    bool has_chr_ram = !is_nes20 ? chrrom == 0 : chrram != 0;
    bool is_dirty = !is_nes20 && ((ines[9] >> 1) != 0 || ines[10] || ines[11] || ines[12] || ines[13] || ines[14] || ines[15]);
    unsigned mapper = (unsigned)(((is_dirty ? 0 : ines[7] >> 4) << 4) | (ines[6] >> 4));
    unsigned ines2mapper = is_nes20 ? ines[8] : 0;
    unsigned prgram = is_nes20 ? ines[10] & 15 : 0;
    unsigned prg_nvram = is_nes20 ? ines[10] >> 4 : 0;
    bool piano = is_nes20 && (ines[15] & 0x3F) == 0x19;
    bool is_vs = is_nes20 && (ines[7] & 3) == 1;
    unsigned vs_hw = is_vs ? ines[13] >> 4 : 0;
    bool mapper99 = (((ines2mapper & 3) << 8) | mapper) == 99;
    bool is_nes20_prg = is_nes20 && (ines[9] & 15) == 15, is_nes20_chr = is_nes20 && (ines[9] >> 4) == 15;
    uint32_t prg_size2 = is_nes20_prg ? ((uint32_t)(((ines[4] & 3) << 1) | 1) << (ines[4] >> 2)) & 0x3FFFFF : (uint32_t)prgrom << 14;
    uint32_t chr_size2 = is_nes20_chr ? ((uint32_t)(((ines[5] & 3) << 1) | 1) << (ines[5] >> 2)) & 0x3FFFFF : (uint32_t)chrrom << 13;
    bool dual = mapper99 && vs_hw == 5, raid = mapper99 && vs_hw == 6;
    uint64_t f = 0;
    f |= (uint64_t)(is_nes20 ? ines[15] & 0x7F : 0) << 53;
    f |= (uint64_t)vs_hw << 49;
    f |= (uint64_t)(is_vs ? ines[13] & 15 : 0) << 45;
    f |= (uint64_t)(is_nes20 ? ines[7] & 3 : 0) << 43;
    f |= (uint64_t)(mapper99 ? (chr_size2 >> 13) & 3 : 0) << 41;
    f |= (uint64_t)(dual ? (prg_size2 >> 14) & 7 : raid ? 4 : mapper99 ? (prg_size2 >> 13) & 7 : 0) << 38;
    f |= (uint64_t)(is_nes20 ? ines[12] & 3 : 0) << 36;
    f |= (uint64_t)is_nes20 << 35;
    f |= (uint64_t)prg_nvram << 31;
    f |= (uint64_t)piano << 30;
    f |= (uint64_t)prgram << 26;
    f |= (uint64_t)((ines[6] >> 1) & 1) << 25;
    f |= (uint64_t)ines2mapper << 17;
    f |= (uint64_t)((ines[6] >> 3) & 1) << 16;
    f |= (uint64_t)has_chr_ram << 15;
    f |= (uint64_t)(ines[6] & 1) << 14;
    f |= (uint64_t)size3(chrrom) << 11;
    f |= (uint64_t)size3(prgrom) << 8;
    f |= mapper;
    return f;
}

InesInfo ines_parse(const uint8_t *h, uint32_t file_size)
{
    InesInfo r;
    memset(&r, 0, sizeof(r));
    if (file_size < 16 || memcmp(h, "NES\x1A", 4) != 0) {
        snprintf(r.err, sizeof(r.err), "not an iNES file (no \"NES\" 1A at the start)");
        return r;
    }
    r.nes20 = ((h[7] >> 2) & 3) == 2;
    bool dirty = !r.nes20 && ((h[9] >> 1) != 0 || h[10] || h[11] || h[12] || h[13] || h[14] || h[15]);
    r.mapper = (uint16_t)(((dirty ? 0 : h[7] >> 4) << 4) | (h[6] >> 4));
    if (r.nes20) {
        r.mapper |= (uint16_t)((h[8] & 15) << 8);
        r.submapper = h[8] >> 4;
    }
    bool prg_exp = r.nes20 && (h[9] & 15) == 15, chr_exp = r.nes20 && (h[9] >> 4) == 15;
    r.prg_bytes = prg_exp ? (((uint32_t)(((h[4] & 3) << 1) | 1)) << (h[4] >> 2)) : (uint32_t)h[4] << 14;
    r.chr_bytes = chr_exp ? (((uint32_t)(((h[5] & 3) << 1) | 1)) << (h[5] >> 2)) : (uint32_t)h[5] << 13;
    r.trainer = (h[6] & 4) != 0;
    r.battery = (h[6] & 2) != 0;
    r.four_screen = (h[6] & 8) != 0;
    r.vertical = (h[6] & 1) != 0;
    r.chr_ram = r.nes20 ? (h[11] & 15) != 0 : h[5] == 0;
    r.prg_off = 16 + (r.trainer ? 512 : 0);
    r.chr_off = r.prg_off + r.prg_bytes;
    r.flags = ines_mapper_flags(h);
    if (r.prg_bytes == 0) {
        snprintf(r.err, sizeof(r.err), "the header says there is no PRG ROM");
    } else if (!ines_mapper_supported(r.mapper)) {
        snprintf(r.err, sizeof(r.err), "mapper %u is not one this core has", r.mapper);
    } else if (r.prg_bytes > 0x200000u || r.chr_bytes > 0x200000u) {
        snprintf(r.err, sizeof(r.err), "PRG %u KB / CHR %u KB: the core holds 2 MB of each", (unsigned)(r.prg_bytes >> 10), (unsigned)(r.chr_bytes >> 10));
    } else if ((uint64_t)r.chr_off + r.chr_bytes > file_size) {
        snprintf(r.err, sizeof(r.err), "the file is %u bytes but the header needs %u", (unsigned)file_size, (unsigned)(r.chr_off + r.chr_bytes));
    } else {
        r.ok = true;
    }
    return r;
}
