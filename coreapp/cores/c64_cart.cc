#include "c64_cart.h"
#include "c64_cart_desc.h"
#include "c64_regs.h"
#include "boot.h"
#include "ops_glue.h"
#include "engines.h"
#include "writeback.h"
#include "writeback_crt.h"
#include <stdio.h>
#include <string.h>

// ---- where the image goes --------------------------------------------------------------------------------------------------------------------
// 16 KB per bank, ROML at +0, ROMH at +0x2000 of the bank (Gideon c64_crt.cc read_chip_packet: bank * 16 KB + (load & 0x2000)). With the bitstream's
// ROM_SEL (T60) the image is the expansion-port block 0x01000000, the REU's memory; without it Gideon's __cart_rom_start 0x03C00000. Both lie behind
// the DDR2 window (io->ddr_write, 0x01000000 + off). The window the bitstream addresses is 4 MiB, and 8 MiB for EasyFlash 3 (CRT 90, XPORT_CAPS
// b0 and b1; trx_cart.cc TRX_XPORT_ROM_MAX_EF3) - g_image_max says which for the cartridge being started.
#define XPORT_OFS      0x00000000u
#define GIDEON_OFS     0x02C00000u
#define IMAGE_MAX      0x00400000u
#define IMAGE_MAX_EF3  0x00800000u
static uint32_t g_image_max = IMAGE_MAX;
#define BANK           0x4000u
#define CART_RAM_ABS   0x00EF0000u        // __cart_ram_start (linker.x): 64 KB the cartridge's RAM is, cleared at every start
#define CART_RAM_BYTES 0x10000u

// ---- the CRT file ----------------------------------------------------------------------------------------------------------------------------
#define CRT_HDRSIZE 0x10
#define CRT_TYPE    0x16
#define CRT_EXROM   0x18
#define CRT_GAME    0x19
#define CHP_LEN     0x04
#define CHP_TYPE    0x08
#define CHP_BANK    0x0A
#define CHP_LOAD    0x0C
#define CHP_SIZE    0x0E

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

// CRT hardware type -> the CART_TYPE byte of the bitstream (Gideon configure_cart: the CART_TYPE_* values of c64.h with their variants). 0 in
// `hw` is resolved from the header (normal) or the chips (Ocean). `trx_only`: the type exists only on the TRX64 bitstream (the descriptor).
struct CrtKind {
    uint16_t crt;
    uint8_t hw;
    bool trx_only;
    uint16_t flash_banks;                 // a flash cartridge always has its whole chip: the banks the file leaves out read $FF (auto_mirror)
    const char *name;
};
static const CrtKind kinds[] = {
    {  0, 0x00, false,   0, "Normal cartridge" },
    {  1, 0x1B, false,   0, "Action Replay" },
    {  4, 0x05, false,   0, "Simons Basic" },
    {  5, 0x00, false,   0, "Ocean" },
    { 15, 0x0A, false,   0, "C64 Game System" },
    { 17, 0x88, true,    0, "Dinamic" },
    { 19, 0x28, false,   0, "Magic Desk" },
    { 32, 0x11, false,  64, "EasyFlash" },
    { 36, 0x3B, false,   0, "Retro Replay" },
    { 60, 0x48, true,   64, "GMod2" },
    { 90, 0x12, true,  512, "EasyFlash 3" },
    { 85, 0xA8, true,    0, "Magic Desk 16" },
    { 86, 0x0F, false, 128, "Protovision Megabyter" },
    { 88, 0x68, true,  256, "C64MegaCart" },
    { 89, 0x2F, true,  128, "Protovision TwoMegabyter" },
    { 232, 0x31, true, 256, "EasyFlashXl" },
};

struct CrtName { uint16_t crt; const char *name; };
static const CrtName unsupported[] = {
    { 2, "KCS Power Cartridge" }, { 3, "Final Cartridge III" }, { 6, "Expert Cartridge" }, { 8, "Super Games" }, { 9, "Atomic Power" },
    { 10, "Epyx Fastload" }, { 11, "Westermann" }, { 12, "Rex" }, { 13, "Final Cartridge I" }, { 18, "Zaxxon" }, { 20, "Super Snapshot 5" },
    { 21, "COMAL 80" }, { 53, "Pagefox" }, { 54, "Kingsoft Business Basic" },
};

static const char *unsupported_name(uint16_t t)
{
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        if (unsupported[i].crt == t) {
            return unsupported[i].name;
        }
    }
    return 0;
}

static const C64CartDesc *find_desc(uint8_t cart_type)
{
    for (unsigned i = 0; i < sizeof(c64_cart_descs) / sizeof(c64_cart_descs[0]); i++) {
        if (c64_cart_descs[i].cart_type == cart_type) {
            return &c64_cart_descs[i];
        }
    }
    return 0;
}

// What one pass over the file's CHIP packets found.
struct CrtScan {
    uint32_t chips;                       // packets with data for the image
    uint32_t highest_bank;
    bool a000;
    uint32_t image_end;                   // the first byte behind the data of the image
};

typedef bool (*chip_cb)(void *user, uint32_t file_off, uint16_t bank, uint16_t load, uint16_t size);

