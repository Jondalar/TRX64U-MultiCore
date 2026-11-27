// The cartridge header of a Super Nintendo image (T142 P8, S9, S12). See snes_rom.h. Written for this project (TREX) from the header layout every SNES
// document gives (title 0x00-0x14, map mode 0x15, chipset 0x16, ROM size 0x17, RAM size 0x18, country 0x19, complement 0x1C, checksum 0x1E, reset vector 0x3C of
// the 64 bytes) and from what the mapper (cores/snes/rtl/chip/DSP/DSP_LHRomMap.vhd) decodes of the mapper byte. No upstream code.
#include "snes_rom.h"
#include <stdio.h>
#include <string.h>

static const uint8_t PAL_COUNTRIES[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 17 };      // Europe, Scandinavia, Finland, Denmark, France, Netherlands, Spain, Germany, Italy, China, Indonesia, Australia

static int score_header(const uint8_t *h, int expect, uint64_t rom_bytes)
{
    int s = 0;
    uint16_t comp = (uint16_t)(h[0x1C] | (h[0x1D] << 8)), sum = (uint16_t)(h[0x1E] | (h[0x1F] << 8));
    if ((uint16_t)(comp + sum) == 0xFFFF && (comp != 0 || sum != 0)) {
        s += 4;
    }
    uint8_t mode = h[0x15];
    if ((mode & 0xE0) == 0x20) {
        s += 1;
        uint8_t m = mode & 0x0F;
        if ((expect == 0 && (m == 0 || m == 2 || m == 3)) || (expect == 1 && (m == 1 || m == 0x0A)) || (expect == 2 && m == 5)) {
            s += 2;
        }
    }
    uint16_t reset = (uint16_t)(h[0x3C] | (h[0x3D] << 8));
    if (reset >= 0x8000) {
        s += 2;
    }
    int printable = 0;
    for (int i = 0; i < 21; i++) {
        if (h[i] >= 0x20 && h[i] < 0x7F) {
            printable++;
        }
    }
    if (printable >= 16) {
        s += 1;
    }
    uint8_t sz = h[0x17];
    if (sz >= 8 && sz <= 0x0D) {
        uint64_t declared = 1024ull << sz;
        if (declared >= rom_bytes / 2 && declared <= rom_bytes * 2) {
            s += 1;
        }
    }
    return s;
}

// T218: which DSP-n a DSP cartridge has. The header only says "a DSP"; the four chips differ in their firmware and the known games are DSP-2 Dungeon Master,
// DSP-3 SD Gundam GX, DSP-4 Top Gear 3000, every other one a DSP-1.
static uint8_t dsp_of_title(const char *title)
{
    char t[22];
    int i = 0;
    for (; title[i] && i < 21; i++) {
        t[i] = (title[i] >= 'a' && title[i] <= 'z') ? (char)(title[i] - 32) : title[i];
    }
    t[i] = 0;
    if (strstr(t, "DUNGEON MASTER")) return 2;
    if (strstr(t, "SD GUNDAM GX")) return 3;
    if (strstr(t, "TOP GEAR 3000") || strstr(t, "TG3000")) return 4;
    return 1;
}

static const char *chip_name(uint8_t chipset)
{
    uint8_t hi = chipset >> 4, lo = chipset & 0x0F;
    if (hi == 0) {
        return 0;                           // 0..2: no chip; 3..6: DSP-n (T218: the application looks for the firmware), above 6 the check below
    }
    switch (hi) {
    case 1: return "the Super FX (GSU)";
    case 2: return 0;                       // OBC-1: supported
    case 3: return "the SA-1";
    case 4: return "the S-DD1";
    case 5: return 0;                       // S-RTC: supported
    case 0x0E: return "an add-on cartridge (Satellaview, Super Game Boy, Sufami Turbo)";
    case 0x0F:
        if (chipset == 0xF3 || chipset == 0xF4) return "the Cx4";
        if (chipset == 0xF5 || chipset == 0xF9) return "the SPC7110";
        if (chipset == 0xF6) return "the ST010 / ST011";
        return "a custom chip (Cx4, SPC7110, ST010)";
    default: return "an unknown coprocessor";
    }
}

