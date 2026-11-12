// The inside of FolderDisk (T128), shared by folderhd.cc (index, synthesis, read, write) and folderhd_fold.cc (the fold-back).
#ifndef COREAPP_FOLDERHD_IMPL_H
#define COREAPP_FOLDERHD_IMPL_H

#include "folderhd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fh {

#define FH_SEEN_BYTES ((FH_ENTRIES_MAX + FH_CREATED_MAX + 16) / 8 + 8)

// ---- counted memory (T128 D9): every block the feature holds goes through these ----
void *mem_alloc(size_t n);
void *mem_realloc(void *p, size_t old_n, size_t n);
void mem_free(void *p, size_t n);

enum { F_DIR = 1, F_DEAD = 2, F_NEW = 4, F_CREATED = 8, F_ERR = 16, F_DELTA = 32 };
#define FH_NONE16 0xFFFFu

// One object of the folder. 44 bytes. name/comment/hname are offsets into the pool (a length byte, then the bytes; 0 = empty / none).
// The fields name .. protect describe the object as the Amiga knows it: the baseline the synthesis uses and the fold-back compares with.
struct Node {
    uint32_t hdr;               // header block (partition relative)
    uint32_t size;              // files: bytes as the synthesis laid them out at mount (never changes: the spans depend on it)
    uint32_t hsize;             // files: bytes the host file has now
    uint32_t first;             // directories: index of the first child (the children of a directory are consecutive at mount)
    uint32_t name;              // the Amiga name (Latin-1)
    uint32_t hname;             // the host name when it is not the FS-UAE encoding of the Amiga name, else 0
    uint32_t comment;
    uint16_t parent;            // Amiga parent (index)
    uint16_t hparent;           // host parent (index); differs from `parent` only while a fold-back moves it
    uint16_t next;              // next entry in the hash chain (index) or FH_NONE16
    uint16_t nchild;
    uint16_t days, mins, ticks; // the Amiga date
    uint8_t protect;            // low byte of the Amiga protection long
    uint8_t flags;
};

struct OvBlk { uint32_t blk; uint8_t *data; };                       // a metadata block in the heap
struct SRun { uint32_t start, count, slot; };                        // blocks start .. start+count-1 live at slots slot .. in the scratch file
struct XRun { uint32_t start, count, fblk; uint32_t node; };         // blocks start .. are the data blocks fblk .. of the file `node`

struct Handle {
    int32_t node;               // -1: free
    CoreFile *f;
    uint32_t lru;
    bool rw, wrote;
    uint64_t ra_off, next_off;  // next_off: where a read that goes on would start
    uint32_t ra_len;
    uint8_t *ra;
};

enum Phase { PH_IDLE = 0, PH_WALK, PH_MKDIR, PH_MATERIAL, PH_REN1, PH_DELETE, PH_REN2, PH_PLACE, PH_PATCH, PH_PROPS, PH_FINISH };
#define FH_LIMBO 0xFFFEu             // hparent of a node that waits in <root>/trx64 between two renames

struct Pend {                   // a change the walk found, applied by the commit phases
    uint32_t node;              // the node (an existing one, or a new one appended by the walk)
    uint32_t aname;             // the Amiga's name now (pool)
    uint32_t acomment;
    uint32_t size;
    uint32_t tmp_seq;           // the temp file the data was collected in
    uint16_t aparent;           // the Amiga's parent now
    uint16_t days, mins, ticks;
    uint8_t protect;
    uint16_t ops;
};
enum { OP_REN = 1, OP_PROPS = 2, OP_CONTENT = 4, OP_NEW = 8, OP_DEL = 16, OP_TMP = 32, OP_HOP = 64, OP_FAIL = 128, OP_PATCH = 256 };

