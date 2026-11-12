// The .cor core bundle, read side (T84 D5, T115 large cores). Format: tools/cor_pack.py (its docstring is the spec).
// Pure code: the bytes come through a CorSource, so the same parser runs on the host in
// tests/hostapp/test_cor.cc and on the board over FileManager files (cor_file.cc).
#ifndef HOST_COR_H
#define HOST_COR_H

#include <stdint.h>

#define COR_HOST_CONTRACT 2          // the host/core contract version this host speaks (T79.1)
#define COR_MANIFEST_MAX  2048

// COR_PART_FULL: record 5, the full bitstream of a LARGE core (manifest "kind":"large", T115 section 7); it replaces the partial.
// COR_PART_DESC, COR_PART_LIBS: records 6 and 7, the menu description and the libraries of the core (T104.1 D9, D12); either kind of core may
// carry them, a bundle without them has size 0 for both and is read as it always was.
enum { COR_PART_PARTIAL = 0, COR_PART_APP = 1, COR_PART_ICON = 2, COR_PART_FULL = 3, COR_PART_DESC = 4, COR_PART_LIBS = 5, COR_PARTS = 6 };

typedef enum {
    COR_OK = 0,
    COR_ERR_IO,        // cannot read the file
    COR_ERR_FORMAT,    // not a bundle, truncated, manifest and records disagree
    COR_ERR_CONTRACT,  // a bundle for another host contract
    COR_ERR_HASH,      // a part does not match its SHA-256
    COR_ERR_RANGE,     // application load address/size outside what the host may overwrite
    COR_ERR_HOST,      // the partial was routed against another host (T84 D5b)
    COR_ERR_LARGE      // a LARGE core (full bitstream): well formed, ci is filled, but it is not started as a layer-2 core
} cor_result_t;

const char *cor_result_str(cor_result_t r);

class CorSource {
public:
    virtual ~CorSource() { }
    virtual uint32_t size() = 0;
    virtual bool read(uint32_t off, void *buf, uint32_t len) = 0;
};

struct CorInfo {
    char name[48];
    char version[24];
    char fpga[16];
    uint32_t contract;
    uint32_t host_key;                 // the host this partial was routed against (manifest "host_key")
    uint32_t app_load, app_entry;
    uint32_t size[COR_PARTS];          // 0 = part absent
    uint32_t off[COR_PARTS];          // file offset of the part's bytes
    char sha[COR_PARTS][65];          // lower-case hex from the manifest
    uint32_t file_size;
    uint32_t hdr_digest;               // FNV-1a of the file header and the manifest, never 0 (the start check compares it, T101 S10)
    bool large;                        // manifest "kind":"large": size[COR_PART_FULL] is the bitstream, size[COR_PART_PARTIAL] is 0
    uint32_t idcode;                   // large: the FPGA the bitstream is for (manifest "idcode")
    char map[12];                      // large: "identity" or "gideon_bl4" (manifest "map")
    char id[24];                       // large: the core's id
    int origin;                        // large (T137 D1): COR_ORIGIN_*; a manifest without "origin" is ours
    uint32_t flash_bit, flash_app;     // large, a backup (D2): where its bitstream and application lay in the SPI flash
    char flash_id[40];                 // large, a backup: the flash chip they were read from (Flash::get_type_string)
};

enum { COR_ORIGIN_TRX = 0, COR_ORIGIN_FOREIGN = 1, COR_ORIGIN_BACKUP = 2 };

// The host key of the RUNNING bitstream: its AXSS word, which the host build sets from the first 8
// hex of ci/lib.sh's host_key (host/pins.xdc, T84 D5b). The application sets this once at start from
// the TRX IO block's BUILD register; 0 means the running bitstream names no host (a build without
// the register, or an older TIMESTAMP one) and then no bundle is refused for it.
extern uint32_t cor_host_key;

// Reads header, manifest and record layout (no part bytes). COR_ERR_CONTRACT and COR_ERR_HOST
// still fill ci, so the caller can say which contract or host the bundle wants. A large core (T115) returns
// COR_ERR_LARGE with ci filled and no contract or host check: the callers that start layer-2 cores must treat it as a refusal.
cor_result_t cor_probe(CorSource &src, CorInfo &ci);

// Streams part `part` into dst (size ci.size[part]) in chunks, hashing as it goes, and compares
// with the manifest. progress(done, total, ctx) is called per chunk; may be NULL.
typedef void (*cor_progress_t)(uint32_t done, uint32_t total, void *ctx);
// verify=false only reads (the caller trusts the manifest hash from an earlier check).
cor_result_t cor_read_part(CorSource &src, const CorInfo &ci, int part, uint8_t *dst,
                           cor_progress_t progress, void *ctx, bool verify = true);

// The SHA-256 of one part against the manifest, streamed (the part is not kept). COR_ERR_HASH on a mismatch.
cor_result_t cor_verify_part(CorSource &src, const CorInfo &ci, int part, cor_progress_t progress = 0, void *ctx = 0);

// Large core: the SHA-256 of the parts in its manifest (application, icon, bitstream) against the file, streamed. The check that
// runs when the entry is added. *why is set on failure (static text).
bool cor_large_verify(CorSource &src, const CorInfo &ci, const char **why, cor_progress_t progress = 0, void *ctx = 0);

// Optional clock (cycles at any rate) for the timing counters below; NULL = not measured.
extern uint32_t (*cor_clock)(void);
extern bool cor_abort;                       // set by a progress callback: cor_read_part stops after the current chunk (COR_ERR_IO)
extern uint32_t cor_t_read, cor_t_hash;     // clock units spent reading / hashing since the last reset

// Value of "key" in the flat manifest JSON: string values are copied (escapes \" \\ \/ decoded, \uXXXX
// becomes '?'), numbers as their digits. False if the key is missing or the value does not fit.
bool cor_json_get(const char *json, const char *key, char *out, int outlen);

#endif
