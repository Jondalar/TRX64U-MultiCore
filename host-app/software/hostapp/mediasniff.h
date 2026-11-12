// What is on a medium, read from its first sectors (T111 D6): the file system type, the label, the number of partitions. Pure code: the
// sectors come through a read callback, so tests/hostapp/test_mediasniff.cc runs it over disk images and the board over the block device.
// It only looks; nothing is mounted and nothing is written.
#ifndef HOST_MEDIASNIFF_H
#define HOST_MEDIASNIFF_H

#include <stdint.h>

enum MediaType {
    MT_NONE,        // blank: no 0x55AA in sector 0, or a partition table with no partition
    MT_FAT12, MT_FAT16, MT_FAT32,
    MT_EXFAT,
    MT_GPT,         // a protective MBR (type 0xEE) with a GPT behind it
    MT_MULTI,       // two or more partitions in the MBR
    MT_OTHER,       // one partition, but a file system this does not know (NTFS, ext, ...)
    MT_ERROR        // a sector could not be read
};

// Reads the sector `lba` (sector_size bytes) into buf. False = the medium did not answer.
typedef bool (*MediaRead)(void *ctx, uint32_t lba, uint8_t *buf);

struct MediaSniff {
    MediaType type;
    char label[16];          // FAT: the 11 characters of the boot sector without trailing blanks, "NO NAME" = none; exFAT: the label entry of the root directory
    int parts;               // partition entries found (GPT: used entries within the first 16)
    bool mbr;                // the volume sits in a partition table (false: sector 0 is the volume, "super floppy")
    uint32_t sectors;        // the size of the medium as given
    uint32_t part_start;     // the one volume: first sector and length (type FAT*, EXFAT, OTHER)
    uint32_t part_len;
    uint32_t cluster_bytes;  // the volume's cluster size (FAT*, EXFAT)
};

// sector_size: 512..4096. A medium with another sector size is read as it is; the types are recognised the same way.
void media_sniff(MediaRead rd, void *ctx, uint32_t sectors, uint32_t sector_size, MediaSniff *out);

const char *media_type_name(MediaType t);     // "exFAT", "FAT32", "blank", ...

#endif