// Walks the packets from `pos`. 0 or a reason; `each` is called for every packet that carries image data (type 0 ROM or 2 flash, size > 0).
static int walk_chips(CoreFile *f, uint64_t fsize, uint32_t pos, chip_cb each, void *user, char *msg, int msglen)
{
    uint32_t n = 0;
    for (;;) {
        uint8_t h[16];
        if (pos + 16 > fsize || !f->read(pos, h, 16)) {
            break;
        }
        if (memcmp(h, "CHIP", 4)) {
            if (!n) {
                snprintf(msg, msglen, "no CHIP packet where the header says (offset %u)", (unsigned)pos);
                return 1;
            }
            break;                                       // padding behind the last packet
        }
        uint32_t len = be32(h + CHP_LEN);
        uint16_t type = be16(h + CHP_TYPE), bank = be16(h + CHP_BANK), load = be16(h + CHP_LOAD), size = be16(h + CHP_SIZE);
        n++;
        if (len < 16u + size || pos + 16ull + size > fsize) {
            snprintf(msg, msglen, "chip packet %u is cut off (%u bytes announced, %u in the file)", (unsigned)n, (unsigned)size,
                     (unsigned)(fsize > pos + 16 ? fsize - pos - 16 : 0));
            return 1;
        }
        if (size && (type == 0 || type == 2) && load != 0xDE00) {
            if (!each(user, pos + 16, bank, load, size)) {
                snprintf(msg, msglen, "chip packet %u (bank %u at $%04X, %u bytes) does not fit the cartridge image", (unsigned)n, bank, load,
                         (unsigned)size);
                return 1;
            }
        }
        pos += len;
    }
    return 0;
}

static bool scan_cb(void *user, uint32_t, uint16_t bank, uint16_t load, uint16_t size)
{
    CrtScan *s = (CrtScan *)user;
    if ((load != 0x8000 && load != 0xA000 && load != 0xE000) || size > BANK) {
        return false;                                    // a C128 cartridge (32 KB banks) or an address no C64 cartridge has
    }
    uint32_t end = (uint32_t)bank * BANK + (load & 0x2000) + (size < 0x2000 ? 0x2000 : size);
    if (end > g_image_max) {
        return false;
    }
    s->chips++;
    if (bank > s->highest_bank) {
        s->highest_bank = bank;
    }
    if (load == 0xA000) {
        s->a000 = true;
    }
    if (end > s->image_end) {
        s->image_end = end;
    }
    return true;
}

// ---- the image in DDR2 -----------------------------------------------------------------------------------------------------------------------
static uint8_t buf[BANK];

struct Load {
    CoreIo *io;
    CoreFile *f;
    uint32_t base;
    bool bad;
};

// What the file said, kept for "Save Cartridge": the header and the chips' places, so the image can be written out again as a .crt from what is in DDR2 now.
#define CHIPS_MAX 1024                                  // EasyFlash 3: 8 slots x 64 banks x 2 halves
struct ChipRec { uint16_t bank, load, size; };
static ChipRec g_chip[CHIPS_MAX];
static int g_nchip;
static uint8_t g_hdr[0x40];
static uint32_t g_img_base;

static bool load_cb(void *user, uint32_t file_off, uint16_t bank, uint16_t load, uint16_t size)
{
    Load *l = (Load *)user;
    if (!l->f->read(file_off, buf, size)) {
        l->bad = true;
        return false;
    }
    if (g_nchip < CHIPS_MAX) {
        g_chip[g_nchip].bank = bank;
        g_chip[g_nchip].load = load;
        g_chip[g_nchip].size = size;
        g_nchip++;
    }
    uint32_t n = size;
    if (n < 0x2000) {                                    // a chip below 8 KB repeats up to 8 KB (Gideon read_chip_packet)
        uint32_t p = 256;
        while (p < n) {
            p <<= 1;
        }
        memset(buf + n, 0xFF, p - n);
        for (n = p; n < 0x2000; n <<= 1) {
            memcpy(buf + n, buf, n);
        }
    }
    l->io->ddr_write(l->base + (uint32_t)bank * BANK + (load & 0x2000), buf, n);
    return true;
}

static void fill(CoreIo *io, uint32_t base, uint32_t from, uint32_t to, uint8_t v)
{
    memset(buf, v, BANK);
    for (uint32_t a = from; a < to; a += BANK) {
        io->ddr_write(base + a, buf, BANK);
    }
}

// The banks the hardware's address lines can reach above the file's are copies of it (Gideon auto_mirror): the image doubles up to the window.
static void mirror(CoreIo *io, uint32_t base, uint32_t used)
{
    for (uint32_t len = used; len < g_image_max; len <<= 1) {
        for (uint32_t a = 0; a < len; a += BANK) {
            io->ddr_read(base + a, buf, BANK);
            io->ddr_write(base + len + a, buf, BANK);
        }
    }
}

