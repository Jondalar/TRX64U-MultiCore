// The blocks of an Amiga hard disk with one FFS (DOS\3, international) partition, as pure functions (T128 D2, D3, D10): RDB, PART, boot block,
// root, user directory, file header, extension, bitmap and bitmap-extension blocks, the checksum, the name hash, the dates. No I/O, no heap.
// Every offset below is a longword index of a 512 byte block (128 longwords, big endian); negative positions of the Amiga documentation are
// written as 128 - n. The layouts were checked against amitools (fs/block/*.py) in tests/coreapp/test_folderhd.cc's dump (spec T128 section 5).
#ifndef COREAPP_AMIGA_FFS_H
#define COREAPP_AMIGA_FFS_H

#include <stdint.h>

#define FFS_BLOCK 512
#define FFS_LONGS 128
#define FFS_HT 72                      // hash table size and data pointers in a header
#define FFS_BM_PER_ROOT 25
#define FFS_BM_PER_BLOCK 127           // longwords of bits in a bitmap block (bit 0 of long 0 = block 2)
#define FFS_BM_BLOCKS 4064             // blocks one bitmap block covers (127 * 32)
#define FFS_BM_EXT_PTRS 127
#define FFS_RESERVED 2
#define FFS_NAME_MAX 30
#define FFS_COMMENT_MAX 79
#define FFS_HEADS 16
#define FFS_SECS 63
#define FFS_CYL_BLOCKS (FFS_HEADS * FFS_SECS)        // 1008
#define FFS_DOSTYPE 0x444F5303u                      // "DOS\3"

#define FFS_T_HEADER 2
#define FFS_T_DATA 8
#define FFS_T_LIST 16
#define FFS_ST_ROOT 1
#define FFS_ST_DIR 2
#define FFS_ST_FILE 0xFFFFFFFDu                      // -3

// longword positions
enum {
    FL_TYPE = 0, FL_KEY = 1, FL_HIGHSEQ = 2, FL_HTSIZE = 3, FL_FIRSTDATA = 4, FL_CHK = 5, FL_TABLE = 6,
    FL_BM_FLAG = FFS_LONGS - 50, FL_BM_PAGES = FFS_LONGS - 49, FL_BM_EXT = FFS_LONGS - 24,
    FL_PROTECT = FFS_LONGS - 48, FL_BYTESIZE = FFS_LONGS - 47, FL_COMMENT = FFS_LONGS - 46,
    FL_DAYS = FFS_LONGS - 23, FL_MINS = FFS_LONGS - 22, FL_TICKS = FFS_LONGS - 21, FL_NAME = FFS_LONGS - 20,
    FL_DISK_DAYS = FFS_LONGS - 10, FL_CREATE_DAYS = FFS_LONGS - 7,
    FL_HASHCHAIN = FFS_LONGS - 4, FL_PARENT = FFS_LONGS - 3, FL_EXTENSION = FFS_LONGS - 2, FL_SECTYPE = FFS_LONGS - 1
};

static inline uint32_t ffs_rd(const uint8_t *b, int l)
{
    const uint8_t *p = b + l * 4;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void ffs_wr(uint8_t *b, int l, uint32_t v)
{
    uint8_t *p = b + l * 4;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

// Sum of `longs` longwords, mod 2^32.
uint32_t ffs_sum(const uint8_t *b, int longs);
// Stores the checksum so that the whole block sums to 0. The checksum longword must be 0 before.
void ffs_seal(uint8_t *b, int chk_long, int longs);
bool ffs_sum_ok(const uint8_t *b, int longs);

// The international upper case of FFS (DOS\3): a-z and the Latin-1 letters 0xE0-0xFE except 0xF7.
uint8_t ffs_upper(uint8_t c);
// Hash slot 0..71 of a name.
uint32_t ffs_hash(const uint8_t *name, int len);
bool ffs_name_eq(const uint8_t *a, int al, const uint8_t *b, int bl);

// ---- dates ----
struct AmDate {
    uint32_t days, mins, ticks;            // since 1978-01-01, minutes since midnight, 1/50 s
};
AmDate ffs_date_from_fat(uint16_t fdate, uint16_t ftime);
// FAT resolution: the 2 s step below the Amiga's tick.
void ffs_date_to_fat(const AmDate &d, uint16_t *fdate, uint16_t *ftime);
void ffs_date_civil(const AmDate &d, int *y, int *mo, int *dd, int *h, int *mi, int *s, int *hund);
bool ffs_date_from_civil(int y, int mo, int dd, int h, int mi, int s, int hund, AmDate *out);

// ---- the device ----
struct FfsGeom {
    uint32_t cylinders;                    // whole disk, cylinder 0 included
    uint32_t part_blocks;                  // (cylinders - 1) * 1008
};
void ffs_build_rdb(uint8_t *b, uint32_t cylinders);                  // block 0
void ffs_build_part(uint8_t *b, uint32_t cylinders, const char *drive_name);   // block 1
void ffs_build_boot(uint8_t *b, uint32_t root);                      // 1024 bytes (blocks 0 and 1 of the partition)

// ---- the objects. A header is described by this; name and comment are bytes, not C strings ----
struct FfsObj {
    const uint8_t *name; int nlen;
    const uint8_t *comment; int clen;
    uint32_t protect;
    AmDate date;
    uint32_t size;                         // bytes (files)
    uint32_t parent;                       // block
    uint32_t hash_next;                    // block of the next entry in the hash chain, 0 none
};

// A file header whose data blocks are the consecutive blocks first_data .. first_data + ndata - 1 (what the synthesis lays out).
void ffs_build_file(uint8_t *b, uint32_t own, const FfsObj &o, uint32_t first_data, uint32_t ndata, uint32_t ext);
// Extension block `e` of such a file (e >= 0): covers data blocks 72 * (e + 1) ...; ext is the next extension block or 0.
void ffs_build_ext(uint8_t *b, uint32_t own, uint32_t header, uint32_t first_data, uint32_t ndata, uint32_t e, uint32_t ext);
void ffs_build_dir(uint8_t *b, uint32_t own, const FfsObj &o, const uint32_t heads[FFS_HT]);
void ffs_build_root(uint8_t *b, const char *name, const AmDate &now, const uint32_t heads[FFS_HT], uint32_t bm_valid,
                    const uint32_t *bm_pages, int nbm_pages, uint32_t bm_ext);
// A bitmap block of `bits` (127 longwords, bit set = free, already in host order per longword); sealed.
void ffs_build_bitmap(uint8_t *b, const uint32_t *longs);
void ffs_build_bm_ext(uint8_t *b, const uint32_t *ptrs, int n, uint32_t next);

// ---- reading what the Amiga wrote (the fold-back walks these) ----
struct FfsHdr {
    uint32_t type, sectype, key, highseq, first_data, protect, size, parent, chain, ext;
    uint8_t name[FFS_NAME_MAX + 1]; int nlen;
    uint8_t comment[FFS_COMMENT_MAX + 1]; int clen;
    AmDate date;
};
// false when the block is not a header (type, checksum)
bool ffs_parse_hdr(const uint8_t *b, FfsHdr *h);
static inline uint32_t ffs_data_ptr(const uint8_t *b, int i) { return ffs_rd(b, FL_TABLE + FFS_HT - 1 - i); }   // data block i of the 72 in a header or extension
static inline uint32_t ffs_hash_slot(const uint8_t *b, int slot) { return ffs_rd(b, FL_TABLE + slot); }

#endif
