/*
 * cart_writer.c
 *
 *  Flash .CRT files to physical flash cartridges via the U64 cart slot.
 *  Supported: EasyFlash (AM29F040 x2), EasyFlash 3 (MX29LV640EB, one slot
 *  at a time), GMod2 (AM29F040), MegaByter (MX29F800CB, 1MB), C64MegaCart
 *  (M29F160FT, 2MB).
 *  Port of the firmware CartWriter (software/io/c64/cart_writer.cc).
 */

#include "cart_writer.h"
#include "eapi_drivers.h"

// ---- Flash chip definitions ----

static const flash_chip_info_t megabyter_info = {
    "MegaByter",
    0x8AAA, 0x8555, // ROML unlock addrs
    0,      0,      // no ROMH
    FLASH_MODE_DE02, 0x03, 0x00,
    128,    1024 * 1024, 86, false
};

static const flash_chip_info_t megacart_info = {
    "C64MegaCart",
    0xEAAA, 0xE555, // through ROMH
    0,      0,      // single chip
    FLASH_MODE_DF00, 0xC0, 0x00,
    256,    2 * 1024 * 1024, 88, false // T33: 88 = C64MegaCart
};

static const flash_chip_info_t gmod2_info = {
    "GMod2",
    // AM29F040: chip addresses $5555/$2AAA, reached through the bank register (gmod2_flash_cycle)
    0x5555, 0x2AAA,
    0,      0,      // no ROMH chip
    FLASH_MODE_DE00, 0xC0, 0x00,
    64,     512 * 1024, 60, false
};

static const flash_chip_info_t easyflash_info = {
    "EasyFlash",
    // AM29F040: unlock addresses $555 and $2AA (from skoe's EAPI source)
    // Bank 0 must be selected. Only lower 11 bits matter for unlock.
    // ROML ($8000 base): $8000+$555=$8555, $8000+$2AA=$82AA
    0x8555, 0x82AA, // ROML chip unlock addresses
    // ROMH ($E000 base): $E000+$555=$E555, $E000+$2AA=$E2AA
    0xE555, 0xE2AA,                 // ROMH chip unlock addresses
    FLASH_MODE_DE02_EF, 0x05, 0x05, // $05=Ultimax (both flash and read mode)
    64,     1024 * 1024, 32, true
};

static const flash_chip_info_t ef3_info = {
    "EasyFlash 3",
    // MX29LV640EB: commands to $8AAA/$8555 in slot 0, bank 0, for ROML and ROMH alike (eapi-mx29640b.s)
    0x8AAA, 0x8555,
    0,      0,                      // ROML and ROMH are the same chip
    FLASH_MODE_DE02_EF, 0x05, 0x05, // $05=Ultimax (both flash and read mode)
    64,     1024 * 1024, 32, true   // one slot
};

static const uint8_t crt_signature[16] = { 0x43, 0x36, 0x34, 0x20, 0x43, 0x41, 0x52, 0x54,
                                           0x52, 0x49, 0x44, 0x47, 0x45, 0x20, 0x20, 0x20 };

// Scratch buffer for one 8K chip read back from the bus during verify
static uint8_t s_chip_buf[0x2000];

// GMod2 EEPROM contents from the CRT (load $DE00), all $FF when it has none
static uint8_t s_eeprom[GMOD2_EEPROM_SIZE];

static void draw_header(cart_writer_t *w);
static void draw_status(cart_writer_t *w, const char *msg);
static void draw_footer(cart_writer_t *w);
static void rebuild(void *ctx);
static void flash_reset_chips(cart_writer_t *w);

// Where the CRT comes from. On Gideon's core it is read from the stick once, when the writer opens on it, and written
// and verified from RAM: writing an EasyFlash 3 keeps the C64 bus busy for half a minute, and the USB stick has dropped
// off during that time (a writer still reading the file then failed half way, and the verify found no file at all).
// On our core, and wherever the whole CRT does not fit in the heap (1 MB of EasyFlash needs a contiguous 2 MB while
// it is read), the CRT is streamed: every pass over it (summary, validation, write, verify) opens the file and reads
// the CHIP packets in sequence, one bank in memory at a time.
typedef struct {
    const uint8_t *data; // memory: the whole CRT
    uint32_t size;
    uint32_t pos;
    void *fh; // stream: the open file, NULL for memory
    const char *path, *name; // stream: what crt_seek opens again to go back
} crt_reader_t;

static crt_reader_t s_reader;

// Starts reading the CRT from its first byte; NULL when it could not be opened
static crt_reader_t *crt_open(const cart_writer_t *w)
{
    s_reader.data = NULL;
    s_reader.size = 0;
    s_reader.pos = 0;
    s_reader.fh = NULL;
    s_reader.path = w->crt_path;
    s_reader.name = w->crt_filename;
    if (w->crt_data) {
        s_reader.data = w->crt_data;
        s_reader.size = w->crt_size;
        return &s_reader;
    }
    if (!w->crt_stream)
        return NULL;
    s_reader.fh = file_open(w->crt_path, w->crt_filename, "rb");
    return s_reader.fh ? &s_reader : NULL;
}

// The same reader on a file, for the check that runs before a writer exists
static crt_reader_t *crt_open_file(const char *path, const char *name)
{
    s_reader.data = NULL;
    s_reader.size = 0;
    s_reader.pos = 0;
    s_reader.path = path;
    s_reader.name = name;
    s_reader.fh = file_open(path, name, "rb");
    return s_reader.fh ? &s_reader : NULL;
}

static void crt_close(crt_reader_t *f)
{
    if (f && f->fh)
        file_close(f->fh);
    if (f)
        f->fh = NULL;
}

static int crt_read(crt_reader_t *f, void *buf, uint32_t len)
{
    if (f->fh) {
        uint32_t got = 0;
        while (got < len) {
            int n = file_read(f->fh, (uint8_t *)buf + got, (int)(len - got));
            if (n <= 0)
                break;
            got += (uint32_t)n;
        }
        f->pos += got;
        return (int)got;
    }
    uint32_t left = f->size - f->pos;
    if (len > left)
        len = left;
    memcpy(buf, f->data + f->pos, len);
    f->pos += len;
    return (int)len;
}

// Puts f at byte pos and says whether it is there. A stream never uses fseek: the firmware's _lseek has no SEEK_CUR
// (ult_syscalls.cc), and newlib asks for it when it flushes the read buffer of a file it has not seeked yet, so
// the fseek of such a file fails and leaves the position where it was (T57 2026-10-09). Forward is read and
// discarded, backward opens the file again.
static bool crt_seek(crt_reader_t *f, uint32_t pos)
{
    if (!f->fh) {
        bool ok = pos <= f->size;
        f->pos = ok ? pos : f->size;
        return ok;
    }
    if (pos < f->pos) {
        file_close(f->fh);
        f->fh = file_open(f->path, f->name, "rb");
        f->pos = 0;
        if (!f->fh)
            return false;
    }
    uint8_t skip[256];
    while (f->pos < pos) {
        uint32_t n = pos - f->pos;
        if (n > sizeof(skip))
            n = sizeof(skip);
        if ((uint32_t)crt_read(f, skip, n) != n)
            return false;
    }
    return true;
}

// Moves past len bytes of f; false when the file ends before
static bool skip_bytes(crt_reader_t *f, uint32_t skip) { return crt_seek(f, f->pos + skip); }

// Moves past the data of a CHIP packet and says whether all of it is there: the last byte of the data is read.
// The validation before the erase uses it, so a CRT that ends inside a packet is refused while the cartridge is
// still untouched.
static bool skip_packet_data(crt_reader_t *f, uint32_t len)
{
    if (len == 0)
        return true;
    if (!skip_bytes(f, len - 1))
        return false;
    uint8_t last;
    return crt_read(f, &last, 1) == 1;
}

// The next CHIP header: 1 = read into ch, 0 = the file ends here, -1 = something else is there (a cut header, bytes
// that are no CHIP). A walk that ends with -1 has not seen the whole file.
static int next_chip(crt_reader_t *f, uint8_t ch[0x10])
{
    int got = crt_read(f, ch, 0x10);
    if (got == 0)
        return 0;
    return (got == 0x10 && memcmp(ch, "CHIP", 4) == 0) ? 1 : -1;
}

// Reads the whole CRT file into w->crt_data; where that is not possible (no memory for it) or not wanted (our core)
// the writer streams the file instead. False only when the file cannot be opened.
static bool load_crt(cart_writer_t *w)
{
    void *f = file_open(w->crt_path, w->crt_filename, "rb");
    if (!f)
        return false;
    if (is_trx()) {
        file_close(f);
        w->crt_stream = true;
        LOG("[CartWriter] CRT is streamed from the file\n");
        return true;
    }
    uint32_t cap = 0x10000, size = 0;
    uint8_t *data = (uint8_t *)g_api->mem->malloc(cap);
    while (data) {
        if (size == cap) {
            uint8_t *more = (uint8_t *)g_api->mem->realloc(data, cap * 2);
            if (!more) {
                g_api->mem->free(data);
                data = NULL;
                break;
            }
            data = more;
            cap *= 2;
        }
        int got = file_read(f, data + size, (int)(cap - size));
        if (got <= 0)
            break;
        size += (uint32_t)got;
    }
    file_close(f);
    if (!data) {
        LOG("[CartWriter] No memory for the CRT, streaming it from the file\n");
        w->crt_stream = true;
        return true;
    }
    w->crt_data = data;
    w->crt_size = size;
    LOG("[CartWriter] CRT loaded: %d bytes\n", (int)size);
    return true;
}

// Reads the data of an EEPROM CHIP packet (load $DE00) into s_eeprom
static bool read_eeprom_packet(crt_reader_t *f, uint16_t size)
{
    uint16_t take = (size > GMOD2_EEPROM_SIZE) ? GMOD2_EEPROM_SIZE : size;
    if (crt_read(f, s_eeprom, take) != take)
        return false;
    return skip_bytes(f, size - take);
}

// EasyProg (flash.c) writes the driver for the flash it found over the EAPI of an EasyFlash CRT: 768 bytes at bank 0
// ROMH $1800, when they start with the signature "eapi". Without that an EasyFlash 3 slot keeps the AM29F040 driver
// and EAPIInit fails. romh is the ROMH half of bank 0.
static void replace_eapi(cart_writer_t *w, int bank, uint8_t *romh)
{
    static const uint8_t signature[4] = { 0x65, 0x61, 0x70, 0x69 };
    if (bank != 0 || w->crt_type != 32 || !w->chip->has_romh)
        return;
    if (memcmp(romh + EAPI_ROMH_OFFSET, signature, sizeof(signature)) != 0)
        return;
    memcpy(romh + EAPI_ROMH_OFFSET, w->base.is_ef3 ? eapi_mx29640b : eapi_am29f040, EAPI_DRIVER_SIZE);
    LOG("[CartWriter] EAPI of the CRT replaced by %s\n", w->base.is_ef3 ? "MX29LV640EB 1.2" : "Am/M29F040 V1.4");
}

static uint16_t eeprom_word(int address) { return (uint16_t)((s_eeprom[2 * address] << 8) | s_eeprom[2 * address + 1]); }

// =========================================================================
//  Flash cart detection
// =========================================================================