// ---- the flash the C64 programs (T154, D4) -----------------------------------------------------------------------------------------------------
// The bitstream's cartridge model answers the flash command sequences itself and holds a program or erase busy until the application has applied
// it to the image in DDR2 and acknowledged it (trx_cart.cc, trx_cart_irq). Here the same, in the interrupt the TRX block raises (IRQ_CAUSE b4
// flash operation, b5 EEPROM written): the operation is applied to the image, the changed blocks go to the write-back service (writeback.h), the
// ACK frees the C64. The geometry is the chip's: a sector is 64 KB of one chip, never a convenient bank number (EF_GEOMETRY.md).
static const C64FlashPart *g_flash;                      // the chip(s) of the cartridge in the port; null: it has none the C64 can program
static CoreIo *g_io;

// Image offset of chip address a (chip: 0 ROML, 1 ROMH of a two chip cartridge): linear 1 - one chip across ROML and ROMH; else the banks are
// 16 KB slots, ROML first and ROMH 8 KB above it, a chip address {a[20:13], a[12:0]} becoming {bank, chip, a[12:0]}.
static uint32_t flash_image_offset(const C64FlashPart *p, uint32_t a, int chip)
{
    if (p->linear == 2) {                                // EasyFlash 3: the chip's order {slot(3), bank_hi(3), ROMH, bank_lo(3), A12-A0} becomes {slot, bank_hi, bank_lo, ROMH, A12-A0}
        return (((a >> 17) & 0x3F) << 17) | (((a >> 13) & 7) << 14) | (((a >> 16) & 1) << 13) | (a & 0x1FFF);
    }
    if (p->linear) {
        return a;
    }
    return ((a >> 13) << 14) | ((uint32_t)chip << 13) | (a & 0x1FFF);
}

// The sector holding chip address a: runs of equal sectors from the chip's start upwards
static void flash_sector_of(const C64FlashPart *p, uint32_t a, uint32_t *start, uint32_t *size)
{
    uint32_t base = 0;
    for (int r = 0; r < p->runs; r++) {
        for (int n = 0; n < p->run[r].count; n++) {
            if (a < base + p->run[r].size) {
                *start = base;
                *size = p->run[r].size;
                return;
            }
            base += p->run[r].size;
        }
    }
    *start = a & ~0xFFFFu;                               // beyond the map: a is masked to the chip, so this cannot happen
    *size = 0x10000;
}

// Applies one operation to the image. A program only clears bits; an erase sets whole 8 KB pieces to $FF (every sector is a whole number of them,
// each contiguous in the image). Short and without allocation: it runs in the interrupt.
static void flash_apply(CoreIo *io, int cmd, int chip, uint32_t a, uint8_t data)
{
    const C64FlashPart *p = g_flash;
    if (cmd == 1) {
        uint32_t off = flash_image_offset(p, a, chip);
        if (off < g_image_max) {
            uint8_t b;
            io->ddr_read(g_img_base + off, &b, 1);
            b &= data;
            io->ddr_write(g_img_base + off, &b, 1);
            wb_mark(off, 1);
        }
        return;
    }
    uint32_t start, size;
    if (cmd == 3) {                                      // chip erase: the whole chip
        start = 0;
        size = p->chip_size;
    } else {
        flash_sector_of(p, a, &start, &size);
    }
    static uint8_t ff[1024];
    if (ff[0] != 0xFF) {
        memset(ff, 0xFF, sizeof(ff));
    }
    for (uint32_t x = start; x < start + size; x += 0x2000) {
        uint32_t off = flash_image_offset(p, x, chip);
        if (off + 0x2000 > g_image_max) {
            continue;
        }
        for (uint32_t k = 0; k < 0x2000; k += sizeof(ff)) {
            io->ddr_write(g_img_base + off + k, ff, sizeof(ff));
        }
        wb_mark(off, 0x2000);
    }
}

// The interrupt handler (IRQ_CAUSE b4 / b5, level): cleared before the ACK, because the next operation cannot start before it.
static uint8_t c64_flash_irq(uint8_t act)
{
    CoreIo *io = g_io;
    if (!io) {
        return 0;
    }
    if (act & C64_IRQ_EEPROM) {
        io->wr(C64_IRQ_CAUSE, C64_IRQ_EEPROM);
        wb_mark_aux();                                   // the 93C86 applied it; the copy is the writer's
    }
    if (act & C64_IRQ_FLASH) {
        io->wr(C64_IRQ_CAUSE, C64_IRQ_FLASH);
        uint32_t evt = io->rd(C64_CART_EVT);             // byte 0 latches 1-3
        evt |= (uint32_t)io->rd(C64_CART_EVT + 1) << 8;
        evt |= (uint32_t)io->rd(C64_CART_EVT + 2) << 16;
        evt |= (uint32_t)io->rd(C64_CART_EVT + 3) << 24;
        uint32_t hi = io->rd(C64_CART_EVT_HI) & 3;
        int cmd = (int)(evt >> 30);
        if (cmd == 0) {
            return 0;                                    // acknowledged by set_type already
        }
        if (g_flash) {
            int chip = g_flash->chips == 2 ? (int)((evt >> 29) & 1) : 0;
            uint32_t a = (((evt >> 8) & 0x1FFFFF) | (hi << 21)) & (g_flash->chip_size - 1);
            flash_apply(io, cmd, chip, a, (uint8_t)evt);
        }
        io->wr(C64_CART_CMD, C64_CART_CMD_INVAL | C64_CART_CMD_ACK);
    }
    return 0;
}

