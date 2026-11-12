// A read-only walk over the FAT12/16 cluster chain of one file on a volume, to tell whether the file lies in ONE run
// of clusters and where (T115 D2). The volume is the flash drive: its sectors are linear from the drive's start, so a
// contiguous file is also contiguous in the SPI flash and the FPGA can load a bitstream from its first byte.
// Pure code: the bytes come through a read callback, so tests/hostapp/test_fatwalk.cc runs it over a fake image.
#ifndef HOST_FATWALK_H
#define HOST_FATWALK_H

#include <stdint.h>

typedef enum {
    FAT_OK = 0,
    FAT_ERR_IO,          // the read callback failed
    FAT_ERR_GEOMETRY,    // no FAT12/16 boot sector
    FAT_ERR_FRAGMENTED,  // the chain leaves the run of consecutive clusters
    FAT_ERR_SIZE,        // the chain is shorter or longer than the file's size needs
    FAT_ERR_CLUSTER      // a start cluster, a free or bad cluster in the chain, out of range
} fat_result_t;

const char *fat_result_str(fat_result_t r);

// Reads n bytes at byte offset `off` from the start of the volume.
typedef bool (*fat_read_t)(void *ctx, uint32_t off, void *buf, uint32_t n);

struct FatGeo {
    uint32_t bps, spc;        // bytes per sector, sectors per cluster
    uint32_t csize;           // bytes per cluster
    uint32_t fat_off;         // byte offset of the first FAT
    uint32_t data_off;        // byte offset of cluster 2
    uint32_t nclust;          // clusters in the data area (numbered 2 ... nclust + 1)
    int fatbits;              // 12 or 16
};

// Parses the boot sector at offset 0. FAT32 and exFAT are refused.
fat_result_t fat_geometry(fat_read_t rd, void *ctx, FatGeo *g);

// The file of `size` bytes whose first cluster is `first`: the chain must be first, first + 1, ... for exactly
// ceil(size / csize) clusters and then end. On FAT_OK *vol_off is the byte offset of its first byte from the start of
// the volume: data_off + (first - 2) * csize. The flash address is the drive's start address plus that (D2).
fat_result_t fat_contiguous(const FatGeo *g, fat_read_t rd, void *ctx, uint32_t first, uint32_t size, uint32_t *vol_off);

// The bytes a file of `size` bytes takes when it lies in one run of clusters: whole clusters.
uint32_t fat_run_bytes(const FatGeo *g, uint32_t size);

// The text for a start that is refused because the flash drive has no free run of `size` bytes in one piece (f_expand: FR_DENIED).
void fat_no_run_text(char *out, unsigned max, const FatGeo *g, uint32_t size);

#endif