static const flash_chip_info_t *detect_flash_cart(cart_writer_t *w)
{
    if (!cart_detect_present(&w->base))
        return NULL;

    bool is_8k = (w->base.det_exrom == 0 && w->base.det_game == 1);
    bool is_ultimax = (w->base.det_exrom == 1 && w->base.det_game == 0);

    // EasyFlash first: the RAM at $DF00 is definitive and needs no banking. An EasyFlash 3 showing its system
    // area (slot 0) keeps the KERNALs in ROMH, so banks 0 and 1 read the same at $8000 and the banking test
    // below would drop the cartridge before it was ever identified.
    if (cart_detect_easyflash()) {
        // EasyFlash 3: slot register at $DE01
        if (cart_detect_ef3(&w->base)) {
            LOG("[CartWriter] EasyFlash 3 detected\n");
            return &ef3_info;
        }
        LOG("[CartWriter] EasyFlash detected\n");
        return &easyflash_info;
    }

    // Check $DE00 banking (the remaining flash carts all use it)
    uint8_t ref[256], buf[256];
    cart_switch_bank(0, 0);
    C64_READ(0x8000, ref, 256);
    cart_switch_bank(1, 0);
    C64_READ(0x8000, buf, 256);
    bool has_banking = (memcmp(ref, buf, 256) != 0);
    cart_switch_bank(0, 0);
    LOG("[CartWriter] $DE00 banking: %s, mode: %s\n", has_banking ? "YES" : "NO",
        is_8k ? "8K" : is_ultimax ? "Ultimax" : "other");
    if (!has_banking)
        return NULL;

    // Only 8K mode carts from here
    if (!is_8k) {
        LOG("[CartWriter] Not 8K mode — only EasyFlash supported for non-8K\n");
        return NULL;
    }

    int banks = cart_count_banks();

    // MegaByter ($DE02=$03 Ultimax) — needs ≥128 banks
    if (banks >= 128) {
        uint8_t e000_before[16], e000_after[16];
        C64_READ(0xE000, e000_before, 16);
        C64_POKE(0xDE02, 0x03);
        C64_READ(0xE000, e000_after, 16);
        C64_POKE(0xDE02, 0x00);
        if (memcmp(e000_before, e000_after, 16) != 0) {
            LOG("[CartWriter] MegaByter confirmed (%d banks)\n", banks);
            return &megabyter_info;
        }
    }

    // C64MegaCart ($DF00=$C0 Ultimax) — needs ≥256 banks
    if (banks >= 256) {
        uint8_t e000_before[16], e000_after[16];
        C64_READ(0xE000, e000_before, 16);
        C64_POKE(0xDF00, 0xC0);
        C64_READ(0xE000, e000_after, 16);
        C64_POKE(0xDF00, 0x00);
        if (memcmp(e000_before, e000_after, 16) != 0) {
            LOG("[CartWriter] C64MegaCart confirmed (%d banks)\n", banks);
            return &megacart_info;
        }
    }

    // GMod2 (EEPROM / high-bank test). The wrap count is 128 on a GMod2: $DE00=64 sets bit 6 and turns the ROM off,
    // $DE00=128 is bank 0 again.
    if (banks <= 128) {
        if (cart_detect_gmod2()) {
            LOG("[CartWriter] GMod2 confirmed\n");
            return &gmod2_info;
        }
    }

    LOG("[CartWriter] %d banks but no supported flash cart\n", banks);
    return NULL;
}

// =========================================================================
//  Flash programming primitives
// =========================================================================

static void enter_flash_mode(cart_writer_t *w)
{
    switch (w->chip->mode_type) {
    case FLASH_MODE_DE02:
        C64_POKE(0xDE02, w->chip->ultimax_val);
        break;
    case FLASH_MODE_DE02_EF:
        C64_POKE(0xDE02, w->chip->ultimax_val);
        break;
    case FLASH_MODE_DF00:
        C64_POKE(0xDF00, w->chip->ultimax_val);
        break;
    case FLASH_MODE_DE00:
        C64_POKE(0xDE00, w->chip->ultimax_val);
        break;
    }
}

static void exit_flash_mode(cart_writer_t *w)
{
    switch (w->chip->mode_type) {
    case FLASH_MODE_DE02:
        C64_POKE(0xDE02, w->chip->normal_val);
        break;
    case FLASH_MODE_DE02_EF:
        C64_POKE(0xDE02, w->chip->normal_val);
        break;
    case FLASH_MODE_DF00:
        C64_POKE(0xDF00, w->chip->normal_val);
        break;
    case FLASH_MODE_DE00:
        C64_POKE(0xDE00, w->chip->normal_val);
        break;
    }
}

static void flash_unlock(cart_writer_t *w)
{
    C64_POKE(w->chip->cmd_addr_1, 0xAA);
    C64_POKE(w->chip->cmd_addr_2, 0x55);
}

static bool flash_poll(uint16_t addr, uint8_t expected)
{
    for (int timeout = 0; timeout < 500000; timeout++) {
        uint8_t val = C64_PEEK(addr);
        if ((val & 0x80) == (expected & 0x80))
            return true;
        if (val & 0x20) {
            val = C64_PEEK(addr);
            if ((val & 0x80) == (expected & 0x80))
                return true;
            LOG("[CartWriter] Poll timeout at $%04X ($%02X vs $%02X)\n", addr, val, expected);
            return false;
        }
    }
    LOG("[CartWriter] Poll exhausted at $%04X\n", addr);
    return false;
}

// =========================================================================
//  GMod2 flash access
//
//  $DE00 holds the bank in bits 5:0 and the mode in bits 7:6: 11 Ultimax
//  with the flash writable, 0x 8K with the flash readable at $8000, 10 off.
//  Bit 6 is also the EEPROM chip select, bits 5 and 4 its clock and data in
//  (VICE gmod2.c). The flash only reads in 8K mode, so polling and verify
//  switch to $80|bank. Command cycles go to the AM29F040 chip addresses
//  $5555/$2AAA through the bank register and the $E000 window, where VICE
//  takes Ultimax writes to the flash; an AM29F040B decodes the low 11 bits
//  of the same addresses. A bank change in flash mode passes $80|bank first,
//  so the EEPROM is deselected while its clock and data bits change.
// =========================================================================

#define GMOD2_MODE_FLASH         0xC0 // Ultimax, flash writable (EEPROM selected)
#define GMOD2_MODE_READ          0x80 // 8K, flash readable, EEPROM deselected
#define GMOD2_PROGRAM_TIMEOUT_MS 50
#define GMOD2_ERASE_TIMEOUT_S    120

// Selects bank in flash mode, deselecting the EEPROM first
static void gmod2_flash_bank(int bank)
{
    C64_POKE(0xDE00, (uint8_t)(GMOD2_MODE_READ | bank));
    C64_POKE(0xDE00, (uint8_t)(GMOD2_MODE_FLASH | bank));
}

// One write cycle to a chip address: bank = address / 8K, offset through $E000
static void gmod2_flash_cycle(uint32_t chip_addr, uint8_t val)
{
    gmod2_flash_bank((int)(chip_addr >> 13));
    C64_POKE((uint16_t)(0xE000 | (chip_addr & 0x1FFF)), val);
}

// $AA to $5555, $55 to $2AAA, cmd to $5555
static void gmod2_flash_command(uint8_t cmd)
{
    gmod2_flash_cycle(0x5555, 0xAA);
    gmod2_flash_cycle(0x2AAA, 0x55);
    gmod2_flash_cycle(0x5555, cmd);
}

// Back to read mode after an error, then 8K bank 0
static void gmod2_flash_reset(void)
{
    gmod2_flash_cycle(0x0000, 0xF0);
    C64_POKE(0xDE00, 0x00);
}

// Reads the byte in 8K mode until it holds expected; DQ5 set on a byte that still differs is a failed program
static bool gmod2_flash_poll(int bank, uint16_t offset, uint8_t expected)
{
    C64_POKE(0xDE00, (uint8_t)(GMOD2_MODE_READ | bank));
    uint16_t addr = 0x8000 | offset;
    uint32_t start = g_api->sys->tick_ms();
    for (;;) {
        uint8_t val = C64_PEEK(addr);
        if (val == expected)
            return true;
        if (val & 0x20) {
            val = C64_PEEK(addr);
            if (val == expected)
                return true;
            LOG("[CartWriter] GMod2 program error bank %d $%04X: $%02X vs $%02X\n", bank, addr, val, expected);
            return false;
        }
        if ((g_api->sys->tick_ms() - start) > GMOD2_PROGRAM_TIMEOUT_MS) {
            LOG("[CartWriter] GMod2 program timeout bank %d $%04X: $%02X vs $%02X\n", bank, addr, val, expected);
            return false;
        }
    }
}

static FRESULT gmod2_chip_erase(cart_writer_t *w)
{
    const flash_chip_info_t *chip = w->chip;

    LOG("[CartWriter] === GMOD2 CHIP ERASE ===\n");
    draw_status(w, "Erasing flash...");
    memset(w->bank_status_lo, BANK_ERASING, chip->max_banks);
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);

    gmod2_flash_command(0x80);
    gmod2_flash_cycle(0x5555, 0xAA);
    gmod2_flash_cycle(0x2AAA, 0x55);
    gmod2_flash_cycle(0x5555, 0x10);

    // DQ7 reads 0 until the whole chip is erased
    C64_POKE(0xDE00, GMOD2_MODE_READ);
    uint32_t start = g_api->sys->tick_ms();
    uint32_t shown = 0;
    bool done = false;
    while (!done) {
        done = (C64_PEEK(0x8000) == 0xFF);
        uint32_t secs = (g_api->sys->tick_ms() - start) / 1000;
        if (!done && secs >= GMOD2_ERASE_TIMEOUT_S)
            break;
        if (secs != shown) {
            shown = secs;
            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Erasing flash... %ds", (int)secs);
            draw_status(w, msg);
        }
    }

    if (!done) {
        gmod2_flash_reset();
        memset(w->bank_status_lo, BANK_ERROR, chip->max_banks);
        cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
        LOG("[CartWriter] GMod2 ERASE FAILED\n");
        draw_status(w, "ERASE FAILED!");
        return FR_DISK_ERR;
    }

    C64_POKE(0xDE00, 0x00);
    LOG("[CartWriter] GMod2 erase done after %d ms\n", (int)(g_api->sys->tick_ms() - start));
    memset(w->bank_status_lo, BANK_EMPTY, chip->max_banks);
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_status(w, "Erase complete.");
    return FR_OK;
}

static FRESULT gmod2_write_bank(int bank, const uint8_t *data, uint16_t size)
{
    uint16_t len = (size > 0x2000) ? 0x2000 : size;
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] == 0xFF)
            continue;
        gmod2_flash_command(0xA0);
        gmod2_flash_cycle(((uint32_t)bank << 13) | i, data[i]);
        if (!gmod2_flash_poll(bank, i, data[i])) {
            gmod2_flash_reset();
            LOG("[CartWriter] FAIL bank %d offset $%04X\n", bank, i);
            return FR_DISK_ERR;
        }
    }
    C64_POKE(0xDE00, 0x00);
    LOG("[CartWriter] Bank %d: OK\n", bank);
    return FR_OK;
}

// Compares the EEPROM with s_eeprom
static FRESULT gmod2_verify_eeprom(cart_writer_t *w)
{
    draw_status(w, "Verifying EEPROM...");
    int errors = 0;
    for (int addr = 0; addr < GMOD2_EEPROM_WORDS; addr++) {
        uint16_t got = cart_gmod2_eeprom_read_word(addr);
        if (got != eeprom_word(addr)) {
            if (errors < 5)
                LOG("[CartWriter] EEPROM word %d: $%04X vs $%04X\n", addr, eeprom_word(addr), got);
            errors++;
        }
    }
    C64_POKE(0xDE00, 0x00);

    if (errors) {
        char msg[40];
        ui_snprintf(msg, sizeof(msg), "EEPROM VERIFY FAILED: %d words!", errors);
        draw_status(w, msg);
        return FR_DISK_ERR;
    }
    LOG("[CartWriter] EEPROM verified\n");
    return FR_OK;
}

// Puts s_eeprom (the CRT's EEPROM packet, all $FFFF without one) into the EEPROM: EWEN, read every word, write the
// ones that differ, EWDS, read back. Words are written one by one instead of ERAL/WRAL, which leave word 0 out in the
// U64 FPGA's EEPROM.
static FRESULT gmod2_write_eeprom(cart_writer_t *w)
{
    static uint16_t words[GMOD2_EEPROM_WORDS];
    LOG("[CartWriter] GMod2 EEPROM: %s\n", w->crt_has_eeprom ? "contents from the CRT" : "no packet, erase");

    cart_gmod2_eeprom_write_enable(true);

    draw_status(w, "Reading EEPROM...");
    for (int addr = 0; addr < GMOD2_EEPROM_WORDS; addr++)
        words[addr] = cart_gmod2_eeprom_read_word(addr);

    FRESULT res = FR_OK;
    int written = 0;
    for (int addr = 0; addr < GMOD2_EEPROM_WORDS && res == FR_OK; addr++) {
        if (words[addr] == eeprom_word(addr))
            continue;
        if ((written & 15) == 0) {
            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Writing EEPROM word %d...", addr);
            draw_status(w, msg);
        }
        if (cart_gmod2_eeprom_write_word(addr, eeprom_word(addr)))
            written++;
        else
            res = FR_DISK_ERR;
    }

    cart_gmod2_eeprom_write_enable(false);
    LOG("[CartWriter] EEPROM: %d words written\n", written);
    if (res != FR_OK) {
        draw_status(w, "EEPROM WRITE FAILED!");
        return res;
    }
    return gmod2_verify_eeprom(w);
}

