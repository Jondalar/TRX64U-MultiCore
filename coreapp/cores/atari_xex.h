// A .xex file presented as a bootable disk (T93 D6, T88 D8): sectors 1-3 are the loader of 6502/xexboot.asm, sectors 4... the
// file. The core's own XEX mode (FLAGS b2, the PBI loader ROM) is not used: it reads a hardware block the core does not have
// (a8_core.vhd has no SD emulation behind $D1xx), a boot disk over the SIO bridge needs nothing from the gateware.
#ifndef COREAPP_ATARI_XEX_H
#define COREAPP_ATARI_XEX_H

#include "atari_disk.h"
#include "fsif.h"

#define XEX_MAX_BYTES 0xFFFFFFu            // the loader counts the file in 24 bits

AtariDisk *xex_open(CoreFs *fs, const char *path, char *err, int errlen);

// The three boot sectors with the file length patched in (384 bytes).
void xex_boot_sectors(uint32_t file_len, uint8_t out[384]);

#endif