// The machine sees its flash busy while the image is copied: a pending operation waits for the interrupt to be enabled again.
static void flash_hold(void *, bool on)
{
    WbPort *port = wb_port();
    if (port && g_flash) {
        port->irq_enable(C64_IRQ_FLASH, !on);
    }
}

// The GMod2's EEPROM, 2 KB behind the window: a copy that one write of the C64 did not tear (WR_COUNT is the seqlock).
#define EE_DATA  2048u
static uint16_t wr_count(CoreIo *io)
{
    uint8_t lo = io->rd(C64_CART_WR_COUNT);              // latches byte 1
    return (uint16_t)(((uint16_t)io->rd(C64_CART_WR_COUNT + 1) << 8) | lo);
}
static int eeprom_copy(void *, uint8_t *buf, uint32_t n)
{
    CoreIo *io = g_io;
    for (int tries = 0; tries < 4; tries++) {
        uint16_t before = wr_count(io);
        uint8_t one = 1;
        io->eeprom_write(0, &one, 1);                    // the dirty flag cleared before the copy, so a write during it is not lost
        io->eeprom_read(EE_DATA, buf, n);
        if (wr_count(io) == before) {
            return 0;
        }
    }
    return 1;
}

static WbCrt g_wbcrt;

// A cartridge with a flash part: the responder is configured from it, the interrupt attached, the file registered with the write-back service.
// The machine is held. Called after set_type, which left the responder off and acknowledged what the cartridge before it had pending.
static void flash_configure(CoreIo *io, uint8_t cart_type, const char *path, const char *name)
{
    const C64CartDesc *d = find_desc(cart_type);
    g_io = io;
    g_flash = d ? d->flash : 0;
    if (!g_flash) {
        return;
    }
    const C64FlashPart *p = g_flash;
    io->wr(C64_CART_FL_MAGIC1, (uint8_t)p->magic1);
    io->wr(C64_CART_FL_MAGIC1 + 1, (uint8_t)(p->magic1 >> 8));
    io->wr(C64_CART_FL_MAGIC2, (uint8_t)p->magic2);
    io->wr(C64_CART_FL_MAGIC2 + 1, (uint8_t)(p->magic2 >> 8));
    io->wr(C64_CART_FL_MASK, (uint8_t)p->mask);
    io->wr(C64_CART_FL_MASK + 1, (uint8_t)(p->mask >> 8));
    io->wr(C64_CART_FL_ID, p->id_mfr);
    io->wr(C64_CART_FL_ID + 1, p->id_dev);
    io->wr(C64_CART_FL_ID + 2, p->id_ofs);
    io->wr(C64_CART_FL_ENABLE, 1);

    g_wbcrt.io = io;
    g_wbcrt.img_base = g_img_base;
    g_wbcrt.img_bytes = g_image_max;
    g_wbcrt.eeprom_read = cart_type == 0x48 ? eeprom_copy : 0;
    g_wbcrt.eeprom_user = 0;
    WbMedium m;
    memset(&m, 0, sizeof(m));
    snprintf(m.path, sizeof(m.path), "%s", path);
    snprintf(m.name, sizeof(m.name), "%s", name);
    m.write = wb_crt_write;
    m.user = &g_wbcrt;
    m.hold = flash_hold;
    wb_register(m);
    if (WbPort *port = wb_port()) {
        port->irq_attach(C64_IRQ_FLASH | C64_IRQ_EEPROM, c64_flash_irq);
        port->irq_enable(C64_IRQ_FLASH | C64_IRQ_EEPROM, true);
    }
    printf("[c64] cartridge type %02x: %s, flash write-back on\n", cart_type, p->part);
}

// ---- the registers ---------------------------------------------------------------------------------------------------------------------------
static void set_type(CoreIo *io, bool block, uint8_t cart_type)
{
    io->cart_wr(C64_CARTREG_TYPE, cart_type);
    if (!block) {
        return;                                          // Gideon's bitstream: the type alone selects the mapping
    }
    // trx_cart_configure: a pending flash operation of the cartridge before this one is acknowledged unapplied, the responder stays off, and
    // the descriptor (all-zero for no cartridge: the bitstream keeps it off) is taken at the next reset. What the write-back service held for
    // the cartridge before is not this image's: the caller has flushed it (wb_flush) when the setting wanted that.
    if (WbPort *port = wb_port()) {
        port->irq_enable(C64_IRQ_FLASH | C64_IRQ_EEPROM, false);
    }
    g_flash = 0;
    wb_unregister();
    io->wr(C64_IRQ_CAUSE, C64_IRQ_FLASH | C64_IRQ_EEPROM);
    io->wr(C64_CART_CMD, C64_CART_CMD_INVAL | C64_CART_CMD_ACK);
    const C64CartDesc *d = cart_type ? find_desc(cart_type) : 0;
    for (int i = 0; i < C64_CART_DESC_BYTES; i++) {
        io->wr(C64_CART_DESC + i, d ? d->desc[i] : 0);
    }
    io->wr(C64_CART_FL_ENABLE, 0);
}

