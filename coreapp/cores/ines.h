// iNES / NES 2.0 headers (T93 D6, T95 5.8-5.9). The core derives its own mapper flags word from the 16 header bytes the
// application writes into the INES registers (nes_flags.v); this file parses the same bytes on the application's side to
// refuse what the core cannot run before the machine is touched, and computes that word with the same expressions so the
// two can be compared (tests/coreapp/test_ines.cc against hand-computed words).
#ifndef COREAPP_INES_H
#define COREAPP_INES_H

#include <stdint.h>

struct InesInfo {
    bool ok;
    char err[200];
    bool nes20;
    uint16_t mapper;           // the mapper number as the core's word carries it ({ines[8][3:0], mapper[7:0]} for NES 2.0)
    uint8_t submapper;
    uint32_t prg_bytes, chr_bytes;
    bool chr_ram;              // no CHR ROM: the machine's 8 KB CHR RAM
    bool trainer;              // 512 bytes between the header and the PRG
    bool battery;
    bool four_screen;
    bool vertical;
    uint32_t prg_off, chr_off; // byte offsets in the file
    uint64_t flags;            // nes_flags.v's mapper_flags
};

uint64_t ines_mapper_flags(const uint8_t h[16]);

// head = the file's first 16 bytes; file_size = its size. Refuses: no "NES" 0x1A magic, a mapper the core does not have, a PRG or
// CHR larger than the 2 MB each has in the DDR2 window, a file shorter than the header says.
InesInfo ines_parse(const uint8_t *head, uint32_t file_size);

// The mapper numbers cores/nes/gen builds (T95 5.10).
bool ines_mapper_supported(unsigned mapper);

#endif
