// The .cfw firmware image, read side (T115 D1, D3). A .cfw carries a complete firmware: a pre-header, an updater
// image with one or more Xilinx .bit files (one per FPGA size) and, after the LAST .bit, one application record
// {load, length, start} followed by the application. The host starts such a firmware by loading the .bit for the
// FPGA it runs on through the ICAP (IPROG) and handing the application over in DDR2.
// Pure code: the bytes come through a CorSource, so the same parser runs in tests/hostapp/test_cfw.cc and on the
// board over a file. docs/specs/T115-full-bitstream-cores.md is the description; the code is the check.
#ifndef HOST_CFW_H
#define HOST_CFW_H

#include <stdint.h>
#include "cor.h"

#define CFW_APP_ADDR      0x00030000u   // load and start the boot ROM of the full firmware expects (D1, D4)
#define CFW_UPDATER_ADDR  0x03000000u   // where the vendor's updater image (the record in front of a .ue2) loads and starts
#define CFW_APP_MAX       0x00400000u   // the largest application the staging area takes
#define CFW_BIT_MAX       0x00800000u   // the largest .bit file the staging area takes
#define CFW_SCAN_MAX      0x00400000u   // the first .bit is looked for in this many bytes from the start
// The IDCODE of the FPGA this host runs on. The host app is a 50T program (one XC7A50T board, ADR-0015) and no
// register of the layer-1 loader reads the IDCODE back, so it is a constant; parts are compared without the
// 4-bit version in the top nibble.
#define CFW_IDCODE_XC7A50T 0x0362C093u
#define CFW_IDCODE_MASK    0x0FFFFFFFu
#define CFW_BOOT_JUMP_ADDR  0x0000FFF8u // the boot ROM jumps to the word here ...
#define CFW_BOOT_MAGIC_ADDR 0x0000FFFCu // ... when the word here holds CFW_BOOT_MAGIC
#define CFW_BOOT_MAGIC      0x1571BABEu
#define CFW_IPROG_LEN     36

typedef enum {
    CFW_OK = 0,
    CFW_ERR_IO,         // cannot read the file
    CFW_ERR_FORMAT,     // not a .cfw (pre-header)
    CFW_ERR_TRUNCATED,  // a .bit or the application reaches past the end of the file
    CFW_ERR_NO_BIT,     // no .bit file in the image
    CFW_ERR_NO_50T,     // .bit files, but none for this FPGA (by the IDCODE inside, not by the text)
    CFW_ERR_NO_APP,     // no application record after the last .bit
    CFW_ERR_LOAD,       // the application record does not load and start at 0x30000
    CFW_ERR_RANGE,      // a .bit or the application is larger than the staging areas
    CFW_ERR_HASH        // a large .cor: a part does not match the SHA-256 of its manifest
} cfw_result_t;

const char *cfw_result_str(cfw_result_t r);

// The DDR2 address map the application is staged under (D3).
enum { CFW_MAP_GIDEON_BL4 = 0, CFW_MAP_IDENTITY = 1 };

struct CfwInfo {
    char name[48];          // "<design> <date> <time>" from the .bit header's fields a, c and d
    char part[16];          // the .bit header's field b (a name only; the IDCODE decides)
    char id[24];            // a large .cor: the manifest's core id (core.toml id): the T135 scratch file's stamp, and what the hand-over carries for the shared-ID pick (T115 7); empty for a .cfw
    uint32_t idcode;        // of the chosen .bit
    int map;                // CFW_MAP_*: identity when the .bit writes the AXSS register, else gideon_bl4
    uint32_t bit_off, bit_len;   // the chosen .bit file inside the .cfw, header included
    uint32_t app_off;       // offset of the application record; its bytes follow after 12 (a large .cor has no record: app_data == app_off)
    uint32_t app_data;      // offset of the application's bytes
    uint32_t app_len, app_load, app_entry;
    uint32_t file_size;
    bool from_cor;          // a large .cor (T115 section 7): name and part are the manifest's, the SHA-256 values below are set
    char bit_sha[65], app_sha[65];
};

// True for a file name ending in .cfw (any case).
bool cfw_is_name(const char *name);

// Reads and checks the image. running_idcode: the IDCODE of the FPGA the host runs on. A file that starts with the .cor magic is read
// as a large core: the manifest, its IDCODE against running_idcode AND against the IDCODE inside the bitstream, the map against the AXSS
// write inside it; the bitstream is the record as it lies in the file (bit_off, bit_len), the application its raw image. A layer-2 .cor
// is CFW_ERR_FORMAT here. No hashes are computed (cor_large_verify does that when the entry is added).
cfw_result_t cfw_probe(CorSource &src, uint32_t running_idcode, CfwInfo &ci);

// The IDCODE the configuration packets of the bitstream (`len` bytes at `off`) write; false when there is none (T152: the .txu check).
bool cfw_bit_idcode(CorSource &src, uint32_t off, uint32_t len, uint32_t *idcode);

// Where the byte at application offset `off` is staged, for a map (D3): stage[cfw_map_off(k, off)] = app[off].
// Only address bits 6:3 move, so every 128-byte block stays where it is.
uint32_t cfw_map_off(int kind, uint32_t off);
uint32_t cfw_unmap_off(int kind, uint32_t off);

// Copies n bytes of the application, which start at application offset `off`, into the staging area `dst` (128-byte
// aligned, holding the whole image rounded up to 128) under map `kind`. off and n are multiples of 128, src and dst
// are 4-byte aligned; pad the last chunk with zeros to 128.
void cfw_stage_copy(int kind, void *dst, uint32_t off, const void *src, uint32_t n);

// The hand-over page of a legacy start (T112 Q1), staged under the application's map: `page` (HANDOVER_PAGE_BYTES, 4-byte aligned) is what
// gets copied to HANDOVER_ADDR. A large .cor (ci.from_cor) carries its path as the host opened it, the manifest's name and its id (the application
// picks among descriptors that share an ID word by the id first, the name second, start.cc); the version stays empty, the host key and the
// video word are 0. A .cfw gets a wiped page, so a
// block of an earlier start cannot be taken for its own.
#define CFW_HANDOVER_BYTES 4096u
// T191 S9: `ymdhms` (year, month, day, hour, minute, second; 0 = the host has no valid clock) goes into the block of a .cor start, so a
// large core's application (the A1200) gets the board's time as a layer-2 core does through coreboot.cc.
void cfw_handover_page(const CfwInfo &ci, const char *path, uint8_t *page, const int *ymdhms = 0);

// The bytes the ICAP gets for IPROG with WBSTAR = faddr, in bitstream (file) order (D4).
void cfw_iprog_seq(uint32_t faddr, uint8_t out[CFW_IPROG_LEN]);

#endif