// trx_xport_shared (trx_cart.cc): ROM_SEL reads back what is written to it
static bool xport_shared(CoreIo *io)
{
    if (io->rd(C64_CART_ROM_SEL) & 3) {
        return true;
    }
    io->wr(C64_CART_ROM_SEL, 1);
    bool shared = io->rd(C64_CART_ROM_SEL) == 1;
    io->wr(C64_CART_ROM_SEL, 0);
    return shared;
}

// Back to a machine without a cartridge after a failure with the machine held.
static char g_cart_name[56];
static void give_up(CoreCtx *c, bool block)
{
    g_cart_name[0] = 0;
    set_type(c->io, block, 0);
    mount_notify_name(c, "Cartridge");                                // T180 D2: told by the name of the slot in the core's [mount] list
    char m[80];
    boot_cold_reset(c, m, sizeof(m));
}

static int g_cart_crt = -1;
static char g_cart_path[FS_PATH_MAX];
bool c64_cart_active(void) { return g_cart_name[0] != 0; }
const char *c64_cart_name(void) { return g_cart_name; }
const char *c64_cart_path(void) { return g_cart_name[0] ? g_cart_path : ""; }
int c64_cart_crt_type(void) { return g_cart_crt; }

// The GMod2's EEPROM: the packet at load $DE00 (up to 2 KB) goes into the window, padded with $FF; a file without one starts a blank EEPROM. The
// dirty flag is cleared, so only what the C64 writes from here on is a change.
static void load_eeprom(CoreIo *io, CoreFile *f, uint64_t fsize, uint32_t pos)
{
    static uint8_t ee[2048];
    memset(ee, 0xFF, sizeof(ee));
    for (;;) {
        uint8_t h[16];
        if (pos + 16 > fsize || !f->read(pos, h, 16) || memcmp(h, "CHIP", 4)) {
            break;
        }
        uint32_t len = be32(h + CHP_LEN);
        uint16_t load = be16(h + CHP_LOAD), size = be16(h + CHP_SIZE);
        if (load == 0xDE00 && size && size <= sizeof(ee) && pos + 16ull + size <= fsize) {
            f->read(pos + 16, ee, size);
            break;
        }
        if (len < 16) {
            break;
        }
        pos += len;
    }
    uint8_t one = 1;
    io->eeprom_write(2048, ee, sizeof(ee));
    io->eeprom_write(0, &one, 1);
}

static bool g_for_state;                                       // a loaded state's cartridge: inserted, not booted (no hold, no reset: the state goes in next)

static int cart_run(CoreCtx *c, const char *path, char *msg, int msglen, bool do_reset);

