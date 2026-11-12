// Formatting a medium as exFAT (T111 D1-D5, D8). FatFs's f_mkfs writes the file system, our own mbr.cc the partition table; this file is the
// order they come in, the checks before the first write, the progress and the cancel, and the shim the writes go through.
//
//   (1) checks         no medium / write protected / smaller than 64 MiB / larger than 2 TiB / 4096-byte sectors / too little memory: refused, nothing written
//   (2) detach         the host lets go of the volume (ops->detach): unmount, drop the drive, free the Disk
//   (3) sector 0 := 0  the old partition table (or the boot sector of a volume without one) is dead from here on
//   (4) f_mkfs         FM_EXFAT | FM_SFD, one FAT, align 8192, cluster size by FatFs's rule, on a Partition(start 8192) of ours
//   (5) MBR            the one entry, last: a table only ever points at a finished volume
//   (6) sync
//   (7) re-attach and verify (ops->attach): the host mounts what was written and it must be an exFAT volume of the geometry computed in (1)
// Any failure between (3) and (5) leaves a medium without a partition table: Disk::Init finds no volume, f_mount fails. Never the old table
// over new structures, never the new table over old ones.
#ifndef HOST_FMT_H
#define HOST_FMT_H

#include <stdint.h>
#include "blockdev.h"

enum FmtResult { FMT_OK = 0, FMT_REFUSED, FMT_FAILED, FMT_CANCELLED };

enum FmtStep { FST_RELEASE = 0, FST_CLEAR, FST_WRITE, FST_TABLE, FST_SYNC, FST_VERIFY, FST_STEPS };

struct FmtGeom {
    uint32_t sectors;          // the medium
    uint32_t part_start;       // 8192
    uint32_t part_len;         // sectors - 8192
    uint32_t au;               // cluster, sectors
    uint32_t cluster_bytes;
    uint32_t clusters;         // clusters of the volume (the exFAT cluster heap)
    uint32_t sys_clusters;     // bitmap + up-case table + root directory
    uint32_t est_bytes;        // what the format writes through the shim, to the byte (steps 3 to 5)
};

// Called every FMT_REPORT_BYTES of written data and at each step. written/total: bytes through the shim and the estimate. Returns non-zero to
// cancel (honoured until the partition table is written).
typedef int (*FmtProgressCb)(void *ctx, int step, uint32_t written, uint32_t total);
#define FMT_REPORT_BYTES  (256u * 1024u)

struct FmtOps {
    void *ctx;
    // The host lets go of the medium's volume. False = it could not (nothing has been written yet).
    bool (*detach)(void *ctx);
    // The host takes the medium up again. verify: it must mount as the exFAT volume `g` describes; false (and `why`) if not. Without verify it only looks again.
    bool (*attach)(void *ctx, const FmtGeom *g, bool verify, char *why, int whylen);
};

// The work buffer f_mkfs gets: a quarter of the free heap, at most 64 KiB, whole sectors; 0 = refuse (less than 4 KiB would remain).
uint32_t fmt_work_size(uint32_t free_heap);

// The volume geometry for a medium of `sectors` (what f_mkfs will choose); est_bytes is exact.
void fmt_geometry(uint32_t sectors, FmtGeom *g);

// The refusals of D8, in this order. NULL = the medium may be formatted (g filled in). `work` is fmt_work_size() of the free heap.
const char *fmt_check(BlockDevice *dev, uint32_t work, FmtGeom *g);

// The layout a finished format has to show (sector 0 and the boot sector it points to). NULL = fine, else what is wrong.
const char *fmt_verify_layout(BlockDevice *dev, const FmtGeom *g);

FmtResult fmt_exfat(BlockDevice *dev, const FmtOps *ops, uint8_t *work, uint32_t worklen, FmtProgressCb cb, void *cbctx, char *why, int whylen);

#endif