// Reads addr for up to one second until it reads $FF (erase done); the last value read goes to *sample
static bool poll_erased_for_a_second(uint16_t addr, uint8_t *sample)
{
    uint32_t start = g_api->sys->tick_ms();
    do {
        *sample = C64_PEEK(addr);
        if (*sample == 0xFF)
            return true;
    } while ((g_api->sys->tick_ms() - start) < 1000);
    return false;
}

static FRESULT flash_chip_erase(cart_writer_t *w)
{
    const flash_chip_info_t *chip = w->chip;

    if (chip == &gmod2_info)
        return gmod2_chip_erase(w);

    // An EasyFlash 3 is erased by slot (ef3_erase_slot) or whole (ef3_erase_cart), never here
    if (w->base.is_ef3)
        return FR_DENIED;

    LOG("[CartWriter] === CHIP ERASE ===\n");
    draw_status(w, "Erasing flash...");
    for (int i = 0; i < chip->max_banks; i++) {
        w->bank_status_lo[i] = BANK_ERASING;
        if (chip->has_romh)
            w->bank_status_hi[i] = BANK_ERASING;
    }
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);

    enter_flash_mode(w);
    cart_switch_bank(0, 0); // unlock sequence requires bank 0

    // Erase ROML chip
    LOG("[CartWriter] Erasing ROML chip...\n");
    flash_unlock(w);
    C64_POKE(chip->cmd_addr_1, 0x80);
    flash_unlock(w);
    C64_POKE(chip->cmd_addr_1, 0x10);

    bool done = false;
    for (int sec = 0; sec < 120 && !done; sec++) {
        uint8_t sample = 0;
        done = poll_erased_for_a_second(chip->cmd_addr_1, &sample);
        if (sec < 5 || (sec % 10) == 0)
            LOG("[CartWriter] ROML erase: %ds, read=$%02X %s\n", sec + 1, sample, done ? "DONE" : "...");

        int erased = (sec * chip->max_banks) / 120;
        for (int b = 0; b < erased && b < chip->max_banks; b++)
            w->bank_status_lo[b] = BANK_EMPTY;
        cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);

        char msg[40];
        ui_snprintf(msg, sizeof(msg), "Erasing ROML... %ds", sec + 1);
        draw_status(w, msg);
    }

    if (!done) {
        exit_flash_mode(w);
        LOG("[CartWriter] ROML ERASE FAILED\n");
        draw_status(w, "ROML ERASE FAILED!");
        return FR_DISK_ERR;
    }

    // Erase ROMH chip (EasyFlash only — separate chip)
    if (chip->has_romh && chip->cmd_addr_h1) {
        LOG("[CartWriter] Erasing ROMH chip...\n");
        C64_POKE(chip->cmd_addr_h1, 0xAA);
        C64_POKE(chip->cmd_addr_h2, 0x55);
        C64_POKE(chip->cmd_addr_h1, 0x80);
        C64_POKE(chip->cmd_addr_h1, 0xAA);
        C64_POKE(chip->cmd_addr_h2, 0x55);
        C64_POKE(chip->cmd_addr_h1, 0x10);

        done = false;
        for (int sec = 0; sec < 120 && !done; sec++) {
            uint8_t sample = 0;
            done = poll_erased_for_a_second(chip->cmd_addr_h1, &sample);
            if (sec < 5 || (sec % 10) == 0)
                LOG("[CartWriter] ROMH erase: %ds, $%02X %s\n", sec + 1, sample, done ? "DONE" : "...");
            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Erasing ROMH... %ds", sec + 1);
            draw_status(w, msg);
        }
        if (!done) {
            exit_flash_mode(w);
            LOG("[CartWriter] ROMH ERASE FAILED\n");
            draw_status(w, "ROMH ERASE FAILED!");
            return FR_DISK_ERR;
        }
    }

    exit_flash_mode(w);

    cart_switch_bank(0, 0);
    uint8_t first[4];
    C64_READ(0x8000, first, 4);
    LOG("[CartWriter] Verify erase: %02X %02X %02X %02X\n", first[0], first[1], first[2], first[3]);

    for (int i = 0; i < chip->max_banks; i++) {
        w->bank_status_lo[i] = BANK_EMPTY;
        if (chip->has_romh)
            w->bank_status_hi[i] = BANK_EMPTY;
    }
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_status(w, "Erase complete.");
    return FR_OK;
}

static FRESULT flash_program_byte(cart_writer_t *w, uint16_t addr, uint8_t val)
{
    if (val == 0xFF)
        return FR_OK;

    // For EasyFlash: unlock sequence must run on bank 0,
    // then switch to target bank for the actual data write.
    // (From skoe's EAPI: "bank 0 must have been selected" for unlock)
    if (w->chip->mode_type == FLASH_MODE_DE02_EF) {
        cart_switch_bank(0, 0);
    }

    flash_unlock(w);
    C64_POKE(w->chip->cmd_addr_1, 0xA0); // program command

    // Switch to target bank before writing the data byte
    if (w->chip->mode_type == FLASH_MODE_DE02_EF) {
        cart_switch_bank(w->ef_current_bank, 0);
    }

    C64_POKE(addr, val);
    return flash_poll(addr, val) ? FR_OK : FR_DISK_ERR;
}

// =========================================================================
//  EasyFlash 3 flash access
//
//  One MX29LV640EB holds all 8 slots. The sequences follow EAPIWriteFlash and
//  EAPIEraseSector of skoe's eapi-mx29640b.s: the command cycles go to
//  $8AAA/$8555 with slot 0 and bank 0 selected, the last cycle goes to the
//  target slot and bank, and completion is polled by comparing the whole
//  byte. A 64K sector is 8 banks of either ROML or ROMH; the 8 boot sectors
//  at the bottom of the chip are 8K, ROML of the banks 0-7 of slot 0.
//
//  Slot 0 is the system area (KERNALs, menu, EF-Directory). Slot writes
//  (type 32 CRTs) change only the EF-Directory sector there. Slot 0 is
//  erased and programmed only inside a confirmed type 90 image write
//  (s_ef3_system), and the whole chip is erased only by ef3_erase_cart(),
//  after the two confirmations of confirm_destructive().
// =========================================================================

#define EF3_SECTOR_BANKS       8
#define EF3_PROGRAM_TIMEOUT_MS 50    // EAPI compares 20 times; a DMA read is slower than its 6502 loop
#define EF3_ERASE_TIMEOUT_MS   20000 // generous bound for one sector

// "EF-Name:" at bank 0, ROMH offset $1B00, followed by 16 PETSCII characters (EasyProg flash.c)
static const uint8_t ef_name_signature[8] = { 0x65, 0x66, 0x2D, 0x6E, 0x41, 0x4D, 0x45, 0x3A };

// Slot 0 may be erased and programmed: set only while a confirmed type 90 image is written, cleared on every way out
static bool s_ef3_system;

// True for flash the writer may change: slots 1-7; slot 0 only while s_ef3_system, else only the EF-Directory
// (bank $10, ROML, $200 bytes)
static bool ef3_target_allowed(int slot, int bank, uint16_t addr)
{
    if (slot < 0 || slot >= EF3_NUM_SLOTS || bank < 0 || bank >= 64)
        return false;
    if (slot == 0 && !s_ef3_system)
        return bank == EF3_DIR_BANK && addr >= 0x8000 && addr < 0x8200;
    return (addr >= 0x8000 && addr < 0xA000) || addr >= 0xE000;
}

// Command cycles 1 and 2, with slot 0 and bank 0 selected
static void ef3_unlock(void)
{
    C64_POKE(EF3_SLOT_REG, 0);
    C64_POKE(0xDE00, 0);
    C64_POKE(0x8AAA, 0xAA);
    C64_POKE(0x8555, 0x55);
}

// __EAPIResetFlash: back to read mode after a timeout, with the target slot and bank selected
static void ef3_reset_flash(int bank)
{
    C64_POKE(0xDE00, (uint8_t)bank);
    C64_POKE(0x8000, 0xF0);
}

// EAPI's cmpByte loop: true once addr reads expected. Reads at least min_reads times and for timeout_ms.
static bool ef3_poll(uint16_t addr, uint8_t expected, int min_reads, uint32_t timeout_ms)
{
    uint32_t start = g_api->sys->tick_ms();
    for (int reads = 1;; reads++) {
        if (C64_PEEK(addr) == expected)
            return true;
        if (reads >= min_reads && (g_api->sys->tick_ms() - start) > timeout_ms)
            return false;
    }
}

// EAPIWriteFlash: addr is $8000-$9FFF (ROML) or $E000-$FFFF (ROMH); needs Ultimax mode
static FRESULT ef3_program_byte(int slot, int bank, uint16_t addr, uint8_t val)
{
    if (val == 0xFF)
        return FR_OK;
    if (!ef3_target_allowed(slot, bank, addr)) {
        LOG("[CartWriter] EF3 write to slot %d bank %d $%04X refused\n", slot, bank, addr);
        return FR_DENIED;
    }

    ef3_unlock();
    C64_POKE(0x8AAA, 0xA0);
    C64_POKE(EF3_SLOT_REG, (uint8_t)slot);
    C64_POKE(0xDE00, (uint8_t)bank);
    C64_POKE(addr, val);
    if (ef3_poll(addr, val, 20, EF3_PROGRAM_TIMEOUT_MS))
        return FR_OK;

    LOG("[CartWriter] EF3 program timeout slot %d bank %d $%04X: $%02X vs $%02X\n", slot, bank, addr, val,
        C64_PEEK(addr));
    ef3_reset_flash(bank);
    return FR_DISK_ERR;
}

// EAPIEraseSector: erases the 64K ROML or ROMH sector that starts at bank (a multiple of 8); needs Ultimax mode
static FRESULT ef3_erase_sector(int slot, int bank, bool romh)
{
    uint16_t base = romh ? 0xE000 : 0x8000;
    if ((bank % EF3_SECTOR_BANKS) != 0 || !ef3_target_allowed(slot, bank, base)) {
        LOG("[CartWriter] EF3 erase of slot %d bank %d $%04X refused\n", slot, bank, base);
        return FR_DENIED;
    }

    ef3_unlock();
    C64_POKE(0x8AAA, 0x80);
    C64_POKE(0x8AAA, 0xAA);
    C64_POKE(0x8555, 0x55);
    C64_POKE(EF3_SLOT_REG, (uint8_t)slot);
    C64_POKE(0xDE00, (uint8_t)bank);
    C64_POKE(base, 0x30);

    // DQ7 reads 0 until the sector is erased, so the first byte cannot read $FF before
    if (ef3_poll(base, 0xFF, 1, EF3_ERASE_TIMEOUT_MS))
        return FR_OK;

    LOG("[CartWriter] EF3 erase timeout slot %d %s bank %d\n", slot, romh ? "ROMH" : "ROML", bank);
    ef3_reset_flash(bank);
    return FR_DISK_ERR;
}

// An 8 KiB boot sector of the MX29LV640EB: ROML of bank 0-7 of slot 0 (the eight KERNAL sectors); needs s_ef3_system
static FRESULT ef3_erase_boot_sector(int bank)
{
    if (!s_ef3_system || bank < 0 || bank >= EF3_SECTOR_BANKS) {
        LOG("[CartWriter] EF3 boot sector %d erase refused\n", bank);
        return FR_DENIED;
    }
    ef3_unlock();
    C64_POKE(0x8AAA, 0x80);
    C64_POKE(0x8AAA, 0xAA);
    C64_POKE(0x8555, 0x55);
    C64_POKE(EF3_SLOT_REG, 0);
    C64_POKE(0xDE00, (uint8_t)bank);
    C64_POKE(0x8000, 0x30);
    if (ef3_poll(0x8000, 0xFF, 1, EF3_ERASE_TIMEOUT_MS))
        return FR_OK;
    LOG("[CartWriter] EF3 boot sector %d erase timeout\n", bank);
    ef3_reset_flash(bank);
    return FR_DISK_ERR;
}

// Every sector of one slot, 0-7 (needs s_ef3_system for slot 0): ROML then ROMH, 64K sectors; in slot 0 the first eight
// ROML banks are eight boot sectors
static FRESULT ef3_erase_slot_any(cart_writer_t *w, int slot)
{
    FRESULT res = FR_OK;
    for (int romh = 0; romh < 2 && res == FR_OK; romh++) {
        uint8_t *status = romh ? w->bank_status_hi : w->bank_status_lo;
        for (int bank = 0; bank < 64 && res == FR_OK; bank += EF3_SECTOR_BANKS) {
            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Erasing slot %d %s %02X-%02X...", slot, romh ? "ROMH" : "ROML", bank,
                        bank + EF3_SECTOR_BANKS - 1);
            draw_status(w, msg);
            memset(status + bank, BANK_ERASING, EF3_SECTOR_BANKS);
            cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
            if (slot == 0 && bank == 0 && !romh) {
                for (int k = 0; k < EF3_SECTOR_BANKS && res == FR_OK; k++)
                    res = ef3_erase_boot_sector(k);
            } else {
                res = ef3_erase_sector(slot, bank, romh != 0);
            }
            memset(status + bank, (res == FR_OK) ? BANK_EMPTY : BANK_ERROR, EF3_SECTOR_BANKS);
            cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
        }
    }
    return res;
}