// do_reset false: the image is placed and nothing boots it - for a loaded state (the state goes in next) and for the cartridge the SID player's end puts
// back while the caller holds the machine (T228 R21)
static int run_image(CoreCtx *c, CoreFile *f, const char *path, bool do_reset, char *msg, int msglen)
{
    CoreIo *io = c->io;
    MediaLoading loading(c);                               // T226 S1

    // ---- everything that can be said about the file before the machine is touched
    uint8_t h[0x40];
    uint64_t fsize = f->size();
    if (fsize < 0x40 || !f->read(0, h, 0x40)) {
        snprintf(msg, msglen, "%s: not a CRT file", path);
        return 1;
    }
    if (!memcmp(h, "C128 CARTRIDGE  ", 16)) {
        snprintf(msg, msglen, "%s: a C128 cartridge", path);
        return 1;
    }
    if (memcmp(h, "C64 CARTRIDGE   ", 16)) {
        snprintf(msg, msglen, "%s: not a CRT file (no C64 CARTRIDGE signature)", path);
        return 1;
    }
    uint16_t crt = be16(h + CRT_TYPE);
    bool block = (io->rd(C64_CAPS) & C64_CAPS_CART) != 0;            // the bitstream has the cartridge block (trx_cart_present)
    const CrtKind *k = 0;
    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (kinds[i].crt == crt) {
            k = &kinds[i];
        }
    }
    if (!k || (k->trx_only && !block)) {
        const char *nm = k ? k->name : unsupported_name(crt);
        if (nm) {
            snprintf(msg, msglen, "Cartridge type %u (%s) is not supported%s", crt, nm, k ? ": it needs the TRX64 bitstream's cartridge block" : "");
        } else {
            snprintf(msg, msglen, "Cartridge type %u is not supported", crt);
        }
        return 1;
    }
    g_image_max = IMAGE_MAX;
    if (crt == 90) {                                                  // EasyFlash 3: the 8 MiB window and the banking in the bitstream, and the shared block (ROM_SEL)
        if ((io->rd(C64_CART_XPORT_CAPS) & (C64_XCAPS_8MB | C64_XCAPS_EF3)) != (C64_XCAPS_8MB | C64_XCAPS_EF3) || !xport_shared(io)) {
            snprintf(msg, msglen, "Cartridge type 90 (EasyFlash 3) needs the bitstream's 8 MiB window and EasyFlash 3 banking (XPORT_CAPS)");
            return 1;
        }
        g_image_max = IMAGE_MAX_EF3;
    }
    uint32_t hdr = be32(h + CRT_HDRSIZE);
    if (hdr < 0x40) {
        hdr = 0x40;
    }
    CrtScan scan;
    memset(&scan, 0, sizeof(scan));
    if (walk_chips(f, fsize, hdr, scan_cb, &scan, msg, msglen)) {
        return 1;
    }
    if (!scan.chips) {
        snprintf(msg, msglen, "%s: the file has no ROM data", path);
        return 1;
    }
    uint8_t hw = k->hw;
    if (crt == 0) {                                                   // normal: the EXROM and GAME lines the header names
        uint8_t exrom = h[CRT_EXROM], game = h[CRT_GAME];
        if (exrom == 1 && game == 0) {
            hw = 0xA1;                                                // Ultimax (and the VIC served, CART_TYPE_UMAX)
        } else if (exrom == 0 && game == 0) {
            hw = 0x01;                                                // 16 KB
        } else if (exrom == 0 && game == 1) {
            hw = 0x41;                                                // 8 KB
        } else {
            snprintf(msg, msglen, "Normal cartridge with EXROM %u / GAME %u maps nothing", exrom, game);
            return 1;
        }
    } else if (crt == 5) {
        hw = scan.a000 ? 0x69 : 0x08;                                 // Ocean: 16 KB banks when a chip sits at $A000, else 8 KB
    }
    if (block && !find_desc(hw)) {
        snprintf(msg, msglen, "Cartridge type %u (%s): the bitstream has no mapping for it", crt, k->name);
        return 1;
    }
    bool shared = block && xport_shared(io);
    uint32_t base = shared ? XPORT_OFS : GIDEON_OFS;

    // ---- what the cartridge before this one changed goes to its file first (Auto): the new image overwrites it
    char fm[160];
    wb_flush(c, WB_WHY_EJECT, fm, sizeof(fm));

    // ---- the machine held, the image placed, the registers set, the reset
    if (!g_for_state) {
        io->mode_reset(true);
    }
    io->cart_wr(C64_CARTREG_TYPE, 0);                                 // the previous cartridge off before its image goes
    io->cart_wr(C64_CARTREG_REU, 0);                                  // one module in the port: the image lies in the REU's memory, the REU is off
    io->cart_wr(C64_CARTREG_SAMPLER, 0);
    io->bus_wr(C64_BUSREG_INTERNAL, 15);                              // the internal cartridge, no external one (Gideon start_cartridge, custom cart)
    io->bus_wr(C64_BUSREG_EXTERNAL, 0);
    if (shared) {
        io->wr(C64_CART_ROM_SEL, 1);
        io->wr(C64_CART_CMD, C64_CART_CMD_INVAL);                     // trx_cart_select_rom: the cached lines of the previous image go
    }
    uint32_t banks = 1;
    while (banks <= scan.highest_bank) {
        banks <<= 1;
    }
    if (banks < k->flash_banks) {
        banks = k->flash_banks;
    }
    fill(io, base, 0, banks * BANK, 0xFF);
    Load l = { io, f, base, false };
    g_nchip = 0;
    memcpy(g_hdr, h, sizeof(g_hdr));
    g_img_base = base;
    char m2[100];
    if (walk_chips(f, fsize, hdr, load_cb, &l, m2, sizeof(m2)) || l.bad) {
        snprintf(msg, msglen, "cannot read %s (the medium is gone?)", path);
        give_up(c, block);
        return 1;
    }
    if (hw != 0x01 && hw != 0x41 && hw != 0xA1) {                     // the types with no banking never address above their 16 KB
        mirror(io, base, banks * BANK);
    }
    set_type(io, block, hw);
    if (block && hw == 0x48) {
        load_eeprom(io, f, fsize, hdr);
    }
    {
        const char *sl = strrchr(path, '/');
        if (block) {
            flash_configure(io, hw, path, sl ? sl + 1 : path);
        }
    }
    static uint8_t zero[4096];
    for (uint32_t a = 0; a < CART_RAM_BYTES; a += sizeof(zero)) {
        io->ddr_abs_write(CART_RAM_ABS + a, zero, sizeof(zero));      // the cartridge's RAM starts clean
    }
    printf("[c64] cartridge %s: CRT type %u -> CART_TYPE %02x, %u chips, %u banks, image at %s\n", path, crt, hw, (unsigned)scan.chips,
           (unsigned)banks, shared ? "0x01000000 (ROM_SEL)" : "0x03C00000");
    const char *slash = strrchr(path, '/');
    fs_short_name(g_cart_name, sizeof(g_cart_name), slash ? slash + 1 : path);
    snprintf(g_cart_path, sizeof(g_cart_path), "%s", path);
    g_cart_crt = crt;
    mount_notify_name(c, "Cartridge");                                // T180 D2
    return do_reset ? boot_cold_reset(c, msg, msglen) : 0;
}

