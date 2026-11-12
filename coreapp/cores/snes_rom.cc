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

static const char *chip_name(uint8_t chipset)
{
    uint8_t hi = chipset >> 4, lo = chipset & 0x0F;
    if (hi == 0) {
        return lo <= 2 ? 0 : "a DSP-n coprocessor (DSP-1, 2, 3 or 4: its firmware is not in the core)";
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