#define EF3_CHIP_ERASE_TIMEOUT_MS 300000 // the datasheet's chip erase time is well inside this

// The whole MX29LV640: chip erase ($AA $55 $80 $AA $55 $10 at $AAA/$555), all 8 MB, system area and every slot.
// Only called after confirm_destructive(). Done when slot 0 bank 0 reads $FF twice (DQ7 data poll), then a spot check:
// the first 16 bytes of both halves of all 512 banks.
static FRESULT ef3_erase_cart(cart_writer_t *w)
{
    LOG("[CartWriter] === EF3 WHOLE CART CHIP ERASE ===\n");
    memset(w->bank_status_lo, BANK_ERASING, sizeof(w->bank_status_lo));
    memset(w->bank_status_hi, BANK_ERASING, sizeof(w->bank_status_hi));
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_status(w, "Erasing the whole cart...");

    enter_flash_mode(w);
    ef3_unlock();
    C64_POKE(0x8AAA, 0x80);
    C64_POKE(0x8AAA, 0xAA);
    C64_POKE(0x8555, 0x55);
    C64_POKE(0x8AAA, 0x10);

    uint32_t start = g_api->sys->tick_ms(), shown = 0;
    int ff = 0;
    bool done = false;
    while (!done) {
        ff = (C64_PEEK(0x8000) == 0xFF) ? ff + 1 : 0;
        if (ff >= 2) {
            done = true;
            break;
        }
        uint32_t ms = g_api->sys->tick_ms() - start;
        if (ms > EF3_CHIP_ERASE_TIMEOUT_MS)
            break;
        if (ms / 1000 != shown) {
            shown = ms / 1000;
            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Erasing the whole cart... %ds", (int)shown);
            draw_status(w, msg);
        }
    }
    if (!done) {
        LOG("[CartWriter] EF3 chip erase timeout\n");
        ef3_reset_flash(0);
        exit_flash_mode(w);
        cart_ef3_select_slot(&w->base);
        memset(w->bank_status_lo, BANK_ERROR, sizeof(w->bank_status_lo));
        memset(w->bank_status_hi, BANK_ERROR, sizeof(w->bank_status_hi));
        draw_status(w, "WHOLE ERASE FAILED!");
        return FR_DISK_ERR;
    }

    // Spot check: the first bytes of every 8 KiB half
    int bad = 0;
    for (int slot = 0; slot < EF3_NUM_SLOTS; slot++) {
        C64_POKE(EF3_SLOT_REG, (uint8_t)slot);
        for (int bank = 0; bank < 64; bank++) {
            uint8_t a[16], b[16];
            C64_POKE(0xDE00, (uint8_t)bank);
            C64_READ(0x8000, a, sizeof(a));
            C64_READ(0xE000, b, sizeof(b));
            for (int i = 0; i < 16; i++)
                if (a[i] != 0xFF || b[i] != 0xFF)
                    bad++;
        }
    }
    exit_flash_mode(w);
    cart_ef3_select_slot(&w->base);
    memset(w->bank_status_lo, bad ? BANK_ERROR : BANK_EMPTY, sizeof(w->bank_status_lo));
    memset(w->bank_status_hi, bad ? BANK_ERROR : BANK_EMPTY, sizeof(w->bank_status_hi));
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    if (bad) {
        LOG("[CartWriter] EF3 chip erase: %d bytes not $FF in the spot check\n", bad);
        draw_status(w, "ERASE CHECK FAILED!");
        return FR_DISK_ERR;
    }
    LOG("[CartWriter] EF3 chip erase done after %d ms\n", (int)(g_api->sys->tick_ms() - start));
    draw_status(w, "Whole cart erased.");
    return FR_OK;
}

// eraseSlot() of EasyProg: every 64K sector of the picked slot, ROML first, then ROMH
static FRESULT ef3_erase_slot(cart_writer_t *w)
{
    const flash_chip_info_t *chip = w->chip;
    int slot = w->base.ef3_slot;
    if (slot < 1 || slot >= EF3_NUM_SLOTS)
        return FR_DENIED;

    LOG("[CartWriter] === EF3 SLOT %d SECTOR ERASE ===\n", slot);
    FRESULT res = FR_OK;
    enter_flash_mode(w);
    for (int romh = 0; romh < 2 && res == FR_OK; romh++) {
        uint8_t *status = romh ? w->bank_status_hi : w->bank_status_lo;
        for (int bank = 0; bank < chip->max_banks && res == FR_OK; bank += EF3_SECTOR_BANKS) {
            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Erasing slot %d %s %02X-%02X...", slot, romh ? "ROMH" : "ROML", bank,
                        bank + EF3_SECTOR_BANKS - 1);
            draw_status(w, msg);
            memset(status + bank, BANK_ERASING, EF3_SECTOR_BANKS);
            cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);

            res = ef3_erase_sector(slot, bank, romh);
            memset(status + bank, (res == FR_OK) ? BANK_EMPTY : BANK_ERROR, EF3_SECTOR_BANKS);
            cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
        }
    }
    exit_flash_mode(w);
    cart_ef3_select_slot(&w->base);

    draw_status(w, (res == FR_OK) ? "Erase complete." : "SLOT ERASE FAILED!");
    return res;
}

// Programs one 16K EasyFlash bank (ROML + ROMH) of a slot
static FRESULT ef3_write_bank_slot(cart_writer_t *w, int slot, int bank, const uint8_t *data, uint16_t size)
{
    enter_flash_mode(w);
    FRESULT res = FR_OK;
    for (int romh = 0; romh < 2 && res == FR_OK; romh++) {
        uint16_t offset = romh ? 0x2000 : 0;
        uint16_t base = romh ? 0xE000 : 0x8000;
        uint16_t len = (size > offset) ? size - offset : 0;
        if (len > 0x2000)
            len = 0x2000;
        for (uint16_t i = 0; i < len && res == FR_OK; i++) {
            res = ef3_program_byte(slot, bank, base + i, data[offset + i]);
            if (res != FR_OK)
                LOG("[CartWriter] FAIL slot %d bank %d %s offset $%04X\n", slot, bank, romh ? "ROMH" : "ROML", i);
        }
    }
    exit_flash_mode(w);
    cart_ef3_select_slot(&w->base);

    if (res == FR_OK)
        LOG("[CartWriter] Slot %d bank %d: OK\n", slot, bank);
    return res;
}

// Writes one 16K EasyFlash bank into the picked slot
static FRESULT ef3_write_bank(cart_writer_t *w, int bank, const uint8_t *data, uint16_t size)
{
    if (bank >= w->chip->max_banks) {
        LOG("[CartWriter] Bank %d is outside an EasyFlash 3 slot\n", bank);
        return FR_INVALID_PARAMETER;
    }

    // EasyProg offers the "EF-Name:" of bank 0 ROMH as the slot name
    if (bank == 0 && size >= 0x4000 && memcmp(data + 0x3B00, ef_name_signature, sizeof(ef_name_signature)) == 0) {
        cart_petscii_to_ascii(w->ef_name, data + 0x3B00 + sizeof(ef_name_signature), EF3_NAME_LEN);
        LOG("[CartWriter] EF-Name: '%s'\n", w->ef_name);
    }
    return ef3_write_bank_slot(w, w->base.ef3_slot, bank, data, size);
}

// slotSaveName() of EasyProg (slots.c): reads the EF-Directory (default names when the signature is missing, slot 0
// always "System Area"), sets the name of the picked slot, erases the 64K sector holding the directory (slot 0, ROML
// banks $10-$17) and writes the directory back. EasyProg computes no checksum: the word is kept as read.
static FRESULT ef3_save_slot_name(cart_writer_t *w, const char *name)
{
    int slot = w->base.ef3_slot;
    if (slot < 1 || slot >= EF3_NUM_SLOTS)
        return FR_DENIED;

    uint8_t dir[EF3_DIR_SIZE];
    if (!cart_ef3_read_dir(&w->base, dir)) {
        char def[EF3_NAME_LEN + 1];
        cart_ascii_to_petscii(dir, "EF-Directory V1:", EF3_DIR_SIGN_LEN);
        for (int s = 1; s < EF3_DIR_SLOTS; s++) {
            ui_snprintf(def, sizeof(def), "Slot %d", s);
            cart_ascii_to_petscii(dir + EF3_DIR_SIGN_LEN + s * EF3_NAME_LEN, def, EF3_NAME_LEN);
        }
        for (int k = 0; k < EF3_DIR_KERNALS; k++) {
            ui_snprintf(def, sizeof(def), "KERNAL %d", k + 1);
            cart_ascii_to_petscii(dir + EF3_DIR_SIGN_LEN + (EF3_DIR_SLOTS + k) * EF3_NAME_LEN, def, EF3_NAME_LEN);
        }
    }
    cart_ascii_to_petscii(dir + EF3_DIR_SIGN_LEN, "System Area", EF3_NAME_LEN);
    cart_ascii_to_petscii(dir + EF3_DIR_SIGN_LEN + slot * EF3_NAME_LEN, name, EF3_NAME_LEN);

    // EasyProg writes two 256-byte blocks; $FF behind the directory is left erased
    uint8_t block[0x200];
    memset(block, 0xFF, sizeof(block));
    memcpy(block, dir, EF3_DIR_SIZE);

    LOG("[CartWriter] EF-Directory: slot %d = '%s'\n", slot, name);
    enter_flash_mode(w);
    FRESULT res = ef3_erase_sector(0, EF3_DIR_BANK, false);
    for (int i = 0; i < (int)sizeof(block) && res == FR_OK; i++) {
        res = ef3_program_byte(0, EF3_DIR_BANK, (uint16_t)(0x8000 + i), block[i]);
    }
    exit_flash_mode(w);

    uint8_t back[EF3_DIR_SIZE];
    if (res == FR_OK && (!cart_ef3_read_dir(&w->base, back) || memcmp(back, dir, EF3_DIR_SIZE) != 0)) {
        LOG("[CartWriter] EF-Directory read back differs\n");
        res = FR_DISK_ERR;
    }
    cart_ef3_select_slot(&w->base);
    return res;
}

// checkWriteCRTImage() of EasyProg: asks for the cartridge name after a successful write, offering the CRT's EF-Name
// (the CRT header name when it has none), and saves it in the EF-Directory
static FRESULT ef3_name_slot(cart_writer_t *w)
{
    char name[EF3_NAME_LEN + 1];
    strncpy(name, w->ef_name[0] ? w->ef_name : w->crt_name, EF3_NAME_LEN);
    name[EF3_NAME_LEN] = 0;
    for (int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; i--)
        name[i] = 0;

    // The string box edits in place, also when cancelled: a cancel keeps the offered name
    char edit[32];
    strcpy(edit, name);
    if (u2p_ui_string_input("Cartridge Name", edit, EF3_NAME_LEN) > 0) {
        strncpy(name, edit, EF3_NAME_LEN);
        name[EF3_NAME_LEN] = 0;
    }

    draw_status(w, "Writing EF-Directory...");
    // The directory sector is the only flash this writes outside do_write_crt: the guard is open for it alone
    if (!slot_wr_open()) {
        cart_notify("Cannot unlock cartridge writes.\nNothing was written.");
        return FR_DENIED;
    }
    FRESULT res = ef3_save_slot_name(w, name);
    if (res != FR_OK)
        flash_reset_chips(w);
    slot_wr_close();
    if (res == FR_OK) {
        strcpy(w->base.ef3_name, name);
        draw_header(w);
    }
    return res;
}