// ---- the SID player's cartridge (T228 R21 item 7) --------------------------------------------------------------------------------------------
// FW FileTypeSID::prepare starts a 16 KB cartridge it builds in RAM (sid_cart / mus_cart: C64_START_CART with cart_def type CART_TYPE_16K, custom_addr) and
// restores the cartridge the machine had at the next reset (C64::restoreCart). Here: the 16 KB are the image of bank 0, a normal cartridge (hw 0x01: EXROM and
// GAME low), placed as c64_cart_run places a CHIP packet; the cartridge before it is remembered by path, and the next reset of the application puts it back
// (c64_cart_player_end, called from the reset's hold). The player cartridge is not "in the port" for the Library: c64_cart_active() is false for it.
static bool g_player;
static bool g_fresh;                                                   // the reset that starts the player is not the one that ends it
static char g_prev_path[FS_PATH_MAX];

bool c64_cart_player_active(void) { return g_player; }

int c64_cart_run_image(CoreCtx *c, const char *name, const uint8_t *img, char *msg, int msglen)
{
    CoreIo *io = c->io;
    bool block = (io->rd(C64_CAPS) & C64_CAPS_CART) != 0;
    if (block && !find_desc(0x01)) {
        snprintf(msg, msglen, "the bitstream has no mapping for a 16 KB cartridge");
        return 1;
    }
    bool shared = block && xport_shared(io);
    uint32_t base = shared ? XPORT_OFS : GIDEON_OFS;
    char fm[160];
    wb_flush(c, WB_WHY_EJECT, fm, sizeof(fm));                         // what the cartridge before it changed goes to its file first
    if (!g_player) {
        snprintf(g_prev_path, sizeof(g_prev_path), "%s", g_cart_name[0] ? g_cart_path : "");
    }
    io->mode_reset(true);
    io->cart_wr(C64_CARTREG_TYPE, 0);
    io->cart_wr(C64_CARTREG_REU, 0);
    io->cart_wr(C64_CARTREG_SAMPLER, 0);
    io->bus_wr(C64_BUSREG_INTERNAL, 15);
    io->bus_wr(C64_BUSREG_EXTERNAL, 0);
    if (shared) {
        io->wr(C64_CART_ROM_SEL, 1);
        io->wr(C64_CART_CMD, C64_CART_CMD_INVAL);
    }
    g_image_max = IMAGE_MAX;
    io->ddr_write(base, img, BANK);
    g_nchip = 0;
    g_img_base = base;
    set_type(io, block, 0x01);
    static uint8_t zero[4096];
    for (uint32_t a = 0; a < CART_RAM_BYTES; a += sizeof(zero)) {
        io->ddr_abs_write(CART_RAM_ABS + a, zero, sizeof(zero));
    }
    printf("[c64] cartridge %s: 16 KB image, CART_TYPE 01, image at %s\n", name, shared ? "0x01000000 (ROM_SEL)" : "0x03C00000");
    g_cart_name[0] = 0;
    g_player = true;
    g_fresh = true;
    mount_notify_name(c, "Cartridge");
    return 0;                                                          // the caller resets the machine when the RAM is ready
}

// The reset of the application ends the player's cartridge: the one before it is placed again (or none), the machine is held by the caller.
void c64_cart_player_end(CoreCtx *c)
{
    if (!g_player) {
        return;
    }
    if (g_fresh) {
        g_fresh = false;
        return;
    }
    g_player = false;
    char m[160];
    m[0] = 0;
    if (g_prev_path[0]) {
        char path[FS_PATH_MAX];
        snprintf(path, sizeof(path), "%s", g_prev_path);
        g_prev_path[0] = 0;
        if (!cart_run(c, path, m, sizeof(m), false)) {
            return;
        }
    }
    g_prev_path[0] = 0;
    set_type(c->io, (c->io->rd(C64_CAPS) & C64_CAPS_CART) != 0, 0);
    g_cart_name[0] = 0;
    mount_notify_name(c, "Cartridge");
}

static int cart_run(CoreCtx *c, const char *path, char *msg, int msglen, bool do_reset)
{
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    struct Closer { CoreCtx *c; CoreFile *f; ~Closer() { c->fs->close(f); } } closer = { c, f };
    return run_image(c, f, path, do_reset, msg, msglen);
}

int c64_cart_run(CoreCtx *c, const char *path, char *msg, int msglen)
{
    return cart_run(c, path, msg, msglen, !g_for_state);
}