SnesInfo snes_parse(const uint8_t *const hdr[3], uint64_t file_size)
{
    SnesInfo r;
    memset(&r, 0, sizeof(r));
    r.copier = (file_size % 1024 == 512) ? 512 : 0;
    uint64_t rom_bytes = file_size - r.copier;
    if (file_size < r.copier + 0x8000) {
        snprintf(r.err, sizeof(r.err), "This file is too short to be a Super Nintendo ROM (%u bytes).", (unsigned)file_size);
        return r;
    }
    r.rom_bytes = (uint32_t)(rom_bytes > 0xFFFFFFFFull ? 0xFFFFFFFFull : rom_bytes);
    if (rom_bytes > 0x800000) {
        snprintf(r.err, sizeof(r.err), "This ROM is larger than 8 MiB (%u KiB): the core's cartridge window is 8 MiB.", (unsigned)(rom_bytes >> 10));
        return r;
    }
    int best = -1, bscore = -1;
    for (int i = 0; i < 3; i++) {
        if (!hdr[i]) {
            continue;
        }
        int s = score_header(hdr[i], i, rom_bytes);
        // an ExHiROM image is larger than 4 MiB; a header at 0x40FFC0 in a smaller file is a coincidence
        if (s > bscore || (s == bscore && i > best && rom_bytes > 0x400000)) {
            best = i;
            bscore = s;
        }
    }
    if (best < 0 || bscore < 4) {
        snprintf(r.err, sizeof(r.err), "No Super Nintendo header found in this file (best score %d): not a LoROM, HiROM or ExHiROM image.", bscore < 0 ? 0 : bscore);
        r.score = bscore;
        return r;
    }
    const uint8_t *h = hdr[best];
    r.score = bscore;
    r.mapping = (uint8_t)best;
    r.chipset = h[0x16];
    memcpy(r.title, h, 21);
    r.title[21] = 0;
    for (int i = 0; i < 21; i++) {
        if (r.title[i] < 0x20 || r.title[i] >= 0x7F) {
            r.title[i] = ' ';
        }
    }
    for (int i = 20; i >= 0 && r.title[i] == ' '; i--) {
        r.title[i] = 0;
    }
    uint8_t hi = r.chipset >> 4, lo = r.chipset & 0x0F;
    const char *chip = chip_name(r.chipset);
    if (chip) {
        snprintf(r.err, sizeof(r.err), "\"%s\" needs %s, which this core does not have.", r.title, chip);
        return r;
    }
    if (lo > 6) {
        snprintf(r.err, sizeof(r.err), "\"%s\": the chipset byte %02X in the header is not one the core knows.", r.title, r.chipset);
        return r;
    }
    r.battery = (lo == 2 || lo == 5 || lo == 6);
    r.rom_type = r.mapping;
    if (hi == 0 && lo >= 3) {               // DSP-n: LoROM or HiROM (DSP_LHRomMap.vhd decodes its registers there), mapper byte bit 7 = a chip, bits 5:4 = which (VER)
        if (r.mapping == 2) {
            snprintf(r.err, sizeof(r.err), "\"%s\": a DSP cartridge that is ExHiROM; the core maps a DSP-n in LoROM and HiROM only.", r.title);
            return r;
        }
        r.dsp = dsp_of_title(r.title);
        r.rom_type = (uint8_t)(0x80 | ((r.dsp - 1) << 4) | r.mapping);
    }
    if (hi == 2) {                          // OBC-1: LoROM only
        if (r.mapping != 0) {
            snprintf(r.err, sizeof(r.err), "\"%s\": an OBC-1 cartridge that is not LoROM.", r.title);
            return r;
        }
        r.rom_type = 0xC0;
    } else if (hi == 5) {                   // S-RTC: ExHiROM (DSP_LHRomMap.vhd decodes its registers there and only there)
        if (r.mapping != 2) {
            snprintf(r.err, sizeof(r.err), "\"%s\": a cartridge with a real-time clock that is not ExHiROM.", r.title);
            return r;
        }
        r.rom_type = 0x0A;
    }
    // sizes as SNES.sv takes them from the header: rom_mask = (1024 << n) - 1, ram_mask = n ? (1024 << n) - 1 : 0 (n = the low nibble of the size byte).
    // Where the image is larger than the header says, the image wins (the core would read the part outside the mask as a mirror otherwise).
    uint32_t rsz = 1024u << (h[0x17] & 0x0F);
    uint32_t pow2 = 1;
    while (pow2 < rom_bytes) {
        pow2 <<= 1;
    }
    if (rsz < pow2) {
        rsz = pow2;
    }
    if (rsz > 0x800000) {
        rsz = 0x800000;
    }
    r.rom_mask = rsz - 1;
    unsigned rn = h[0x18] & 0x0F;
    if (rn > 8) {
        rn = 8;                             // 256 KiB is what the core has
    }
    r.ram_bytes = rn ? (1024u << rn) : 0;
    r.ram_mask = r.ram_bytes ? r.ram_bytes - 1 : 0;
    r.pal = false;
    for (unsigned i = 0; i < sizeof(PAL_COUNTRIES); i++) {
        if (h[0x19] == PAL_COUNTRIES[i]) {
            r.pal = true;
        }
    }
    r.ok = true;
    return r;
}