static FRESULT flash_write_bank(cart_writer_t *w, int bank, const uint8_t *data, uint16_t size)
{
    const flash_chip_info_t *chip = w->chip;

    if (w->base.is_ef3)
        return ef3_write_bank(w, bank, data, size);
    if (chip == &gmod2_info)
        return gmod2_write_bank(bank, data, size);

    w->ef_current_bank = bank;
    cart_switch_bank(bank, 0);
    enter_flash_mode(w);

    // ROML chip: data at $8000 (MegaByter, GMod2, EF) or $E000 (MegaCart)
    uint16_t roml_base = (chip->mode_type == FLASH_MODE_DF00) ? 0xE000 : 0x8000;
    uint16_t roml_size = (size > 0x2000) ? 0x2000 : size;

    int non_ff = 0;
    for (uint16_t i = 0; i < roml_size; i++)
        if (data[i] != 0xFF)
            non_ff++;
    LOG("[CartWriter] Bank %d ROML: %d/%d bytes at $%04X\n", bank, non_ff, roml_size, roml_base);

    for (uint16_t i = 0; i < roml_size; i++) {
        FRESULT res = flash_program_byte(w, roml_base + i, data[i]);
        if (res != FR_OK) {
            exit_flash_mode(w);
            LOG("[CartWriter] FAIL bank %d ROML offset $%04X\n", bank, i);
            return res;
        }
    }

    // ROMH chip (EasyFlash only): second 8K at $E000, uses separate unlock addrs
    if (chip->has_romh && size > 0x2000) {
        const uint8_t *romh_data = data + 0x2000;
        uint16_t romh_size = size - 0x2000;
        if (romh_size > 0x2000)
            romh_size = 0x2000;

        non_ff = 0;
        for (uint16_t i = 0; i < romh_size; i++)
            if (romh_data[i] != 0xFF)
                non_ff++;
        LOG("[CartWriter] Bank %d ROMH: %d/%d bytes at $E000\n", bank, non_ff, romh_size);

        // For ROMH, use the ROMH unlock addresses
        for (uint16_t i = 0; i < romh_size; i++) {
            if (romh_data[i] == 0xFF)
                continue;
            // ROMH unlock sequence
            C64_POKE(chip->cmd_addr_h1, 0xAA);
            C64_POKE(chip->cmd_addr_h2, 0x55);
            C64_POKE(chip->cmd_addr_h1, 0xA0);
            C64_POKE(0xE000 + i, romh_data[i]);
            if (!flash_poll(0xE000 + i, romh_data[i])) {
                exit_flash_mode(w);
                LOG("[CartWriter] FAIL bank %d ROMH offset $%04X\n", bank, i);
                return FR_DISK_ERR;
            }
        }
    }

    exit_flash_mode(w);
    LOG("[CartWriter] Bank %d: OK\n", bank);
    return FR_OK;
}

static FRESULT flash_verify_bank(cart_writer_t *w, int bank, const uint8_t *data, uint16_t size)
{
    const flash_chip_info_t *chip = w->chip;

    cart_ef3_select_slot(&w->base); // EasyFlash 3: the picked slot

    // For EasyFlash: use Ultimax ($DE02=$05) for verify.
    // ROML at $8000, ROMH at $E000 — no PLA/BASIC ROM conflicts.
    if (chip->mode_type == FLASH_MODE_DE02_EF) {
        C64_POKE(0xDE02, 0x05); // Ultimax: ROML@$8000, ROMH@$E000
        for (volatile int d = 0; d < 100; d++) {
        }
    }
    cart_switch_bank(bank, 0);
    for (volatile int d = 0; d < 100; d++) {
    }

    // Debug: log first bytes actually read
    if (bank < 2) {
        uint8_t dbg[4];
        C64_READ(0x8000, dbg, 4);
        LOG("[CartWriter] Verify bank %d: $8000=%02X %02X %02X %02X  $DE02=%02X\n", bank, dbg[0], dbg[1], dbg[2],
            dbg[3], C64_PEEK(0xDE02));
    }

    // Verify ROML ($8000)
    uint16_t roml_size = (size > 0x2000) ? 0x2000 : size;
    int mismatches = 0;
    C64_READ(0x8000, s_chip_buf, roml_size);
    for (uint16_t i = 0; i < roml_size; i++) {
        if (s_chip_buf[i] != data[i]) {
            if (mismatches < 5)
                LOG("[CartWriter] Verify FAIL bank %d ROML+$%04X: $%02X vs $%02X\n", bank, i, data[i],
                    s_chip_buf[i]);
            mismatches++;
        }
    }

    // Verify ROMH ($E000 in Ultimax) if EasyFlash with >8K data
    if (chip->has_romh && size > 0x2000) {
        uint16_t romh_size = size - 0x2000;
        C64_READ(0xE000, s_chip_buf, romh_size);
        for (uint16_t i = 0; i < romh_size; i++) {
            if (s_chip_buf[i] != data[0x2000 + i]) {
                if (mismatches < 5)
                    LOG("[CartWriter] Verify FAIL bank %d ROMH+$%04X: $%02X vs $%02X\n", bank, i, data[0x2000 + i],
                        s_chip_buf[i]);
                mismatches++;
            }
        }
    }

    // Restore to cart-off after verify
    if (chip->mode_type == FLASH_MODE_DE02_EF) {
        C64_POKE(0xDE02, chip->normal_val);
    }

    if (mismatches) {
        LOG("[CartWriter] Bank %d: %d mismatches\n", bank, mismatches);
        return FR_DISK_ERR;
    }
    return FR_OK;
}

// =========================================================================
//  Write guard (T57 5.4): ROM_WR_EN is up only while the writer programs
// =========================================================================

// Back to read mode on every chip the writer can have left in command state. Only after a failed operation, and
// only on our core, where the write lock closes right behind it: a chip left in command state would answer reads
// with status bytes until the next reset.
static void flash_reset_chips(cart_writer_t *w)
{
    if (!is_trx() || !w->chip)
        return;
    if (w->chip == &gmod2_info) {
        gmod2_flash_reset();
        return;
    }
    enter_flash_mode(w);
    if (w->base.is_ef3)
        C64_POKE(EF3_SLOT_REG, 0);
    C64_POKE(0xDE00, 0);
    C64_POKE(w->chip->mode_type == FLASH_MODE_DF00 ? 0xE000 : 0x8000, 0xF0);
    if (w->chip->has_romh)
        C64_POKE(0xE000, 0xF0);
    exit_flash_mode(w);
    cart_ef3_select_slot(&w->base);
}

static FRESULT do_write_crt(cart_writer_t *w);

// On Gideon's core there is no lock and this is do_write_crt. On ours the lock opens before the first ROML/ROMH
// write and closes on every way out; a failure puts the chips back to read mode first.
static FRESULT writer_guarded(cart_writer_t *w)
{
    if (!is_trx())
        return do_write_crt(w);
    if (!slot_wr_open()) {
        cart_notify("Cannot unlock cartridge writes.\nNothing was written.");
        return FR_DENIED;
    }
    FRESULT res = do_write_crt(w);
    if (res != FR_OK)
        flash_reset_chips(w);
    slot_wr_close();
    return res;
}

// =========================================================================
//  CRT operations
// =========================================================================

// Marks one bank as being written, programs it and records the result
static FRESULT write_bank_with_map(cart_writer_t *w, int bank, const uint8_t *data, uint16_t size, const char *msg,
                                   int *banks_written)
{
    const flash_chip_info_t *chip = w->chip;

    w->bank_status_lo[bank] = BANK_WRITING;
    if (chip->has_romh)
        w->bank_status_hi[bank] = BANK_WRITING;
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_status(w, msg);

    FRESULT res = flash_write_bank(w, bank, data, size);
    uint8_t st = (res == FR_OK) ? BANK_FILLED : BANK_ERROR;
    w->bank_status_lo[bank] = st;
    if (chip->has_romh)
        w->bank_status_hi[bank] = st;
    if (res == FR_OK)
        (*banks_written)++;
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    return res;
}

static FRESULT do_write_crt_from(cart_writer_t *w, crt_reader_t *f)
{
    const flash_chip_info_t *chip = w->chip;
    w->ef_name[0] = 0;

    uint8_t hdr[0x40];
    if (crt_read(f, hdr, 0x40) != 0x40 || memcmp(hdr, crt_signature, 16) != 0)
        return FR_DISK_ERR;
    uint32_t first_chip = cart_get_dword(hdr + 0x10);
    if (!crt_seek(f, first_chip))
        return FR_DISK_ERR;

    // Validate: scan CHIP packets for ROMH data (load addr != $8000).
    // MegaByter/C64MegaCart/GMod2 only have ROML — no ROMH chip.
    int packets = 0; // every CHIP packet of the file, EEPROM included: the write below must get through as many
    {
        bool has_romh = false;
        int chip_count = 0;
        while (true) {
            uint8_t ch[0x10];
            int more = next_chip(f, ch);
            if (more < 0) {
                LOG("[CartWriter] CRT has bytes that are no CHIP packet after %d packets\n", packets);
                cart_notify("CRT is not a clean list of CHIP\npackets. Nothing was erased.");
                return FR_DISK_ERR;
            }
            if (!more)
                break;
            packets++;
            uint16_t load = cart_get_word(ch + 0x0C);
            uint32_t pkt_len = cart_get_dword(ch + 0x04);
            // GMod2 EEPROM contents (load $DE00) are no ROMH
            if (load != 0x8000 && load != 0xDE00)
                has_romh = true;
            if (load != 0xDE00)
                chip_count++;
            // Skip the data; a packet that is not all there refuses the CRT before anything is erased
            if (pkt_len < 0x10 || !skip_packet_data(f, pkt_len - 0x10)) {
                LOG("[CartWriter] CRT ends inside a CHIP packet\n");
                return FR_DISK_ERR;
            }
        }
        if (chip_count == 0 || !crt_seek(f, first_chip)) {
            LOG("[CartWriter] CRT has no chips to write\n");
            cart_notify("CRT has no chips to write.\nNothing was erased.");
            return FR_DISK_ERR;
        }

        if (has_romh && !chip->has_romh) {
            LOG("[CartWriter] CRT contains ROMH data — not compatible with %s\n", chip->name);
            return FR_INVALID_PARAMETER;
        }
        LOG("[CartWriter] CRT validated: %d chips%s\n", chip_count, has_romh ? " (ROML+ROMH)" : " (ROML only)");
    }

    // EasyFlash 3: only the sectors of the picked slot
    FRESULT res = w->base.is_ef3 ? ef3_erase_slot(w) : flash_chip_erase(w);
    if (res != FR_OK)
        return res;

    // For EasyFlash: 16KB per bank (ROML 8K + ROMH 8K)
    // For others: 8KB per bank (ROML only)
    int buf_size = chip->has_romh ? 0x4000 : 0x2000;
    uint8_t *bank_buf = (uint8_t *)g_api->mem->malloc(buf_size);
    if (!bank_buf)
        return FR_NOT_ENOUGH_CORE;
    memset(bank_buf, 0xFF, buf_size);
    memset(s_eeprom, 0xFF, sizeof(s_eeprom));

    int banks_written = 0;
    int last_bank = -1;
    int done_packets = 0;
    bool clean_end = false;
    res = FR_OK;

    while (res == FR_OK) {
        uint8_t ch[0x10];
        int more = next_chip(f, ch);
        if (!more)
            clean_end = true;
        if (more <= 0)
            break;
        done_packets++;

        uint16_t bank = cart_get_word(ch + 0x0A);
        uint16_t load = cart_get_word(ch + 0x0C);
        uint16_t size = cart_get_word(ch + 0x0E);
        if (size > 0x2000)
            size = 0x2000;

        // EEPROM data packets (GMod2: load=$DE00) are written after the flash
        if (load == 0xDE00) {
            if (!read_eeprom_packet(f, size)) {
                res = FR_DISK_ERR;
                break;
            }
            continue;
        }

        if (chip->has_romh) {
            // EasyFlash: ROML ($8000) and ROMH ($A000/$E000) may come as
            // separate CHIP packets. Not every bank has both — some CRTs
            // only include ROMH for banks that need it.

            // If a packet of another bank arrives while the previous bank waits for its ROMH, flush it first. A
            // ROMH-only bank after a ROML-only bank (banks 37 and 63 of a test CRT) otherwise merged into one.
            if (last_bank >= 0 && bank != last_bank) {
                char msg[40];
                ui_snprintf(msg, sizeof(msg), "Bank %d/%d", last_bank, chip->max_banks);
                res = write_bank_with_map(w, last_bank, bank_buf, 0x4000, msg, &banks_written);
                memset(bank_buf, 0xFF, buf_size);
                last_bank = -1;
                if (res != FR_OK)
                    break;
            }

            // Load this CHIP packet into the appropriate half
            int offset = (load == 0x8000) ? 0 : 0x2000;
            if (crt_read(f, bank_buf + offset, size) != size) {
                res = FR_DISK_ERR;
                break;
            }
            if (offset == 0x2000)
                replace_eapi(w, bank, bank_buf + 0x2000);

            // If this is ROMH, the bank is complete — flush immediately
            if (offset == 0x2000) {
                char msg[40];
                ui_snprintf(msg, sizeof(msg), "Bank %d/%d", bank, chip->max_banks);
                res = write_bank_with_map(w, bank, bank_buf, 0x4000, msg, &banks_written);
                memset(bank_buf, 0xFF, buf_size);
                last_bank = -1; // no pending data
            } else {
                last_bank = bank; // ROML loaded, waiting for ROMH or next bank
            }
        } else {
            // Non-EasyFlash: one CHIP packet = one bank
            if (crt_read(f, bank_buf, size) != size) {
                res = FR_DISK_ERR;
                break;
            }

            char msg[40];
            ui_snprintf(msg, sizeof(msg), "Bank %d/%d ($%04X)", bank, w->crt_num_chips, load);
            res = write_bank_with_map(w, bank, bank_buf, size, msg, &banks_written);
        }
    }

    // Flush last pending EasyFlash bank (ROML without following ROMH)
    if (res == FR_OK && chip->has_romh && last_bank >= 0) {
        char msg[40];
        ui_snprintf(msg, sizeof(msg), "Bank %d/%d (final)", last_bank, chip->max_banks);
        res = write_bank_with_map(w, last_bank, bank_buf, 0x4000, msg, &banks_written);
    }

    g_api->mem->free(bank_buf);

    // The validation walked the whole file; a write that stopped anywhere else has not programmed the CRT, however
    // quietly it stopped (a reader that lost its place once ended it after the first packet and reported success)
    if (res == FR_OK && (!clean_end || done_packets != packets || banks_written == 0)) {
        LOG("[CartWriter] write ended after %d of %d packets, %d banks\n", done_packets, packets, banks_written);
        draw_status(w, "WRITE INCOMPLETE: CRT not read to its end!");
        res = FR_DISK_ERR;
    }

    if (res == FR_OK) {
        char msg[40];
        ui_snprintf(msg, sizeof(msg), "%d banks written.", banks_written);
        draw_status(w, msg);
    }
    return res;
}

