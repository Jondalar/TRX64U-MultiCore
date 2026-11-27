// The .u2a file, format version 3 (T104.1 D10, D11): the image of a library or an application, position independent, with its
// relocation table, its dependencies and its residence. The writer is tools/u2a_pack.py; the reader is libload.cc. All values are
// little-endian; every offset is a file offset and a multiple of 4.
//
//   header (104) | budget (header flag U2A3_FLAG_BUDGET, U2a3Budget) | exports (header flag U2A3_FLAG_EXPORTS, U2a3Exports) | needs (20 each) | payload (the image, linked at 0) | relocation table (u32 each) | manifest (JSON, apps)
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
/* Base 4.0: the needs list is the one way a file reaches a table (api->lib->find is gone): entry n is api->lib->need(n) (svctab.h, libslot.h), bound when the file
 * loads to the export of that name of the libraries the app is linked with. The file format is unchanged: needs_count is a byte. */
#define U2A3_NEEDS_MAX 8
/* Two needs that name no library (AppIndex and LibLoader skip them): */
#define PRG_APP_NEED "standalone"        /* offered only when the board is not the MultiCore host (T228 DC1) */
#define CART_APP_NEED "cart_port"        /* base 4.0: offered only where the running core's TOML A has the block c64cart (T239.3 D40: the capability's block guards the
                                            cartridge probe) */
#define U2A3_NONE 0xFFFFFFFFU           /* an entry offset the file does not have */

#define U2A3_ARCH_RISCV32 2             /* as U2A_ARCH_RISCV32 of format 2 */
#define U2A3_KIND_LIB 1
#define U2A3_KIND_APP 2
#define U2A3_RES_TRANSIENT 0            /* loaded when opened, freed when it returns (today's .u2a) */
#define U2A3_RES_RESIDENT 1             /* stays; its contributions and poll live on (D10.3) */

// The application binary interface the base offers, as major.minor. A file carries the majors it knows (abi_min .. abi_max); the
// minor is a dependency like any other: `needs base >= 4.0` (D11). Version numbers are major << 16 | minor. A file whose range does not hold this
// major is refused before anything of it is placed or runs; a file of major 3 is refused with the major it was built for and the one to rebuild with
// (libload.cc). Major 3 ended at 3.6 (SDK 0.5.1): 3.1 (T197) api->lib; 3.2 (SDK 0.2.0) the late poll, the budget record, a library's memory account,
// named exports; 3.3 (SDK 0.3.0) api->sync, settings, ctx, io, board; 3.4 (SDK 0.4.6) api->wb, overlay exit_to_machine and goto_screen; 3.5 (SDK
// 0.5.0) LN_LIST, ctx.ask_list, the F3 search sources; 3.6 (SDK 0.5.1) ctx.browse_at and browse_sheet, SVCFS_SIZE_UNKNOWN, [[key]] rows.
#define U2A_ABI_MAJOR 4
#define U2A_BASE_MINOR 0                /* 4.0 (T239.3 P2-6, D57: one joint cut with TRX64-Ultimate's P2-5, no fallback and no parallel old API): api->trx (SvcTrx with SvcRing), api->c64 (SvcC64), api->lib->find and LibRecord::exports are gone, and with them the base's export "c64" (C64Api); a table is exported by name only (LibRecord::export_list for a file, TOML B `exports` for a linked library, libslot.h) and reached through slots fixed when the app or the file is linked (libslot.h, SvcLib::need); SvcNet::announce takes SvcTrx::announce's place; U2A3_NEEDS_MAX 8; CoreDialogs of a core's own application (coredlg.h, T260, T239.3 D54) with edit_list and text */
#define U2A3_VERSION(major, minor) ((uint32_t)(((major) << 16) | (minor)))

// Limits the loader enforces before it allocates anything.
#define U2A3_IMAGE_MAX 0x00100000U      /* payload + bss, 1 MiB */
#define U2A3_ALIGN_MAX 4096U

/* The budget a library declares (T239.3 S13): header flag U2A3_FLAG_BUDGET says the record stands directly behind the header (file offset
 * U2A3_HEADER_SIZE); the needs, the image and everything else follow it, each at the offset the header names. `size` is the size of the record as the packer
 * wrote it; a later base API appends members and the size grows, so a loader reads the members it knows and ignores the rest. Every other member is a
 * ceiling (heap_keep is a promise the loader checks against the room of the heap, not a ceiling): U2A3_BUDGET_NONE says "no figure of its own", and the limit is then the one of the platform (the image: U2A3_IMAGE_MAX, the peak: the
 * heap, a registration: the room of the table). The loader refuses the library that is over a figure, with a message that names the library, the
 * figure and the limit. */
#define U2A3_FLAG_BUDGET 2U
#define U2A3_BUDGET_NONE 0xFFFFFFFFU
struct U2a3Budget {
    uint32_t size;                      /* sizeof(U2a3Budget) of the writer, at least that of this header */
    uint32_t image;                     /* payload + bss, bytes */
    uint32_t heap_keep;                 /* bytes the library holds through api->mem when it is idle: the loader refuses it when its image and this figure do not fit the room of the heap */
    uint32_t heap_peak;                 /* the most it holds at once through api->mem (the sizes asked for; realloc holds old and new block together): a ceiling, not a reservation */
    uint32_t ops, states, events;       /* what lib_register registers */
    uint32_t nodes, file_actions, settings, tabs, polls;   /* what the record of lib_register and the menu record declare together */
};
#define U2A3_BUDGET_SIZE 48
typedef char u2a3_budget_size_check[(sizeof(U2a3Budget) == U2A3_BUDGET_SIZE) ? 1 : -1];
#define U2A3_BUDGET_MAX 256U            /* a record of more bytes is not a budget */

/* The names a library exports (T239.3 D20, base 3.2): header flag U2A3_FLAG_EXPORTS says a list stands directly behind the budget record (behind the header when there is
 * none): a count, then that many entries of the size of a need. It is the static form of LibRecord's export list (libapi.h LibExportDecl). The loader refuses
 * a library whose lib_register does not return every name and major the file lists. A library may export more in its record than the file lists. Base 4.0 loads no folder
 * of libraries, so no load order is read from it: the order in which linked libraries start is the one of their `needs` in the TOML B (tools/libtab.py link_order). */
#define U2A3_FLAG_EXPORTS 4U
#define U2A3_EXPORTS_MAX 8
struct U2a3ExportName {
    char name[U2A3_NAME_MAX];           /* [a-z0-9_] */
    uint32_t major;                     /* the major version of the layout of the table, at least 1 */
};
#define U2A3_EXPORT_NAME_SIZE 20
typedef char u2a3_export_name_size_check[(sizeof(U2a3ExportName) == U2A3_EXPORT_NAME_SIZE) ? 1 : -1];

/* One entry of the needs list, which is the file's slot list (base 4.0, libslot.h): entry n is what api->lib->need(n) answers. */
struct U2a3Need {
    char name[U2A3_NAME_MAX];           /* "base", PRG_APP_NEED, CART_APP_NEED, or the name of an export (base 4.0: never a library's name) */
    uint32_t min_version;               /* major << 16 | minor: "base" has to be at least this; an export has exactly this major (its minor means nothing) */
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
    uint32_t flags;                     /*  16  U2A3_FLAG_MENU, U2A3_FLAG_BUDGET, U2A3_FLAG_EXPORTS */
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