// ---- T218 D6: the byte order of a DSP-n firmware file --------------------------------------------------------------------------
// The rule is read from the chip, cores/snes/rtl/chip/DSP/DSPn.vhd, not from a file format: a program word is PROG_ROM_Q(23:0) with the type in 23:22 (l.115,
// INSTR_JP = "10", l.47-50). A JP word's branch field is OP_BRCH = bits 21:13 (l.126) and its target OP_NA = bits 12:2 (l.125); the decode (l.407-440) acts on a
// branch field only if it is 0 (PC <= SO), or 010xxxxxx with the condition code in bits 5:2 of the field being 0..12 or 15 (l.408-433: 13 and 14 fall to "others"),
// or 10xxxxxxx with the low six bits 0 (jump / call, l.436-442). Everything else is a no-op the firmware's author never wrote. So a JP word is "valid" when its
// branch field is one of those. In a real firmware (the proven dsp1b.bin: 273 of 273 JP words) every JP word is valid; read the wrong way round the same file has
// 5 of 130, random bytes about 4 %. OP/RT/LD words have no unused combination to test (every OP_P, OP_ALU, OP_SRC value decodes), so they do not vote.
// A reading "holds" when it has at least 16 JP words and at least 7/8 of them are valid. Turned only when the high-byte-first reading holds and ours does not.
static bool jp_valid(uint32_t w)
{
    uint32_t b = (w >> 13) & 0x1FF;
    if (b == 0) {
        return true;
    }
    if ((b >> 6) == 2) {
        uint32_t cc = (b >> 2) & 15;
        return cc <= 12 || cc == 15;
    }
    return (b >> 7) == 2 && (b & 0x3F) == 0;
}

static bool order_holds(const uint8_t *fw, bool high_first)
{
    unsigned jp = 0, ok = 0;
    for (unsigned i = 0; i < 2048; i++) {
        const uint8_t *b = fw + 3 * i;
        uint32_t w = high_first ? ((uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2]) : ((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16);
        if ((w >> 22) == 2) {
            jp++;
            ok += jp_valid(w);
        }
    }
    return jp >= 16 && ok * 8 >= jp * 7;
}

bool snes_dsp_fw_fix(uint8_t *fw)
{
    if (order_holds(fw, false) || !order_holds(fw, true)) {
        return false;
    }
    for (unsigned i = 0; i < 2048; i++) {                                    // the program: 3 bytes each
        uint8_t t = fw[3 * i];
        fw[3 * i] = fw[3 * i + 2];
        fw[3 * i + 2] = t;
    }
    for (unsigned i = 0; i < 1024; i++) {                                    // the data: 2 bytes each, after the 6144 program bytes
        uint8_t t = fw[6144 + 2 * i];
        fw[6144 + 2 * i] = fw[6144 + 2 * i + 1];
        fw[6144 + 2 * i + 1] = t;
    }
    return true;
}