static FRESULT do_write_crt(cart_writer_t *w)
{
    crt_reader_t *f = crt_open(w);
    if (!f)
        return FR_DISK_ERR;
    FRESULT res = do_write_crt_from(w, f);
    crt_close(f);
    return res;
}

// Verifies one bank against the buffer and records the result in the map
static void verify_bank_with_map(cart_writer_t *w, int bank, const uint8_t *data, uint16_t size, int *verified,
                                 int *errors)
{
    const flash_chip_info_t *chip = w->chip;

    char msg[40];
    ui_snprintf(msg, sizeof(msg), "Verify bank %d...", bank);
    draw_status(w, msg);

    uint8_t st;
    if (flash_verify_bank(w, bank, data, size) == FR_OK) {
        st = BANK_VERIFIED;
        (*verified)++;
    } else {
        st = BANK_ERROR;
        (*errors)++;
    }
    w->bank_status_lo[bank] = st;
    if (chip->has_romh)
        w->bank_status_hi[bank] = st;
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
}

static FRESULT do_verify_crt_from(cart_writer_t *w, crt_reader_t *f)
{
    const flash_chip_info_t *chip = w->chip;

    uint8_t hdr[0x40];
    if (crt_read(f, hdr, 0x40) != 0x40 || memcmp(hdr, crt_signature, 16) != 0 ||
        !crt_seek(f, cart_get_dword(hdr + 0x10)))
        return FR_DISK_ERR;

    // For EasyFlash: collect ROML+ROMH into 16K buffer (same as write)
    int buf_size = chip->has_romh ? 0x4000 : 0x2000;
    uint8_t *bank_buf = (uint8_t *)g_api->mem->malloc(buf_size);
    if (!bank_buf)
        return FR_NOT_ENOUGH_CORE;
    // A bank the CRT has only one half of is compared against erased flash in the other half, as the write left it
    memset(bank_buf, 0xFF, buf_size);

    int errors = 0, verified = 0;
    int last_bank = -1;
    memset(s_eeprom, 0xFF, sizeof(s_eeprom));

    while (true) {
        uint8_t ch[0x10];
        int more = next_chip(f, ch);
        if (more < 0)
            errors++; // bytes that are no CHIP packet: the rest of the CRT was not compared
        if (more <= 0)
            break;

        uint16_t bank = cart_get_word(ch + 0x0A);
        uint16_t load = cart_get_word(ch + 0x0C);
        uint16_t size = cart_get_word(ch + 0x0E);
        if (size > 0x2000)
            size = 0x2000;

        // EEPROM data (GMod2: load=$DE00) is no bank; kept for the EEPROM verify
        if (load == 0xDE00) {
            if (!read_eeprom_packet(f, size))
                errors++;
            continue;
        }

        if (chip->has_romh) {
            // EasyFlash: pair ROML+ROMH packets into 16K buffer

            // A packet of another bank arrives — verify the pending previous bank first
            if (last_bank >= 0 && bank != last_bank) {
                verify_bank_with_map(w, last_bank, bank_buf, buf_size, &verified, &errors);
                memset(bank_buf, 0xFF, buf_size);
            }

            // Load CHIP data into correct half
            int offset = (load == 0x8000) ? 0 : 0x2000;
            if (crt_read(f, bank_buf + offset, size) != size)
                errors++;
            if (offset == 0x2000)
                replace_eapi(w, bank, bank_buf + 0x2000); // compare with what the write put there

            if (offset == 0x2000) {
                // ROMH received — bank complete, verify now
                verify_bank_with_map(w, bank, bank_buf, buf_size, &verified, &errors);
                memset(bank_buf, 0xFF, buf_size);
                last_bank = -1;
            } else {
                last_bank = bank;
            }
        } else {
            // Non-EF: one CHIP packet = one bank
            if (crt_read(f, bank_buf, size) != size)
                errors++;
            verify_bank_with_map(w, bank, bank_buf, size, &verified, &errors);
        }
    }

    // Flush last pending EF bank (ROML without ROMH)
    if (chip->has_romh && last_bank >= 0) {
        verify_bank_with_map(w, last_bank, bank_buf, buf_size, &verified, &errors);
    }

    g_api->mem->free(bank_buf);

    if (verified == 0)
        errors++; // nothing was compared: no verify
    char msg[60];
    if (errors == 0)
        ui_snprintf(msg, sizeof(msg), "Verify OK: %d banks.", verified);
    else
        ui_snprintf(msg, sizeof(msg), "VERIFY FAILED: %d OK, %d errors!", verified, errors);
    draw_status(w, msg);
    return (errors == 0) ? FR_OK : FR_DISK_ERR;
}

static FRESULT do_verify_crt(cart_writer_t *w)
{
    crt_reader_t *f = crt_open(w);
    if (!f)
        return FR_DISK_ERR;
    FRESULT res = do_verify_crt_from(w, f);
    crt_close(f);
    return res;
}

// =========================================================================
//  EasyFlash 3 whole image (CRT type 90): slot-major CHIP packets, bank field = slot * 64 + bank. Written to the real
//  cart as it is: the slots the file has packets for are erased (slot 0 with its eight boot sectors) and programmed,
//  the other slots stay as they are. A file with slot 0 only is the way back after a whole-cart erase.
// =========================================================================

#define EF3_IMAGE_BANKS (EF3_NUM_SLOTS * 64)

// Which banks of the image a type 90 CRT has a packet for (bit per bank, per slot); false when the file is not a
// valid image: nothing is touched before this has passed
typedef struct {
    uint64_t banks[EF3_NUM_SLOTS]; // bit n of slot s: bank n has a packet
    int packets;
} ef3_image_t;

static bool ef3_image_scan(crt_reader_t *f, ef3_image_t *img)
{
    memset(img, 0, sizeof(*img));
    uint8_t hdr[0x40];
    if (crt_read(f, hdr, 0x40) != 0x40 || memcmp(hdr, crt_signature, 16) != 0)
        return false;
    if (!crt_seek(f, cart_get_dword(hdr + 0x10)))
        return false;
    while (true) {
        uint8_t ch[0x10];
        int more = next_chip(f, ch);
        if (more < 0) {
            LOG("[CartWriter] type 90 CRT: bytes that are no CHIP packet\n");
            return false;
        }
        if (!more)
            break;
        uint32_t len = cart_get_dword(ch + 0x04);
        uint16_t bank = cart_get_word(ch + 0x0A), load = cart_get_word(ch + 0x0C), size = cart_get_word(ch + 0x0E);
        if (bank >= EF3_IMAGE_BANKS || (load != 0x8000 && load != 0xA000 && load != 0xE000) || size > 0x2000 ||
            len != (uint32_t)0x10 + size) {
            LOG("[CartWriter] type 90 CRT: bad packet bank %d load $%04X size %d\n", bank, load, size);
            return false;
        }
        if (!skip_packet_data(f, size)) {
            LOG("[CartWriter] CRT ends inside a CHIP packet\n");
            return false;
        }
        img->banks[bank >> 6] |= 1ULL << (bank & 63);
        img->packets++;
    }
    return img->packets > 0;
}

static int ef3_image_slots(const ef3_image_t *img)
{
    int n = 0;
    for (int s = 0; s < EF3_NUM_SLOTS; s++)
        n += img->banks[s] != 0;
    return n;
}

// Streams the packets of the file bank by bank (ROML and ROMH paired, an absent half is erased flash) and hands each
// bank to fn(slot, bank, 16 KiB); stops at the first FRESULT that is not FR_OK
typedef FRESULT (*ef3_bank_fn)(cart_writer_t *w, int slot, int bank, const uint8_t *data);

static FRESULT ef3_image_each_bank(cart_writer_t *w, crt_reader_t *f, ef3_bank_fn fn)
{
    uint8_t hdr[0x40];
    if (!crt_seek(f, 0) || crt_read(f, hdr, 0x40) != 0x40 || !crt_seek(f, cart_get_dword(hdr + 0x10)))
        return FR_DISK_ERR;
    uint8_t *buf = (uint8_t *)g_api->mem->malloc(0x4000);
    if (!buf)
        return FR_NOT_ENOUGH_CORE;
    memset(buf, 0xFF, 0x4000);
    FRESULT res = FR_OK;
    int cur = -1;
    while (res == FR_OK) {
        uint8_t ch[0x10];
        int more = next_chip(f, ch);
        if (more < 0)
            res = FR_DISK_ERR;
        if (more <= 0)
            break;
        int bank = cart_get_word(ch + 0x0A);
        uint16_t load = cart_get_word(ch + 0x0C), size = cart_get_word(ch + 0x0E);
        if (cur >= 0 && bank != cur) {
            res = fn(w, cur >> 6, cur & 63, buf);
            memset(buf, 0xFF, 0x4000);
            if (res != FR_OK)
                break;
        }
        cur = bank;
        if (crt_read(f, buf + (load == 0x8000 ? 0 : 0x2000), size) != size)
            res = FR_DISK_ERR;
    }
    if (res == FR_OK && cur >= 0)
        res = fn(w, cur >> 6, cur & 63, buf);
    g_api->mem->free(buf);
    return res;
}

static int s_img_slot = -1;

// The map shows the slot being worked on
static void ef3_image_slot_shown(cart_writer_t *w, int slot)
{
    if (slot == s_img_slot)
        return;
    s_img_slot = slot;
    memset(w->bank_status_lo, BANK_UNKNOWN, sizeof(w->bank_status_lo));
    memset(w->bank_status_hi, BANK_UNKNOWN, sizeof(w->bank_status_hi));
    if (w->window) {
        char line[CART_SCREEN_COLS + 1];
        ui_snprintf(line, sizeof(line), "Image: slot %d%s", slot, slot == 0 ? " (system area)" : "");
        cart_screen_line(w->window, w->layout.y_slot, 15, line);
    }
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
}

static FRESULT ef3_image_write_bank(cart_writer_t *w, int slot, int bank, const uint8_t *data)
{
    ef3_image_slot_shown(w, slot);
    char msg[40];
    ui_snprintf(msg, sizeof(msg), "Slot %d bank %d/64", slot, bank + 1);
    w->bank_status_lo[bank] = w->bank_status_hi[bank] = BANK_WRITING;
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_status(w, msg);
    FRESULT res = ef3_write_bank_slot(w, slot, bank, data, 0x4000);
    w->bank_status_lo[bank] = w->bank_status_hi[bank] = (res == FR_OK) ? BANK_FILLED : BANK_ERROR;
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    return res;
}

