// A folder on the stick/SD as an Amiga hard disk (T128). The folder is turned into a block device on the fly: an RDB, one FFS (DOS\3) partition
// whose every block is computed from an index of the folder when the Amiga asks for it, and a write path that sends what the Amiga writes
// back into real files. The sector service (sectors.cc) sees an ordinary CoreFile, the gateware an ordinary hard disk.
//
//   read     RDB + PART (cylinder 0), then the partition: boot block, root, bitmap, directories, file headers and extension blocks computed from the
//            index (amiga_ffs.cc), data blocks read from the host files through a small handle table (8 files, a read-ahead each).
//   write    a full data block of an unchanged, still linked file goes straight into the host file; everything else the Amiga writes (headers,
//            directories, bitmap, new data) is kept in an overlay (heap for metadata, a scratch file for data) and read back from it.
//   fold-back  at idle, at hold and at eject the overlay is walked exactly as the Amiga would walk the disk and the host tree is brought to it:
//            files created, rewritten (temp file + rename), renamed, deleted, directories made and removed, in slices of at most FH_SLICE_MS.
// Names follow FS-UAE (host-invalid bytes as %xx, protection bits and comments in <file>.uaem). The layout is frozen for the life of the mount.
#ifndef COREAPP_FOLDERHD_H
#define COREAPP_FOLDERHD_H

#include "fsif.h"
#include "io.h"
#include "amiga_ffs.h"

// ---- the caps (T128 D9); every allocation of the feature is counted against them ----
#define FH_ENTRIES_MAX 16384              // objects in the folder tree at mount
#define FH_INDEX_BYTES (1u << 20)         // nodes + name pool at mount
#define FH_POOL_EXTRA (128u * 1024)       // room for the names the Amiga gives during one mount (new objects, renames)
#define FH_CREATED_MAX 4096               // objects the Amiga creates during one mount
#define FH_OVERLAY_BLOCKS 1024            // metadata blocks kept in the heap
#define FH_SRUNS_MAX 8192                 // runs of blocks in the scratch file
#define FH_XRUNS_MAX 16384                // runs of folded-back data
#define FH_HANDLES 8
#define FH_RA_BYTES 4096                  // read-ahead per handle
#define FH_CHUNK 16384                    // the working buffer of the fold-back
#define FH_SLICE_MS 20                    // the longest the fold-back holds the poll (Q9)
#define FH_RESERVE_BYTES (16u << 20)      // medium space that is never promised to the Amiga (D6)
#define FH_MAX_CYL 65535
#define FH_STRAIGHT_WRITES 0              // 1: data writes of unchanged files go straight to the host file (unsafe, see folderhd.cc; only the bench's control sets it)                  // the gateware keeps 16 bits

namespace fh { struct Impl; }

struct FhStats {
    uint32_t nodes, pool_bytes, index_bytes;       // the index as built at mount
    uint32_t skipped;                              // objects left out (see the report)
    uint32_t cylinders, part_blocks, used_blocks, root_block;
    uint32_t overlay_blocks, scratch_runs, xruns;  // now
    uint32_t straight_writes, delta_writes, ro_writes, dropped_writes;
    uint32_t foldbacks, fold_errors, aborted;
    uint32_t handles_open, handle_peak, handle_opens, ra_hits, ra_reads;
    uint32_t created, deleted, renamed, rewritten, patched, mkdirs, rmdirs;
    uint32_t slice_max_ms;
};

class FolderDisk : public CoreFile {
public:
    struct Cfg {
        CoreFs *fs;
        CoreIo *io;                       // the clock
        int unit;
        const char *folder;               // "/usb0/amiga/hd"
        uint64_t want_bytes;              // the partition size the person set (bytes); rounded down to whole cylinders
        uint32_t max_cylinders;           // what the core can address, cylinder 0 included (<= FH_MAX_CYL)
    };
    // The folder as a disk, or null with the reason in msg (nothing is mounted then).
    static FolderDisk *mount(const Cfg &cfg, char *msg, int msglen);
    ~FolderDisk();

    // CoreFile
    uint64_t size();
    bool writable();                      // true: the Amiga's writes are accepted (on a read only medium they stay in the overlay, D8)
    bool read(uint64_t off, void *buf, uint32_t len);
    bool write(uint64_t off, const void *buf, uint32_t len);
    bool sync();                          // everything is on the medium when it returns (eject, hold)

    // The idle work: one slice of the fold-back, at most FH_SLICE_MS. True while there is more.
    bool idle_slice();
    bool host_writable() const { return host_rw_; }
    bool dirty() const;
    const FhStats &stats() const { return st_; }
    // The lines of the mount report (what was left out and why), for the console.
    const char *report() const { return report_; }

    // ---- inside; public so the host benches can reach the numbers ----
    fh::Impl *p;
private:
    FolderDisk();
    bool host_rw_;
    FhStats st_;
    char report_[512];
};

// The most the menu can offer for a folder: the core's reach and what the medium behind the folder can back (free space less the reserve, plus the
// tree that is on it already). 0 when nothing can be offered.
// at_least: a size the caller wants to know is backed; when the free space alone backs it the tree is not looked at (it would only add to the answer).
uint64_t folderhd_offer_bytes(CoreFs *fs, const char *folder, uint32_t max_cylinders, uint64_t at_least = 0);
// The partition size the person asked for, as bytes: "2", "0.5", "1.25" (GB, two decimals); empty is `def`. false: not a number.
bool folderhd_parse_gb(const char *text, uint64_t def, uint64_t *out);
// The memory the feature holds right now / at most (all allocations are counted), for the benches.
uint32_t folderhd_heap_now(void);
uint32_t folderhd_heap_peak(void);
void folderhd_heap_reset_peak(void);

// The firmware's printf knows no precision and no length modifier (tests/coreapp/boardprintf.h): bytes are copied to a string first, sizes are printed as unsigned MB.
static inline void folderhd_cpy(char *dst, int n, const void *src, int len)
{
    if (len > n - 1) len = n - 1;
    if (len < 0) len = 0;
    for (int i = 0; i < len; i++) dst[i] = ((const char *)src)[i];
    dst[len] = 0;
}

// FS-UAE's name rule (fsdb_host.cpp aname_to_nname / nname_to_aname), exposed for the benches.
int folderhd_aname_to_host(const uint8_t *an, int alen, char *out, int outlen);       // Latin-1 Amiga name -> UTF-8 host name with %xx; length or -1
int folderhd_host_to_aname(const char *host, uint8_t *out, int outmax);               // UTF-8 host name -> Latin-1, %xx decoded; length or -1 (not Latin-1)

#endif
