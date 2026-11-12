// The .u2a file, format version 3 (T104.1 D10, D11): the image of a library or an application, position independent, with its
// relocation table, its dependencies and its residence. The writer is tools/u2a_pack.py; the reader is libload.cc. All values are
// little-endian; every offset is a file offset and a multiple of 4.
//
//   header (104) | needs (20 each) | payload (the image, linked at 0) | relocation table (u32 each) | manifest (JSON, apps)
//
// The image is linked at address 0, so an offset into the image is also its link-time address. The relocation table lists the offset of
// every 32-bit word that holds such an address. The loader adds the address it placed the image at to each of those words. Nothing else
// in the image changes: code is pc-relative (-fPIE -mcmodel=medany -mno-relax). A word an R_RISCV_RELATIVE entry describes is
// prefilled with its addend by the packer, so "word += base" is the whole operation.
#ifndef COREAPP_LIBFMT_H
#define COREAPP_LIBFMT_H

#include <stdint.h>

#define U2A3_MAGIC 0x50413255U          /* "U2AP", as in format 2 */
#define U2A3_FORMAT_VERSION 3
#define U2A3_HEADER_SIZE 104
#define U2A3_NEED_SIZE 20
#define U2A3_NAME_MAX 16                /* a name and its NUL */
#define U2A3_NEEDS_MAX 4
#define U2A3_NONE 0xFFFFFFFFU           /* an entry offset the file does not have */

#define U2A3_ARCH_RISCV32 2             /* as U2A_ARCH_RISCV32 of format 2 */
#define U2A3_KIND_LIB 1
#define U2A3_KIND_APP 2
#define U2A3_RES_TRANSIENT 0            /* loaded when opened, freed when it returns (today's .u2a) */
#define U2A3_RES_RESIDENT 1             /* stays; its contributions and poll live on (D10.3) */

// The application binary interface the base offers, as major.minor. A file carries the majors it knows (abi_min .. abi_max); the
// minor is a dependency like any other: `needs base >= 3.1` (D11). Version numbers are major << 16 | minor.
#define U2A_ABI_MAJOR 3
#define U2A_BASE_MINOR 0
#define U2A3_VERSION(major, minor) ((uint32_t)(((major) << 16) | (minor)))

// Limits the loader enforces before it allocates anything.
#define U2A3_IMAGE_MAX 0x00100000U      /* payload + bss, 1 MiB */
#define U2A3_ALIGN_MAX 4096U

struct U2a3Need {
    char name[U2A3_NAME_MAX];           /* "base", or the name another library provides */
    uint32_t min_version;               /* major << 16 | minor: the one provided has to be at least this */
};

struct U2a3Header {
    uint32_t magic;                     /*   0 */
    uint16_t header_size;               /*   4  U2A3_HEADER_SIZE */
    uint16_t format_version;            /*   6  U2A3_FORMAT_VERSION */
    uint16_t abi_min;                   /*   8  lowest ABI major the file knows */
    uint16_t abi_max;                   /*  10  highest */
    uint8_t kind;                       /*  12  U2A3_KIND_* */
    uint8_t residence;                  /*  13  U2A3_RES_* */
    uint8_t arch;                       /*  14  U2A3_ARCH_* */
    uint8_t needs_count;                /*  15  at most U2A3_NEEDS_MAX */
    uint32_t flags;                     /*  16  0 */
    char name[U2A3_NAME_MAX];           /*  20  what it provides (a library); the contributor name of its registrations */
    uint32_t version;                   /*  36  what it provides, major << 16 | minor */
    char capability[U2A3_NAME_MAX];     /*  40  the core has to have it ("drive_a"); empty: none needed (Q10) */
    uint32_t payload_offset;            /*  56  the image as it is in the file */
    uint32_t payload_size;              /*  60 */
    uint32_t bss_size;                  /*  64  zeroed behind the payload */
    uint32_t align;                     /*  68  power of two, 4 .. U2A3_ALIGN_MAX */
    uint32_t register_offset;           /*  72  lib_register(api, out_record), image offset; U2A3_NONE: none */
    uint32_t main_offset;               /*  76  app_main(api, ctx), image offset; U2A3_NONE: none */
    uint32_t reloc_offset;              /*  80  file offset of the table */
    uint32_t reloc_count;               /*  84  words; each an image offset, strictly ascending, 4-aligned, inside the payload */
    uint32_t needs_offset;              /*  88 */
    uint32_t manifest_offset;           /*  92  apps: the JSON manifest of format 2, carried unchanged; 0: none */
    uint32_t manifest_size;             /*  96 */
    uint32_t reserved;                  /* 100  0 */
};

typedef char u2a3_header_size_check[(sizeof(U2a3Header) == U2A3_HEADER_SIZE) ? 1 : -1];
typedef char u2a3_need_size_check[(sizeof(U2a3Need) == U2A3_NEED_SIZE) ? 1 : -1];

#endif
