// The partition table the format writes (T111 D2): one entry, type 0x07, from LBA 8192 (4 MiB) to the end of the medium. Pure code.
// f_mkfs's own MBR (FM_SFD off) would start at sector 63 and is not used; Disk::Init (host-app/software/filesystem/disk.cc) finds an exFAT
// volume only inside a partition table, so the table is part of the format.
#ifndef HOST_MBR_H
#define HOST_MBR_H

#include <stdint.h>

#define MBR_PART_START   8192u        // sectors: 4 MiB with 512-byte sectors; also the alignment f_mkfs gets (MKFS_PARM.align)
#define MBR_TYPE_EXFAT   0x07

// Builds the 512-byte sector 0 for a medium of `total` sectors: entry 0 = {boot 0, CHS 0xFE 0xFF 0xFF both ends, type 0x07,
// start 8192, length total - 8192}, entries 1-3 zero, no boot code, 0x55AA. disk_sig is the 32-bit disk signature at offset 440.
void mbr_build(uint8_t sector[512], uint32_t total, uint32_t disk_sig);

#endif