static FRESULT do_write_ef3_image_from(cart_writer_t *w, crt_reader_t *f)
{
    ef3_image_t img;
    if (!ef3_image_scan(f, &img))
        return FR_INVALID_PARAMETER; // nothing touched
    LOG("[CartWriter] EF3 image: %d packets, %d slots\n", img.packets, ef3_image_slots(&img));

    s_ef3_system = true; // on every way out the caller clears it
    FRESULT res = FR_OK;
    enter_flash_mode(w);
    for (int slot = 0; slot < EF3_NUM_SLOTS && res == FR_OK; slot++) {
        if (!img.banks[slot])
            continue;
        s_img_slot = -1;
        ef3_image_slot_shown(w, slot);
        res = ef3_erase_slot_any(w, slot);
    }
    exit_flash_mode(w);
    s_img_slot = -1;
    if (res == FR_OK)
        res = ef3_image_each_bank(w, f, ef3_image_write_bank);
    s_ef3_system = false;
    cart_ef3_select_slot(&w->base);
    return res;
}

static FRESULT do_write_ef3_image(cart_writer_t *w)
{
    crt_reader_t *f = crt_open(w);
    if (!f)
        return FR_DISK_ERR;
    FRESULT res = do_write_ef3_image_from(w, f);
    crt_close(f);
    return res;
}

static int s_verify_errors, s_verify_banks;

static FRESULT ef3_image_verify_bank(cart_writer_t *w, int slot, int bank, const uint8_t *data)
{
    ef3_image_slot_shown(w, slot);
    C64_POKE(0xDE02, 0x05);
    C64_POKE(EF3_SLOT_REG, (uint8_t)slot);
    C64_POKE(0xDE00, (uint8_t)bank);
    int bad = 0;
    C64_READ(0x8000, s_chip_buf, 0x2000);
    for (int i = 0; i < 0x2000; i++)
        bad += s_chip_buf[i] != data[i];
    C64_READ(0xE000, s_chip_buf, 0x2000);
    for (int i = 0; i < 0x2000; i++)
        bad += s_chip_buf[i] != data[0x2000 + i];
    w->bank_status_lo[bank] = w->bank_status_hi[bank] = bad ? BANK_ERROR : BANK_VERIFIED;
    s_verify_errors += bad != 0;
    s_verify_banks++;
    if (bad)
        LOG("[CartWriter] EF3 verify slot %d bank %d: %d bytes differ\n", slot, bank, bad);
    if ((bank & 7) == 7)
        cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    return FR_OK;
}

// Compares every slot the file has packets for with the flash: banks of the file with their data, the banks of such a
// slot that the file leaves out with erased flash (the write erased them)
static FRESULT do_verify_ef3_image_from(cart_writer_t *w, crt_reader_t *f)
{
    ef3_image_t img;
    if (!ef3_image_scan(f, &img))
        return FR_INVALID_PARAMETER;
    s_verify_errors = s_verify_banks = 0;
    s_img_slot = -1;
    FRESULT res = ef3_image_each_bank(w, f, ef3_image_verify_bank);
    static uint8_t erased[0x4000];
    memset(erased, 0xFF, sizeof(erased));
    for (int slot = 0; slot < EF3_NUM_SLOTS && res == FR_OK; slot++) {
        if (!img.banks[slot])
            continue;
        for (int bank = 0; bank < 64; bank++)
            if (!(img.banks[slot] >> bank & 1))
                ef3_image_verify_bank(w, slot, bank, erased);
    }
    C64_POKE(0xDE02, w->chip->normal_val);
    cart_ef3_select_slot(&w->base);
    LOG("[CartWriter] EF3 verify: %d banks, %d errors\n", s_verify_banks, s_verify_errors);
    char msg[48];
    if (res != FR_OK || s_verify_errors)
        ui_snprintf(msg, sizeof(msg), "VERIFY FAILED: %d of %d banks differ!", s_verify_errors, s_verify_banks);
    else
        ui_snprintf(msg, sizeof(msg), "Verify OK: %d banks, %d slots.", s_verify_banks, ef3_image_slots(&img));
    draw_status(w, msg);
    return (res == FR_OK && !s_verify_errors) ? FR_OK : FR_DISK_ERR;
}

static FRESULT do_verify_ef3_image(cart_writer_t *w)
{
    crt_reader_t *f = crt_open(w);
    if (!f)
        return FR_DISK_ERR;
    FRESULT res = do_verify_ef3_image_from(w, f);
    crt_close(f);
    return res;
}

// Two questions before anything that destroys more than one slot: a YES/NO popup that says what goes, then the user
// types the word. Nothing is touched before both are answered; any other answer ends it.
static bool confirm_destructive(const char *msg, const char *word)
{
    if (u2p_ui_popup_buttons(msg, BUTTON_YES | BUTTON_NO) != BUTTON_YES)
        return false;
    char title[40], typed[64] = "";
    ui_snprintf(title, sizeof(title), "Type %s to confirm", word);
    if (u2p_ui_string_input(title, typed, 12) <= 0)
        return false;
    for (const char *a = typed, *b = word;; a++, b++) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        if (ca != *b)
            return false;
        if (!*b)
            return true;
    }
}

// The same wrapper as writer_guarded() for the whole-cart operations
static FRESULT ef3_guarded(cart_writer_t *w, FRESULT (*op)(cart_writer_t *))
{
    if (is_trx() && !slot_wr_open()) {
        cart_notify("Cannot unlock cartridge writes.\n"
                    "Nothing was written.");
        return FR_DENIED;
    }
    FRESULT res = op(w);
    s_ef3_system = false;
    if (is_trx()) {
        if (res != FR_OK)
            flash_reset_chips(w);
        slot_wr_close();
    }
    return res;
}

static FRESULT ef3_erase_cart_op(cart_writer_t *w) { return ef3_erase_cart(w); }

// =========================================================================
//  Drawing
// =========================================================================

static void draw_header(cart_writer_t *w)
{
    if (!w->window)
        return;
    cart_layout_compute(&w->layout, w->chip ? w->chip->max_banks : 1, w->chip ? w->chip->has_romh : false,
                        w->chip && w->base.is_ef3);

    char line[CART_SCREEN_COLS + 1];
    if (w->chip) {
        ui_snprintf(line, sizeof(line), "TREX Cart Writer - %s (%dKB)", w->chip->name, w->chip->total_size / 1024);
    } else {
        ui_snprintf(line, sizeof(line), "TREX Cart Writer - No cart detected");
    }
    cart_screen_line(w->window, w->layout.y_header, 1, line);

    // EasyFlash 3: the slot to write
    if (w->chip && w->base.is_ef3) {
        if (w->base.ef3_whole) {
            ui_snprintf(line, sizeof(line), "Full EF3 (all slots, type 90)");
        } else if (w->base.ef3_slot) {
            ui_snprintf(line, sizeof(line), "Slot %d%s%s", w->base.ef3_slot, w->base.ef3_name[0] ? ": " : "",
                        w->base.ef3_name);
        } else {
            ui_snprintf(line, sizeof(line), "No slot selected");
        }
        cart_screen_line(w->window, w->layout.y_slot, 15, line);
    }
    cart_screen_file(w->window, &w->layout, w->crt_filename);
}

static void draw_status(cart_writer_t *w, const char *msg)
{
    cart_screen_status(w->window, &w->layout, msg);
}

static void draw_footer(cart_writer_t *w)
{
    static const char *const keys[] = { "F1", "Scan", "F3", "Write", "F5", "Verify", "R/S", "Exit" };
    static const char *const slot_keys[] = { "F7", "Slot/full/erase" };
    if (!w->window)
        return;
    cart_screen_keys(w->window, w->layout.y_keys, keys, 4);
    if (w->chip && w->base.is_ef3)
        cart_screen_keys(w->window, w->layout.y_keys2, slot_keys, 1);
}

void cart_writer_redraw(cart_writer_t *w)
{
    if (!w->window)
        return;
    window_clear(w->window);
    draw_header(w);
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_footer(w);
}

// =========================================================================
//  init / deinit / poll
// =========================================================================

// Scans the flash cart; EasyFlash ROMH is only visible at $E000 in flash (Ultimax) mode
static void scan_chip(cart_writer_t *w)
{
    const flash_chip_info_t *chip = w->chip;
    cart_ef3_select_slot(&w->base); // EasyFlash 3: the picked slot
    if (chip->has_romh)
        enter_flash_mode(w); // Ultimax: ROMH@$E000
    cart_scan_all_banks(w->bank_status_lo, w->bank_status_hi, chip->max_banks, chip->has_romh,
                        chip->has_romh ? 0xE000 : 0xA000);
    if (chip->has_romh)
        exit_flash_mode(w);
}

// What the check before the writer saw in the file; the writer's summary takes it instead of walking the file again
static struct {
    bool valid;
    char path[256], name[256];
    char crt_name[33];
    uint16_t type;
    int chips;
    uint32_t bytes;
    bool eeprom;
} s_pre;

// The banks a CRT of this type may name, 0 = the writer has no such cartridge type
static int crt_type_banks(uint16_t type)
{
    switch (type) {
    case 32: return 64;                 // EasyFlash, and a slot of the EasyFlash 3
    case 60: return 64;                 // GMod2
    case 86: return 128;                // MegaByter
    case 19: case 88: return 256;       // C64MegaCart (19: what older dumps carry)
    case 90: return EF3_NUM_SLOTS * 64; // EasyFlash 3 image
    default: return 0;
    }
}

// Looks at a CRT before the writer screen opens. NULL = the writer can take the file, else why not, in the user's
// words (the popup text). Same rules as the write below: header, a type the writer knows, whole CHIP packets to the
// end of the file (the last byte of every packet is read), sizes and banks in range, at least one chip. Only
// the file is read: no cartridge, no bus. What depends on the cartridge in the port (8K-only flash, EasyFlash 3
// slot or image) the writer still checks itself.
const char *cart_writer_check_crt(const char *path, const char *filename)
{
    static char reason[160];
    s_pre.valid = false;
    crt_reader_t *f = crt_open_file(path, filename);
    if (!f) {
        LOG("[CartWriter] check: cannot open %s / %s\n", path, filename);
        return "Cannot open the CRT file.";
    }
    uint8_t hdr[0x40];
    const char *bad = NULL;
    if (crt_read(f, hdr, 0x40) != 0x40 || memcmp(hdr, crt_signature, 16) != 0) {
        bad = "This is not a CRT file.\nThe C64 CARTRIDGE header\nis missing or cut off.";
    }
    uint16_t type = 0;
    int max_banks = 0, chips = 0;
    uint32_t bytes = 0, first_chip = 0;
    bool eeprom = false;
    if (!bad) {
        type = cart_get_word(hdr + 0x16);
        first_chip = cart_get_dword(hdr + 0x10);
        max_banks = crt_type_banks(type);
        if (!max_banks) {
            ui_snprintf(reason, sizeof(reason), "CRT type %d is no cartridge\nthe writer can flash.", type);
            bad = reason;
        } else if (first_chip < 0x40 || !crt_seek(f, first_chip)) {
            bad = "The CRT header is damaged:\nthe chip data starts\noutside the file.";
        }
    }
    while (!bad) {
        uint8_t ch[0x10];
        int more = next_chip(f, ch);
        if (more < 0) {
            bad = "The CRT has bytes that are\nno chip packet (damaged\nor cut off).";
            break;
        }
        if (!more)
            break;
        uint32_t len = cart_get_dword(ch + 0x04);
        uint16_t bank = cart_get_word(ch + 0x0A), load = cart_get_word(ch + 0x0C), size = cart_get_word(ch + 0x0E);
        if (size > 0x2000 || len != (uint32_t)0x10 + size) {
            bad = "A chip packet of the CRT\nhas a wrong size.";
            break;
        }
        if (!skip_packet_data(f, size)) {
            bad = "The CRT file is cut off\ninside a chip packet.";
            break;
        }
        if (load == 0xDE00) {
            eeprom = true; // GMod2 EEPROM contents: no bank
            continue;
        }
        if (bank >= max_banks) {
            ui_snprintf(reason, sizeof(reason), "The CRT has bank %d, but\ntype %d has %d banks.", bank, type,
                        max_banks);
            bad = reason;
            break;
        }
        bytes += size;
        chips++;
    }
    crt_close(f);
    if (!bad && chips == 0)
        bad = "The CRT has no chips\nto write.";
    if (bad) {
        LOG("[CartWriter] check: %s / %s refused: %s\n", path, filename, bad);
        return bad;
    }
    LOG("[CartWriter] check: CRT type %d, %d chips, %d bytes ok\n", type, chips, (int)bytes);
    strncpy(s_pre.path, path, sizeof(s_pre.path) - 1);
    strncpy(s_pre.name, filename, sizeof(s_pre.name) - 1);
    memcpy(s_pre.crt_name, hdr + 0x20, 32);
    s_pre.crt_name[32] = 0;
    s_pre.type = type;
    s_pre.chips = chips;
    s_pre.bytes = bytes;
    s_pre.eeprom = eeprom;
    s_pre.valid = true;
    return NULL;
}