struct Impl {
    FolderDisk *self;
    CoreFs *fs;
    CoreIo *io;
    int unit;
    FhStats *st;
    bool host_rw;
    char base[200];                     // the mounted folder
    char root[48];                      // the medium's root ("/usb0")
    char tmpdir[64];                    // <root>/trx64
    char scratch_path[96];
    char volname[32];
    uint32_t kept_backups;              // folderhd<unit>.bk* files found at the mount

    // geometry
    uint32_t cyls, nblk, R, nbm, nbmext, K;
    uint64_t size_bytes;
    AmDate vol_date;

    // the index
    Node *nodes; uint32_t nn, ncap, nbase;
    uint8_t *pool; uint32_t plen, pcap, pool_limit;
    bool pool_bad;                      // a name did not fit the pool (the room the Amiga's new names may take is fixed, FH_POOL_EXTRA)
    uint16_t *cre; uint32_t ncre, cre_cap;       // created nodes ordered by header block
    uint8_t *seen;                      // one bit per node, valid during a walk

    // the Amiga's changes
    OvBlk *ov; uint32_t nov, ov_cap;
    SRun *sr; uint32_t nsr, sr_cap;
    CoreFile *scratch; uint32_t sslots; uint32_t smeta;    // smeta: metadata blocks that overflowed into the scratch
    XRun *xr; uint32_t nxr, xr_cap;
    uint32_t gen;                       // changes with every block the Amiga wrote into the overlay
    uint32_t meta_gen;                  // ... of metadata
    bool ov_full_said, sr_full_said;

    // handles
    Handle hd[FH_HANDLES];
    uint32_t lru_clock;

    // small caches
    uint32_t memo_blk; uint8_t memo[FFS_BLOCK];
    struct Stab { int32_t node; uint32_t gen; bool ok; } stab[16];

    // the fold-back
    int phase;
    uint32_t folded_gen;                // overlay generation the host tree is known to hold
    uint32_t job_gen, cursor, sub, retry_ms, bm_wait_ms;
    bool consistent;                    // the Amiga's bitmap flag was valid when the run began
    bool bm_waiting;                    // the Amiga's bitmap flag was invalid when the run wanted to start (since bm_wait_ms)
    bool late;                          // the Amiga wrote after the commit started
    bool fold_failed;                   // an item of the commit failed: nothing is dropped, the next run tries again
    bool force;                         // sync(): do not wait for the Amiga's bitmap to be valid
    uint32_t nn_start, plen_start;
    Pend *pend; uint32_t npend, pend_cap;
    bool walk_full;
    bool step_err;                      // a step of the commit could not do what the medium refused
    uint8_t *buf;                       // the working buffer (FH_CHUNK)
    CoreFile *tmp;                      // the temp file being written
    uint32_t tmp_seq;
    uint64_t tmp_off, tmp_left;
    uint32_t it_j, it_n, it_ext, it_hops;
    uint8_t it_blk[FFS_BLOCK];
    uint32_t visited, mv_seq;
    struct Frame { uint32_t block; uint16_t node; uint16_t slot; uint32_t chain; };
    Frame stack[40];
    int sp;
    struct Repair { uint32_t node, seq, size; } repair[8];
    uint32_t nrepair;
    uint32_t step_ms_max;               // the longest single step of the current slice

    // ---- index ----
    uint32_t pool_add(const uint8_t *s, int n);
    const uint8_t *pstr(uint32_t off) const { return pool + off; }
    bool node_add(Node **out, uint32_t *idx);
    uint32_t index_bytes() const { return nn * (uint32_t)sizeof(Node) + plen; }
    void host_name(uint32_t idx, char *out, int n);
    bool path_of(uint32_t idx, char *out, int n, bool host_parent = true);
    bool scan(char *msg, int msglen, char *report, int replen);
    bool layout(const FolderDisk::Cfg &cfg, char *msg, int msglen);

