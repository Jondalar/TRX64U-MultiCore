// The cartridge header of a Super Nintendo image (T142 P8, S9, S12): which of the three places holds the header (LoROM at 0x7FC0, HiROM at 0xFFC0, ExHiROM
// at 0x40FFC0, 512 bytes further with a copier header), what the cartridge is (the chip named by the header's "chipset" byte), and the three values the core
// takes: the mapper byte (SNES_MiSTer's `rom_type`, which DSP_LHRomMap.vhd decodes), the ROM mask and the battery RAM mask (SNES.sv: `rom_mask = (1024 << size) - 1`,
// `ram_mask = ram_size ? (1024 << ram_size) - 1 : 0`), and the region. Pure: the caller reads the 64 bytes at each place.
#ifndef COREAPP_SNES_ROM_H
#define COREAPP_SNES_ROM_H

#include <stdint.h>

struct SnesInfo {
    bool ok;
    char err[200];                 // when !ok: why the core does not run this image
    char title[22];
    uint32_t copier;               // 0 or 512: bytes before the ROM in the file
    uint32_t rom_bytes;            // the file's size without the copier header
    uint8_t mapping;               // 0 LoROM, 1 HiROM, 2 ExHiROM
    uint8_t chipset;               // header byte 0x16
    uint8_t rom_type;              // SNES_ROM_TYPE
    uint32_t rom_mask, ram_mask;   // 24 bit
    uint32_t ram_bytes;
    bool pal;                      // the header's country code is a 50 Hz country
    bool battery;                  // the chipset byte says the RAM is battery backed
    int score;                     // the winner's score (the tests look at it)
};

// Where a header can be: file offsets without the copier header.
#define SNES_HDR_LO 0x7FC0u
#define SNES_HDR_HI 0xFFC0u
#define SNES_HDR_EX 0x40FFC0u

// hdr[0..2] = the 64 bytes at SNES_HDR_LO / HI / EX (+copier), or null when the file is too short for that place; file_size = the file's size.
SnesInfo snes_parse(const uint8_t *const hdr[3], uint64_t file_size);

#endif
