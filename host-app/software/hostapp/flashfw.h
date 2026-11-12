// The firmware flash layer (T137 D4; the MultiCore update of T152 builds on it, T152 D2): check before the first erase, the write, the read-back.
// Pure code over the Ultimate application's `Flash` class. The layer does not decide WHO may be written (fwrestore.cc does for a restore); it
// guarantees the order: an image exists only after fw_image_seal() was handed a package whose checks passed, and fw_write() refuses an unsealed
// image before it touches the chip. docs/specs/T137-host-convert-and-restore.md is the description; the code is the check.
#ifndef HOST_FLASHFW_H
#define HOST_FLASHFW_H

#include <stdint.h>
#include "cor.h"
#include "sha256.h"

class Flash;

// ---- the flash layout of the 50T board (flash.h flash_addresses_u64ii_50t, T49)
#define FW_BIT_ADDR        0x000000u
#define FW_BIT_SLOT        0x220000u      // the bitstream slot; the application starts right behind it
#define FW_APP_ADDR        0x220000u
#define FW_APP_SLOT        0x1E0000u      // the application slot, 12-byte record included
#define FW_REC_HDR         12u
#define FW_SECTOR          4096u
#define FW_PAGE            256u
// the DDR2 staging of the checked package (outside the host's image, the core windows and the trampoline; coreboot.h): the application where a
// core's application waits, the bitstream behind it. A firmware write never runs together with a core start.
#define FW_STAGE_APP       0x03000000u
#define FW_STAGE_BIT       0x03400000u

// ---- checksums and the boot ROM's stamp walk
uint32_t fw_crc32(const void *data, uint32_t len);                        // zlib CRC-32
uint32_t fw_crc32_update(uint32_t crc, const void *data, uint32_t len);   // streaming: start with 0
uint32_t fw_chk(uint32_t c, const void *data, uint32_t len);              // Gideon's calc_checksum, start with 0
// The AXSS stamp (USR_ACCESS write) the boot ROM's walk finds in a bitstream (tools/trx_pkg.py axss); false when there is none.
bool fw_axss(const uint8_t *bit, uint32_t len, uint32_t *value);

// ---- reading a part into the staging buffer while it is hashed
typedef void (*FwStageProgress)(void *ctx, uint32_t done, uint32_t total);
struct FwHash {
    uint32_t crc, chk;
    sha256_t sha;
    uint32_t seen, skip;
};
void fw_hash_begin(FwHash *h, uint32_t skip);                 // skip: leading bytes that are not hashed (a raw image's header)
void fw_hash_feed(FwHash *h, const uint8_t *d, uint32_t n);
void fw_hash_sha_hex(FwHash *h, char out[65]);
// Reads `have` bytes from src at off into dst (n bytes; the rest of n is a zero pad) in chunks, feeding h; polls abort(ctx) per chunk.
// *done_all counts across calls for the progress callback. false: unreadable or aborted.
bool fw_read_part(CorSource &src, uint32_t off, uint8_t *dst, uint32_t n, uint32_t have, FwHash &h, FwStageProgress cb, void *ctx,
                  uint32_t *done_all, uint32_t total_all, bool (*abort)(void *));

// ---- the image
struct FwImage {
    const uint8_t *bit;               // written at FW_BIT_ADDR
    uint32_t bit_len;
    const uint8_t *app;               // written at FW_APP_ADDR: record + data, padded to 4
    uint32_t app_len;
    uint32_t axss;                    // the stamp the bitstream must show in the flash, 0 = none demanded
    uint32_t sealed;                  // FW_SEALED once fw_image_seal() accepted it
};
#define FW_SEALED 0x53454C44u
// The one gate to the write: the caller's checks passed (`checked`), the parts fit their slots and the application record is one the boot ROM
// accepts. Anything else leaves *img unsealed and names the reason. Nothing here touches the flash.
bool fw_image_seal(FwImage *img, bool checked, const uint8_t *bit, uint32_t bit_len, const uint8_t *app, uint32_t app_len, uint32_t axss, const char **why);
// The same gate for an image of ONE part (T152 D5: a .txu may carry only the host app, or only the host): the absent part is NULL / 0. At least one
// part must be present. Such an image goes to fw_write_parts(); fw_write() refuses it (it writes both regions).
bool fw_image_seal_parts(FwImage *img, bool checked, const uint8_t *bit, uint32_t bit_len, const uint8_t *app, uint32_t app_len, uint32_t axss, const char **why);

// The chip the host may write: the type string of the Ultimate application's flash classes. HOSTAPP_EMU_FLASH adds the emulator's chip.
bool fw_chip_ok(const char *type);
// Chip and geometry: 256-byte pages, 4 KiB sectors, erase needed. *type is the chip's name for the page.
bool fw_flash_check(Flash *fl, const char **type, const char **why);
// Whole image against the flash (both regions byte by byte); true when the flash holds exactly it already.
bool fw_flash_equals(Flash *fl, const FwImage &img);
// One region of the image against the flash: the bitstream bytes [0, bit_len) / the application record and data. false for a part the image lacks.
bool fw_flash_equals_bit(Flash *fl, const FwImage &img);
bool fw_flash_equals_app(Flash *fl, const FwImage &img);

enum { FWP_APP = 0, FWP_BIT, FWP_READBACK, FWP_PHASES };
// done/total in sectors (FWP_APP, FWP_BIT) or pages (FWP_READBACK); detail: "erasing" / "programming" / "checking" / "tail".
typedef void (*FwProgress)(void *ctx, int phase, uint32_t done, uint32_t total, const char *detail);

struct FwWriteResult {
    bool ok;
    int phase;                        // where it stopped
    uint32_t sector;                  // the flash address of the sector or page that failed
    const char *why;
    uint32_t erases, programs;        // counted, for the page
    bool started;                     // the first erase was issued: the flash is changed
};
// The write: application first with its first sector erased first and programmed last (descending pages), then the bitstream front to
// back and the rest of its slot erased, every page read back (3 tries), then both regions read back whole and the AXSS stamp walked in the flash.
// Idempotent: a repeat after a failure starts again from the staged image. No cancel exists: the callback cannot stop it. An unsealed image, a chip
// that is not ok or a part that does not fit is refused with started = false, before any erase.
bool fw_write(Flash *fl, const FwImage &img, FwProgress cb, void *ctx, FwWriteResult *res);
// T152: the same write for the parts named in `parts` (FW_PART_*), each of which the image must hold. A part not named is not erased, not
// programmed and not read back as a whole; the region of an unnamed part is never touched (a bench counts erases and programs per region).
#define FW_PART_BIT 1u
#define FW_PART_APP 2u
bool fw_write_parts(Flash *fl, const FwImage &img, uint32_t parts, FwProgress cb, void *ctx, FwWriteResult *res);

#endif
