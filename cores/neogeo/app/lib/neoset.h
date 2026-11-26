// The Neo Geo ROM sets (T143 D5, D9, P8): which files a game is, where its bytes go in the DDR2 and what the core is told about them. Pure: CoreFs and CoreIo only, so
// tests/coreapp/ runs it against a model of the core's register block, its copy engine and the DDR2.
//
// A set comes from two kinds of source:
//   - a folder of the files of a cartridge (MAME names), described by an entry of the user's romsets.xml (the file MiSTer's NeoGeo core takes: the entry lists the files with the
//     index each one is loaded at, and the chips the cartridge has). Entries without <file> lines (the Darksoft style: the loader of the MiSTer framework names the files) are
//     recognised and refused with that reason.
//   - one .neo file (4096 byte header with the sizes of P, S, M, V1, V2 and C; the C ROM byte interleaved) plus the entry of romsets.xml of the same name for the chips.
// What the loader does is what Main_MiSTer's neogeo loader does to the SDRAM as DEV_NOTES.md of the upstream core and neogeo.sv:ioctl_addr_offset / CROM_LOAD_ADDR / VROM_LOAD_ADDR
// say (the code of the core is the reference; the loader itself is not in the repository):
//   P  index 4 at $200000 (P1, up to 1 MB), 5 at $280000, 6 at $300000 (P2, banked 1 MB at a time); bytes as the file has them (the 68k's words with the bytes swapped).
//   S  index 8 at $80000, 128 KB banks; every tile of 32 bytes is rearranged: out[line * 4 + k] = in[block(k) * 8 + line], block = 2, 3, 0, 1 (the fix data is read a line at a time).
//   M  index 9, the Z80 program.
//   V  index 16 + n at n * 512 KB of the V space (bit 24 of that address = the second V ROM, ADPCM-B's own).
//   C  index 64 + 2 * bank + parity: byte pairs interleaved, bank * 1 MB + 4 * word + 2 * parity; index 15: the file as it is (Darksoft's merged C ROM).
//   The BIOS (system ROM, 128 KB at 0) and the board fix ROM sfix.sfix (at $20000, same rearrangement as S) come from the user's own files, not from the set.
#ifndef COREAPP_NEOSET_H
#define COREAPP_NEOSET_H

#include "ctx.h"

#define NEO_NAME 56
#define NEO_PATH FS_PATH_MAX
#define NEO_FILES_MAX 48

struct NeoFileRef {
    char name[NEO_NAME];
    int index;                          // the loader's index (xml.md): P 4 5 6, S 8, M 9, V 16.., C 15 or 64..
    uint32_t offset;                    // where in the file loading starts
    uint32_t size;                      // how much (0: to the end of the file)
};

struct NeoSetInfo {
    char name[NEO_NAME];                // the set asked for
    bool found;                         // an entry of romsets.xml has this name
    uint8_t pcm, ct0, link, cmc, sma, pvc, xram, ms5p;
    int8_t rom_wait, p_wait;            // -1: the entry does not say
    int nfiles;                         // <file> lines; 0 for an entry of the Darksoft style
    NeoFileRef f[NEO_FILES_MAX];
};

enum NeoKind { NK_P = 1, NK_S, NK_M, NK_V, NK_C, NK_CNEO, NK_CRAW };

struct NeoPart {
    uint8_t kind;
    int index;                          // the loader's index
    uint32_t src_off;                   // start in the file
    uint32_t size;                      // bytes to load
    char path[NEO_PATH];
};

struct NeoParts {
    int n;
    NeoPart p[NEO_FILES_MAX];
    bool p_swap;                        // .neo only: the P ROM is in the 68k's byte order and has to be swapped for the core
    NeoSetInfo info;
};

// Where everything lies and what the core is told (all addresses are DDR2 byte addresses).
struct NeoPlan {
    uint32_t cfg;
    uint32_t p2_mask, c_mask, v1_mask, v2_mask, m_mask;
    uint8_t c_start;
    uint32_t rom_base, v1_base, v2_base, m1_base;
    uint32_t end;                       // the first byte after the set
    bool adpcma_ext;
};

// The DDR2 map of the application (offsets from the DDR2 window at 0x01000000, neogeo_regs.h): the CPU writes below NEO_CPU_LIMIT (64 MB less the staging megabyte); the core's copy
// engine moves what is staged there to any address of the 128 MB.
#define NEO_DDR_BASE   0x01000000u
#define NEO_CPU_LIMIT  0x03F00000u
#define NEO_STAGE_ABS  0x03F00000u
#define NEO_STAGE_SIZE 0x00100000u
#define NEO_DDR_END    0x08000000u

// romsets.xml: the entry named `set` (case-insensitive; an entry's name may be a comma separated list). False when the file cannot be read; info->found tells whether the entry is there.
bool neo_xml_find(CoreFs *fs, const char *xml_path, const char *set, NeoSetInfo *info);

// The parts of a folder set from its entry (the files are looked for in dir) and of a .neo file. 0 or a reason in msg.
int neo_parts_xml(CoreFs *fs, const char *dir, const NeoSetInfo &s, NeoParts *out, char *msg, int msglen);
int neo_parts_neo(CoreFs *fs, const char *path, const NeoSetInfo &s, NeoParts *out, char *msg, int msglen);

// Layout and masks. rom_base and m1_base are where the application puts them (neogeo_regs.h). 0 or a reason (does not fit the DDR2).
int neo_plan(const NeoParts &p, uint32_t rom_base, uint32_t m1_base, NeoPlan *out, char *msg, int msglen);

// The chip word of the core (neogeo.sv cfg).
uint32_t neo_cfg_word(const NeoSetInfo &s, bool adpcma_ext);

// Streams the files into the DDR2 and writes the plan into the core's registers (the core is held). 0 or a reason.
int neo_load(CoreCtx *c, const NeoParts &p, const NeoPlan &pl, char *msg, int msglen);

// Bytes into the DDR2 at any address: below the CPU's limit straight through the window, otherwise through the staging megabyte and the core's copy engine (which also does the byte masks).
bool neo_put(CoreCtx *c, uint32_t abs_dst, const uint8_t *data, uint32_t n, int mask_mode);   // mask_mode: 0 all bytes, 1 bytes 0,1 of every four, 2 bytes 2,3
void neo_fix_rearrange(uint8_t *data, uint32_t n);                                           // n a multiple of 32, in place

// Registers as 32 bit values.
void neo_wr32(CoreIo *io, uint32_t reg, uint32_t v);
uint32_t neo_rd32(CoreIo *io, uint32_t reg);

#endif
