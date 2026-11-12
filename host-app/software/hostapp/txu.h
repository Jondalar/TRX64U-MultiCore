// The MultiCore update package, .txu (T152 D3/D5): our own host and/or host app, packaged by tools/txu_pack.py (the writer; this file is the reader and
// the checks). Pure code over CorSource and the flash layer (flashfw.h), so tests/hostapp/test_txu.cc runs it against packages from the real writer and a
// fake flash. It knows nothing of Gideon's or Commodore's firmware: a .cfw/.ue2 is never a .txu (D3, S4), and installing those is T137's.
//
// Layout (little endian, tools/txu_pack.py is the same table):
//    0  "TXU1"
//    4  u32 hdr_len            80 + 48 * nparts (the CRC-32 below included)
//    8  u32 flags              0
//   12  u32 host_key           the AXSS stamp of the host part (what cor_host_key is when it runs); 0 without a host part
//   16  u32 iface_provided     the host-app interface version the host part provides; 0 without a host part
//   20  u32 iface_needed       the host-app interface version the app part needs; 0 without an app part
//   24  u32 nparts            1 or 2
//   28  char origin[8]         "trx"
//   36  char version[24]       e.g. "main-1a2b3c4d"
//   60  char commit[16]        the git commit (15 characters at most)
//   76  nparts x { u32 kind (1 host, 2 app); u32 off; u32 size; u32 flash_addr; u8 sha256[32] }
//  hdr_len-4  u32 CRC-32 of the bytes before it
// The parts follow, 4-aligned; the file ends with the last of them. host part = the .bit as written at flash 0x000000; app part = the record and its
// data as written at 0x220000. Cores are never in a .txu (Q1).
#ifndef HOST_TXU_H
#define HOST_TXU_H

#include <stdint.h>
#include "cor.h"
#include "flashfw.h"

#define TXU_HDR_FIXED    80u
#define TXU_PART_BYTES   48u
#define TXU_KIND_HOST    1u
#define TXU_KIND_APP     2u

// The host-app interface (D5): what the host gateware offers the app and what the app expects of it. The running host app is built for ONE host; the
// number moves whenever the interface between them changes in a way an old app or host cannot live with. A package's app and host must agree on it,
// and so must a package's single part with the part that stays in the flash.
#define TXU_IFACE_PROVIDED 1u     // what the host this app was built together with offers
#define TXU_IFACE_NEEDED   1u     // what this app expects of the host

typedef enum {
    TXU_OK = 0,
    TXU_ERR_IO,
    TXU_ERR_NOT_TXU,       // the magic is not TXU1
    TXU_ERR_FORMAT,        // truncated, header and parts disagree, a field out of range
    TXU_ERR_CRC,           // the header does not match its CRC-32
    TXU_ERR_ORIGIN         // the origin is not "trx"
} txu_result_t;

const char *txu_result_str(txu_result_t r);
bool txu_is_name(const char *name);                     // ends in .txu (any case)

struct TxuPart {
    uint32_t kind, off, size, addr;
    uint8_t sha[32];
};

struct TxuInfo {
    char origin[9], version[25], commit[17];
    uint32_t host_key, iface_provided, iface_needed;
    int nparts;
    TxuPart part[2];
    uint32_t file_size;
    const TxuPart *host() const { for (int i = 0; i < nparts; i++) if (part[i].kind == TXU_KIND_HOST) return &part[i]; return 0; }
    const TxuPart *app() const { for (int i = 0; i < nparts; i++) if (part[i].kind == TXU_KIND_APP) return &part[i]; return 0; }
};

// Reads and checks the header (no part bytes). *ci is filled as far as it got.
txu_result_t txu_probe(CorSource &src, TxuInfo *ci);

// What the running system is, for the checks that come before the first erase.
struct TxuRunning {
    uint32_t host_key;           // cor_host_key, 0 = the running bitstream names none
    uint32_t iface_provided;     // TXU_IFACE_PROVIDED of the running app
    uint32_t iface_needed;       // TXU_IFACE_NEEDED of the running app
};

struct TxuCheck {
    bool ok;
    char why[160];               // the reason in words when !ok
    bool host_key_changes;       // a host part whose key differs from the running one
    bool has_host, has_app;
};

// S3: does this package fit? Nothing is read or written. The app must agree with the host that is there afterwards (the package's own, else the running
// one), and a host part must agree with the app that is there afterwards.
void txu_check(const TxuInfo &ci, const TxuRunning &run, TxuCheck *out);

struct TxuStaged {
    bool ok;
    const char *why;
    char host_sha[65], app_sha[65];
    uint32_t host_axss;
    uint32_t host_len, app_len;
};

// Reads the parts into bit_dst (at least FW_BIT_SLOT bytes of room) and app_dst (FW_APP_SLOT), compares each with the SHA-256 of the header, checks the
// host part (a .bit whose header length matches the part, the IDCODE of this FPGA, the stamp = host_key) and seals the image (flashfw.h: the
// application's record is checked there). Nothing is written anywhere. `abort` (may be NULL) is polled per chunk.
bool txu_stage(CorSource &src, const TxuInfo &ci, uint8_t *bit_dst, uint8_t *app_dst, FwStageProgress cb, void *ctx, bool (*abort)(void *), TxuStaged *o,
               FwImage *img);

// The cores on the media, as far as a host update concerns them (D1): partial cores are bound to the host key, large cores to nothing of ours.
struct TxuSurvey {
    int partial;                 // layer-2 cores found
    int broken;                  // work with the running host, stop with the new key
    int already_off;             // do not work with the running host (routed for another key)
    int revived;                 // do not work now, work with the new key
    int large;                   // large cores: never affected
};
// One .cor, as cor_probe described it. new_key = the key after the update (the package's, else the running one).
void txu_survey_add(TxuSurvey *s, cor_result_t r, const CorInfo &ci, uint32_t running_key, uint32_t new_key);

#endif