    // ---- synthesis ----
    int span_find(uint32_t b);
    void synth(uint32_t b, uint8_t *out, bool *known);
    void synth_root_cluster(uint32_t b, uint8_t *out);
    void synth_node(uint32_t i, uint32_t off, uint8_t *out);
    void obj_of(uint32_t i, FfsObj *o);
    void dir_heads(uint32_t i, uint32_t heads[FFS_HT]);
    void bitmap_block(uint32_t k, uint8_t *out);
    bool view_read(uint32_t b, uint8_t *out);               // what the Amiga sees: overlay, scratch, folded data, the synthesis
    bool data_read(uint32_t i, uint32_t fblk, uint8_t *out); // block fblk of the host file of node i, zero filled
    uint32_t file_blocks(const Node &n) const { return (n.size + FFS_BLOCK - 1) / FFS_BLOCK; }
    uint32_t file_ext(const Node &n) const { uint32_t d = file_blocks(n); return d > FFS_HT ? (d - FFS_HT + FFS_HT - 1) / FFS_HT : 0; }
    uint32_t span_len(const Node &n) const { return (n.flags & F_DIR) ? 1 : 1 + file_blocks(n) + file_ext(n); }

    // ---- the Amiga's changes ----
    int ov_find(uint32_t b);
    bool ov_put(uint32_t b, const uint8_t *data);
    bool sr_find(uint32_t b, uint32_t *slot);
    bool sr_put(uint32_t b, const uint8_t *data);
    void sr_remove_range(uint32_t start, uint32_t count);
    bool delta_has(uint32_t b);
    bool delta_read(uint32_t b, uint8_t *out);
    bool scratch_open();
    void scratch_reset();
    int xr_find(uint32_t b);
    bool xr_insert(uint32_t start, uint32_t count, uint32_t fblk, uint32_t node);
    int created_find(uint32_t hdr);
    int node_by_hdr(uint32_t hdr);
    bool write_block(uint32_t b, const uint8_t *buf);
#if FH_STRAIGHT_WRITES
    bool stable(uint32_t i);
    bool reachable(uint32_t i);
    bool straight(uint32_t i, uint32_t fblk, const uint8_t *buf);
#endif
    bool ro_put(uint32_t b, const uint8_t *buf);

    // ---- handles ----
    Handle *hopen(uint32_t node, bool rw);
    void hclose(Handle *h);
    void hclose_all();
    bool hsync_all();
    bool hread(Handle *h, uint64_t off, uint8_t *buf, uint32_t n);

    // ---- the fold-back (folderhd_fold.cc) ----
    bool dirty() const;
    bool fold_step();                   // one unit of work; true while there is more
    void fold_begin();
    void fold_abort(const char *why);
    void fold_finish();
    bool walk_step();
    void walk_end();
    bool visit(uint32_t blk, const uint8_t *b, Frame &f);
    bool it_start(uint32_t hdr_block);
    bool it_next(uint32_t *blk);
    int content_changed(uint32_t idx, const FfsHdr &h);      // 0 same, 1 only data blocks in the overlay (patched in place), 2 another file content
    bool step_patch();
    bool seen_get(uint32_t i) const { return (seen[i >> 3] >> (i & 7)) & 1; }
    void seen_set(uint32_t i) { seen[i >> 3] |= (uint8_t)(1u << (i & 7)); }
    bool pend_add(const Pend &p);
    bool step_mkdir();
    bool step_material();
    bool step_delete();
    bool step_rename(int pass);
    bool step_place();
    bool step_props();
    bool write_sidecar(uint32_t idx, const Pend &p);
    bool sidecar_path(uint32_t idx, char *out, int n);
    void tmp_path(uint32_t seq, char *out, int n);
    void bak_path(uint32_t seq, char *out, int n);
    bool host_absent(const char *path);
    bool run_repair();
    void pend_tmp(const Pend &p, char *out, int n);
    void tmp_drop_all();
    void node_dead(uint32_t idx);
    void fail(const char *what, uint32_t idx);
};

}   // namespace fh

#endif