// Reads the CRT name, the CHIP packet count and the total data size
static void read_crt_summary(cart_writer_t *w)
{
    LOG("[CartWriter] CRT: %s / %s\n", w->crt_path, w->crt_filename);
    if (!load_crt(w))
        return;
    if (s_pre.valid && !strcmp(s_pre.path, w->crt_path) && !strcmp(s_pre.name, w->crt_filename)) {
        memcpy(w->crt_name, s_pre.crt_name, sizeof(w->crt_name));
        w->crt_type = s_pre.type;
        w->crt_num_chips = s_pre.chips;
        w->crt_total_bytes = (int)s_pre.bytes;
        w->crt_has_eeprom = s_pre.eeprom;
        LOG("[CartWriter] CRT: %d chips, %d bytes (checked on open)\n", w->crt_num_chips, w->crt_total_bytes);
        return;
    }
    crt_reader_t *f = crt_open(w);
    if (!f)
        return;

    uint8_t hdr[0x40];
    if (crt_read(f, hdr, 0x40) == 0x40 && memcmp(hdr, crt_signature, 16) == 0) {
        memcpy(w->crt_name, hdr + 0x20, 32);
        w->crt_type = cart_get_word(hdr + 0x16);
        w->crt_name[32] = '\0';
        LOG("[CartWriter] CRT name: '%s'\n", w->crt_name);
        bool at_chips = crt_seek(f, cart_get_dword(hdr + 0x10));
        w->crt_num_chips = 0;
        w->crt_total_bytes = 0;
        while (at_chips) {
            uint8_t ch[0x10];
            if (next_chip(f, ch) <= 0)
                break;
            uint32_t pkt_len = cart_get_dword(ch + 0x04);
            // GMod2 EEPROM contents (load $DE00) are no bank
            if (cart_get_word(ch + 0x0C) == 0xDE00) {
                w->crt_has_eeprom = true;
            } else {
                w->crt_total_bytes += cart_get_word(ch + 0x0E);
                w->crt_num_chips++;
            }
            if (pkt_len < 0x10 || !skip_bytes(f, pkt_len - 0x10))
                break;
        }
        LOG("[CartWriter] CRT: %d chips, %d bytes\n", w->crt_num_chips, w->crt_total_bytes);
    }
    crt_close(f);
}

void cart_writer_init(cart_writer_t *w, const char *path, const char *filename)
{
    memset(w, 0, sizeof(*w));
    strncpy(w->crt_path, path ? path : "", sizeof(w->crt_path) - 1);
    strncpy(w->crt_filename, filename ? filename : "", sizeof(w->crt_filename) - 1);

    w->window = cart_screen_open();
    window_clear(w->window);
    u2p_ui_set_redraw(rebuild, w);

    if (!cart_session_open(false)) {
        cart_writer_redraw(w);
        draw_status(w, cart_session_status());
        return;
    }
    if (!cart_check_dma_access()) {
        cart_writer_redraw(w);
        draw_status(w, "No cart access: set Cart Pref Internal");
        return;
    }
    w->chip = detect_flash_cart(w);
    if (!w->chip && w->base.det_game && w->base.det_exrom)
        cart_notify_no_cartridge();

    if (w->chip) {
        // Parse CRT file if provided
        if (w->crt_filename[0]) {
            read_crt_summary(w);
        }

        // EasyFlash 3: one EasyFlash per slot, or the full EF3 (type 90): the user picks
        if (w->base.is_ef3) {
            cart_ef3_pick_slot(&w->base, EF3_PICK_WHOLE);
        }

        draw_header(w);
        draw_status(w, "Scanning...");
        draw_footer(w);
        if (w->base.is_ef3 && !w->base.ef3_slot && !w->base.ef3_whole) {
            draw_status(w, "No slot selected. F7=Select slot");
            return;
        }
        scan_chip(w);
        w->num_banks_found = w->chip->max_banks;
        cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
        draw_status(w, "Ready.");
    } else {
        cart_writer_redraw(w);
        draw_status(w, "No supported flash cart found!");
    }
}

void cart_writer_deinit(cart_writer_t *w)
{
    u2p_ui_set_redraw(NULL, NULL);
    if (w->crt_data) {
        g_api->mem->free(w->crt_data);
        w->crt_data = NULL;
    }
    cart_ef3_restore_slot(&w->base);
    cart_session_close();
    window_close(w->window);
    w->window = NULL;
}

// Scans the flash cart again (EasyFlash 3: the picked slot) and shows done when finished
static void rescan(cart_writer_t *w, const char *done)
{
    draw_status(w, "Scanning...");
    memset(w->bank_status_lo, BANK_UNKNOWN, sizeof(w->bank_status_lo));
    memset(w->bank_status_hi, BANK_UNKNOWN, sizeof(w->bank_status_hi));
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    scan_chip(w);
    cart_draw_bank_map(w->window, &w->layout, w->bank_status_lo, w->bank_status_hi);
    draw_status(w, done);
}

// A dialog restores the screen it found when it closes, and that was not this
// one: the frame, the header and the keys are gone afterwards. The SDK dialogs
// call this when they close. Open the frame again, which clears the whole
// screen, and draw everything, rather than drawing the frame a second time
// into the old window, which would nest one frame inside the other.
static void rebuild(void *ctx)
{
    cart_writer_t *w = (cart_writer_t *)ctx;
    if (w->window)
        window_close(w->window);
    w->window = cart_screen_open();
    cart_writer_redraw(w);
}

// F7 "ERASE WHOLE CART...": the chip erase of the real EasyFlash 3, 8 MB, after two confirmations
static void erase_cart_ui(cart_writer_t *w)
{
    if (!confirm_destructive("ERASE THE WHOLE EASYFLASH 3?\n"
                             "All 8 MB: the system area\n"
                             "(menu, KERNALs), every slot,\n"
                             "the directory. The cart will\n"
                             "not start until a system\n"
                             "image is written back.",
                             "ERASE")) {
        draw_status(w, "Not erased. Nothing changed.");
        return;
    }
    FRESULT res = ef3_guarded(w, ef3_erase_cart_op);
    LOG("[CartWriter] whole-cart erase: %s\n", res == FR_OK ? "done" : "FAILED");
    if (res == FR_OK)
        draw_status(w, "Whole cart erased. Write a system image!");
}

// F3 with a type 90 image: write the slots the file has (slot 0 included), after two confirmations
static void write_ef3_image_ui(cart_writer_t *w)
{
    if (!confirm_destructive("WRITE THE WHOLE EASYFLASH 3\n"
                             "IMAGE? Every slot the file\n"
                             "has is erased and rewritten,\n"
                             "slot 0 (the system area)\n"
                             "included.",
                             "WRITE")) {
        draw_status(w, "Not written. Nothing changed.");
        return;
    }
    FRESULT res = ef3_guarded(w, do_write_ef3_image);
    if (res == FR_OK)
        draw_status(w, "Image written! F5=Verify");
    else if (res == FR_INVALID_PARAMETER)
        draw_status(w, "Not a valid type 90 image!");
    else
        draw_status(w, "WRITE FAILED!");
}

int cart_writer_poll(cart_writer_t *w, int c)
{
    bool image = w->chip && w->base.is_ef3 && w->base.ef3_whole; // "Full EF3" picked: the type 90 image, all slots
    bool no_slot = w->chip && w->base.is_ef3 && !w->base.ef3_slot && !image;

    switch (c) {
    case KEY_F1:
        if (no_slot) {
            draw_status(w, "No slot selected. F7=Select slot");
        } else if (w->chip) {
            rescan(w, "Scan complete.");
        }
        break;

    case KEY_F3:
        if (!w->chip) {
            draw_status(w, "No flash cart!");
            break;
        }
        if (!w->crt_filename[0]) {
            draw_status(w, "No CRT file.");
            break;
        }
        if (no_slot) {
            draw_status(w, "No slot selected. F7=Select slot");
            break;
        }
        if (image) {
            if (w->crt_type != 90) {
                cart_notify("Full EF3 writes a type 90\n"
                            "image. This CRT is not one:\n"
                            "pick a slot (F7) for it.");
                draw_status(w, "Not a type 90 image!");
                break;
            }
            write_ef3_image_ui(w);
            break;
        }
        if (w->base.is_ef3 && w->crt_type == 90) {
            cart_notify("A type 90 image needs\n"
                        "Full EF3 (F7). Writing a\n"
                        "single slot from it is not\n"
                        "supported.");
            draw_status(w, "Pick Full EF3 for type 90.");
            break;
        }
        if (!w->base.is_ef3 && w->crt_type == 90) {
            cart_notify("EasyFlash 3 image (type 90):\n"
                        "needs an EasyFlash 3 in the\n"
                        "port.");
            draw_status(w, "Not for this cartridge!");
            break;
        }
        // A slot of an EasyFlash 3 is an EasyFlash
        if (w->base.is_ef3 && w->crt_type != 32) {
            cart_notify("EasyFlash 3 slots take\n"
                        "EasyFlash CRTs (type 32) only.");
            draw_status(w, "Not an EasyFlash CRT!");
            break;
        }
        // F3 on this screen is the decision: the chip, the slot and the file
        // are all in front of the user, so there is no second question.
        {
            {
                FRESULT res = writer_guarded(w);
                // Like EasyProg, name the slot in the EF-Directory once the CRT is written
                if (res == FR_OK && w->base.is_ef3 && ef3_name_slot(w) != FR_OK) {
                    draw_status(w, "Written; directory update FAILED!");
                    break;
                }
                // GMod2: verify the flash, then the EEPROM last. It powers up write-disabled, which keeps it safe
                // while flash mode toggles its chip select.
                if (res == FR_OK && w->chip == &gmod2_info) {
                    bool verified = do_verify_crt(w) == FR_OK && gmod2_write_eeprom(w) == FR_OK;
                    if (verified) {
                        draw_status(w, "Flash+EEPROM verified.");
                    }
                    break; // otherwise the status shows what failed
                }
                if (res == FR_OK) {
                    draw_status(w, "Write complete! F5=Verify");
                } else if (res == FR_INVALID_PARAMETER) {
                    cart_notify("This CRT contains ROMH data\n"
                                "(16K/Ultimax). Cannot write to\n"
                                "an 8K-only flash cart.");
                    draw_status(w, "CRT has ROMH - incompatible!");
                } else {
                    draw_status(w, "WRITE FAILED!");
                }
            }
        }
        break;

    case KEY_F5:
        if (no_slot) {
            draw_status(w, "No slot selected. F7=Select slot");
        } else if (image && w->crt_filename[0]) {
            if (w->crt_type != 90)
                draw_status(w, "Not a type 90 image!");
            else if (do_verify_ef3_image(w) == FR_INVALID_PARAMETER)
                draw_status(w, "Not a valid type 90 image!");
        } else if (w->base.is_ef3 && w->crt_type == 90 && w->crt_filename[0]) {
            draw_status(w, "Pick Full EF3 for type 90.");
        } else if (w->chip && w->crt_filename[0]) {
            FRESULT res = do_verify_crt(w);
            if (res == FR_OK && w->chip == &gmod2_info) {
                if (gmod2_verify_eeprom(w) == FR_OK)
                    draw_status(w, "Flash and EEPROM verified OK!");
            } else if (res == FR_OK) {
                draw_status(w, "All banks verified OK!");
            }
        }
        break;

    case KEY_F7:
        if (w->chip && w->base.is_ef3) {
            int r = cart_ef3_pick_slot(&w->base, EF3_PICK_WHOLE | EF3_PICK_ERASE);
            if (r == EF3_PICKED_SLOT || r == EF3_PICKED_WHOLE) {
                cart_writer_redraw(w);
                rescan(w, "Ready.");
            } else if (r == EF3_PICKED_ERASE) {
                erase_cart_ui(w);
            }
        }
        break;

    case KEY_ESCAPE:
    case KEY_BACK:
    case KEY_BREAK:
        return MENU_CLOSE;
    }
    return 0;
}