// "Save Cartridge": the image as it is in DDR2 now, written as a .crt with the header and the chip layout of the file it came from.
int c64_cart_save(CoreCtx *c, const char *path, char *msg, int msglen)
{
    if (!c64_cart_active() || !g_nchip) {
        snprintf(msg, msglen, "There is no cartridge in the port");
        return 1;
    }
    if (!c->fs->save(path, g_hdr, 0)) {
        snprintf(msg, msglen, "cannot write %s", path);
        return 1;
    }
    CoreFile *f = c->fs->open(path, true);
    if (!f || !f->writable()) {
        if (f) {
            c->fs->close(f);
        }
        snprintf(msg, msglen, "%s: the medium cannot be written", path);
        return 1;
    }
    uint32_t hdr = be32(g_hdr + CRT_HDRSIZE);
    if (hdr < 0x40 || hdr > 0x40) {
        hdr = 0x40;
    }
    uint64_t pos = 0;
    bool ok = f->write(pos, g_hdr, hdr);
    pos += hdr;
    for (int i = 0; ok && i < g_nchip; i++) {
        uint8_t ch[16];
        memcpy(ch, "CHIP", 4);
        uint32_t len = 16u + g_chip[i].size;
        ch[4] = (uint8_t)(len >> 24); ch[5] = (uint8_t)(len >> 16); ch[6] = (uint8_t)(len >> 8); ch[7] = (uint8_t)len;
        ch[8] = 0; ch[9] = 0;                                         // type 0: ROM
        ch[10] = (uint8_t)(g_chip[i].bank >> 8); ch[11] = (uint8_t)g_chip[i].bank;
        ch[12] = (uint8_t)(g_chip[i].load >> 8); ch[13] = (uint8_t)g_chip[i].load;
        ch[14] = (uint8_t)(g_chip[i].size >> 8); ch[15] = (uint8_t)g_chip[i].size;
        ok = f->write(pos, ch, 16);
        pos += 16;
        c->io->ddr_read(g_img_base + (uint32_t)g_chip[i].bank * BANK + (g_chip[i].load & 0x2000), buf, g_chip[i].size);
        ok = ok && f->write(pos, buf, g_chip[i].size);
        pos += g_chip[i].size;
    }
    ok = ok && f->sync();
    c->fs->close(f);
    if (!ok) {
        snprintf(msg, msglen, "%s: writing failed (medium full or gone)", path);
        return 1;
    }
    snprintf(msg, msglen, "Cartridge saved: %s (%u bytes)", path, (unsigned)pos);
    return 0;
}

// The image as it stands in DDR2: whole banks up to the highest the file has, read piece by piece (the .c64re's cartridge section).
uint32_t c64_cart_image_bytes(void)
{
    uint32_t top = 0;
    for (int i = 0; i < g_nchip; i++) {
        top = g_chip[i].bank + 1u > top ? g_chip[i].bank + 1u : top;
    }
    return c64_cart_active() ? top * BANK : 0;
}
void c64_cart_image_read(CoreCtx *c, uint32_t off, uint8_t *dst, uint32_t n)
{
    c->io->ddr_read(g_img_base + off, dst, n);
}
void c64_cart_image_write(CoreCtx *c, uint32_t off, const uint8_t *src, uint32_t n)
{
    if (c64_cart_active() && off < g_image_max && n <= g_image_max - off) {       // the file's image never goes past the window the cartridge was inserted with
        c->io->ddr_write(g_img_base + off, src, n);
        c->io->wr(C64_CART_CMD, C64_CART_CMD_INVAL);                                  // the cartridge's cached lines of the image go (cmd.inval): every write drops them, no caller does it (D50)
    }
}

// CONTROL EASYFLASH 0 (control_target.cc:223-246): the 8 banks of 8 KB that start at `bank` (the firmware's mask 0x38) in the ROML or ROMH half become $FF, as a sector erase of an
// EasyFlash does, and the change goes to the write-back service (C64_CRT::note_change). The firmware does not look where the image ends; here a bank outside the image is left alone.
bool c64_cart_ef_clear(CoreCtx *c, uint8_t bank, bool high)
{
    if (!c64_cart_active()) {
        return false;
    }
    static uint8_t ff[1024];
    if (ff[0] != 0xFF) {
        memset(ff, 0xFF, sizeof(ff));
    }
    for (uint32_t b = 0; b < 8; b++) {
        uint32_t off = ((uint32_t)bank + b) * BANK + (high ? 0x2000u : 0u);
        if (off + 0x2000 > g_image_max) {
            continue;
        }
        for (uint32_t k = 0; k < 0x2000; k += sizeof(ff)) {
            c->io->ddr_write(g_img_base + off + k, ff, sizeof(ff));
        }
        wb_mark(off, 0x2000);
    }
    return true;
}

int c64_cart_remove(CoreCtx *c, char *msg, int msglen)
{
    CoreIo *io = c->io;
    char fm[160];
    wb_flush(c, WB_WHY_EJECT, fm, sizeof(fm));                        // the file first, then the cartridge goes
    g_cart_name[0] = 0;
    g_cart_crt = -1;
    bool block = (io->rd(C64_CAPS) & C64_CAPS_CART) != 0;
    if (!g_for_state) {
        io->mode_reset(true);
    }
    set_type(io, block, 0);
    {
        OpIn v[1] = { in_int("value", 0) };                           // T228 R20 6: the REU, the sampler and the UCI of F5 are the machine's again
        OpResult r;
        ops_call(c, "c64.fx_io", v, 1, &r);
    }
    printf("[c64] cartridge removed\n");
    mount_notify_name(c, "Cartridge");                                // T180 D2
    return g_for_state ? 0 : boot_cold_reset(c, msg, msglen);
}

// A loaded state's cartridge (FW trx_c64re.cc build_cartridge): the .crt at `path` is inserted, or the port emptied for NULL, and nothing is reset - the state
// goes in next. 0, or a reason in msg.
int c64_cart_for_state(CoreCtx *c, const char *path, char *msg, int msglen)
{
    g_for_state = true;
    int rc = path ? c64_cart_run(c, path, msg, msglen) : c64_cart_remove(c, msg, msglen);
    g_for_state = false;
    return rc;
}
