/*
 * cart_dumper.c
 *
 *  Dump physical cartridges to .CRT files via the U64 cart slot, with a bank
 *  map showing scan/dump progress. Port of the firmware CartDumper
 *  (software/io/c64/cart_dumper.cc).
 */

#include "cart_dumper.h"
#include "eapi_drivers.h"

static const uint8_t crt_signature[16] = { 0x43, 0x36, 0x34, 0x20, 0x43, 0x41, 0x52, 0x54,
                                           0x52, 0x49, 0x44, 0x47, 0x45, 0x20, 0x20, 0x20 };

static void draw_header(cart_dumper_t *d);
static void draw_status(cart_dumper_t *d, const char *msg);
static void draw_footer(cart_dumper_t *d);

// =========================================================================
//  Cart type detection (probe via DMA banking tests)
// =========================================================================

static cart_probe_result_t probe_cart_type(cart_dumper_t *d)
{
    cart_probe_result_t result;
    result.banking_found = false;
    result.hw_type = 0;
    result.type_name = "Unknown";
    result.bank_count = 1;

    uint8_t det_game = d->base.det_game;
    uint8_t det_exrom = d->base.det_exrom;
    bool is_8k = (det_exrom == 0 && det_game == 1);
    bool is_16k = (det_exrom == 0 && det_game == 0);
    bool is_ultimax = (det_exrom == 1 && det_game == 0);

    uint8_t ref[256], buf[256];

    LOG("[CartDumper] Probing: %s mode\n", is_8k ? "8K" : is_16k ? "16K" : is_ultimax ? "Ultimax" : "None");

    // =====================================================================
    // STEP 1: Basic $DE00 banking tests
    // =====================================================================
    cart_switch_bank(0, 0);
    C64_READ(0x8000, ref, 256);

    // Data-write: $DE00 = bank number
    cart_switch_bank(1, 0);
    C64_READ(0x8000, buf, 256);
    bool de00_data = (memcmp(ref, buf, 256) != 0);
    cart_switch_bank(0, 0);

    LOG("[CartDumper] $DE00 banking: %s\n", de00_data ? "YES" : "NO");

    // =====================================================================
    // STEP 2: EasyFlash — $DF00 RAM test (from CartBase)
    // Only EasyFlash has 256 bytes RAM at $DF00. Definitive, and independent of the banking test above: an
    // EasyFlash 3 showing its system area (slot 0) keeps the KERNALs in ROMH, so banks 0 and 1 read the same
    // at $8000 and STEP 1 says NO for a cartridge that banks perfectly well.
    // =====================================================================
    bool megabyter = false;

    bool ef = cart_detect_easyflash();
    if (ef) {
        // Set Ultimax for EF-mode banking
        C64_POKE(0xDE02, 0x05);
        cart_switch_bank(0, 0);
        // EasyFlash 3: slot register at $DE01
        cart_detect_ef3(&d->base);
    }

    // =====================================================================
    // STEP 3: Count banks (from CartBase)
    // =====================================================================
    int bank_count = 1;
    if (de00_data && !ef) {
        bank_count = cart_count_banks();
    } else if (ef) {
        bank_count = 64; // EasyFlash always has 64 banks
    }

    // =====================================================================
    // STEP 3b: MegaByter — $DE02=$03 + bank_count ≥ 128
    // Only test if NOT EasyFlash (both share $DE02)
    // =====================================================================
    if (!ef && de00_data && bank_count >= 128) {
        uint8_t e000_before[16], e000_after[16];
        C64_READ(0xE000, e000_before, 16);
        C64_POKE(0xDE02, 0x03);
        C64_READ(0xE000, e000_after, 16);
        C64_POKE(0xDE02, 0x00);

        if (memcmp(e000_before, e000_after, 16) != 0) {
            megabyter = true;
            LOG("[CartDumper] MegaByter confirmed ($DE02=$03 + %d banks)\n", bank_count);
        }
    }

    // =====================================================================
    // STEP 4: $DF00 — C64MegaCart and Super Games
    // =====================================================================
    bool megacart = false;
    bool supergames = false;

    if (is_16k && !ef) {
        C64_POKE(0xDF00, 0);
        C64_READ(0x8000, ref, 256);
        C64_POKE(0xDF00, 1);
        C64_READ(0x8000, buf, 256);
        supergames = (memcmp(ref, buf, 256) != 0);
        C64_POKE(0xDF00, 0);
        LOG("[CartDumper] $DF00 Super Games: %s\n", supergames ? "YES" : "NO");
    }

    if (is_8k && de00_data && !ef && !megabyter) {
        uint8_t e000_before[16], e000_after[16];
        C64_READ(0xE000, e000_before, 16);
        C64_POKE(0xDF00, 0xC0);
        C64_READ(0xE000, e000_after, 16);
        C64_POKE(0xDF00, 0x00);

        if (memcmp(e000_before, e000_after, 16) != 0 && bank_count >= 256) {
            megacart = true;
            LOG("[CartDumper] C64MegaCart (%d banks + $DF00)\n", bank_count);
        }
    }

    // =====================================================================
    // STEP 5: GMod2 — EEPROM and high-bank tests (from CartBase)
    // =====================================================================
    bool gmod2 = false;
    if (is_8k && de00_data && !ef && !megabyter && !megacart) {
        gmod2 = cart_detect_gmod2();
        cart_switch_bank(0, 0);
    }

    // =====================================================================
    // STEP 6: Assign type
    // =====================================================================
    if (ef) {
        result.hw_type = 32;
        result.type_name = d->base.is_ef3 ? "EasyFlash 3" : "EasyFlash";
        result.banking_found = true;
    } else if (is_8k) {
        if (megabyter) {
            result.hw_type = 86;
            result.type_name = "MegaByter";
            result.banking_found = true;
        } else if (megacart) {
            result.hw_type = 88; // T33: 88 = C64MegaCart (19 is Magic Desk)
            result.type_name = "C64MegaCart";
            result.banking_found = true;
        } else if (bank_count > 256) {
            result.hw_type = 62;
            result.type_name = "GMod3";
            result.banking_found = true;
        } else if (gmod2) {
            result.hw_type = 60;
            result.type_name = "GMod2";
            result.banking_found = true;
            bank_count = 64; // GMod2 always 64 banks (upper $DE00 bits are control)
        } else if (de00_data) {
            result.hw_type = 5;
            result.type_name = "Ocean/Magic Desk";
            result.banking_found = true;
        } else {
            result.hw_type = 0;
            result.type_name = "Normal 8K";
        }
    } else if (is_16k) {
        if (ef) {
            result.hw_type = 32;
            result.type_name = "EasyFlash";
            result.banking_found = true;
        } else if (supergames) {
            result.hw_type = 8;
            result.type_name = "Super Games";
            result.banking_found = true;
        } else if (de00_data) {
            result.hw_type = 5;
            result.type_name = "Ocean 16K";
            result.banking_found = true;
        } else {
            result.hw_type = 0;
            result.type_name = "Normal 16K";
        }
    } else if (is_ultimax) {
        if (ef) {
            result.hw_type = 32;
            result.type_name = "EasyFlash";
            result.banking_found = true;
        } else if (de00_data) {
            result.hw_type = 0;
            result.type_name = "Ultimax (banked)";
            result.banking_found = true;
        } else {
            result.hw_type = 0;
            result.type_name = "Normal (Ultimax)";
        }
    }

    result.bank_count = bank_count;
    LOG("[CartDumper] === Result: %s (type %d, %d banks) ===\n", result.type_name, result.hw_type, bank_count);
    return result;
}

static int select_cart_type(cart_dumper_t *d, const cart_probe_result_t *probe)
{
    bool is_8k = (d->base.det_exrom == 0 && d->base.det_game == 1);
    bool is_16k = (d->base.det_exrom == 0 && d->base.det_game == 0);

    // Types natively supported by U64 cart emulation
    // 61 (C64MegaCart) and 86 (MegaByter) map to Ocean 8K for playback
    bool u64_native = (probe->hw_type == 0 || probe->hw_type == 5 || probe->hw_type == 8 || probe->hw_type == 15 ||
                       probe->hw_type == 19 || probe->hw_type == 32 || probe->hw_type == 60 ||
                       probe->hw_type == 61 || probe->hw_type == 86 || probe->hw_type == 88);

    if (u64_native) {
        // Auto-detected type works on U64 — just confirm
        char msg[80];
        ui_snprintf(msg, sizeof(msg), "Detected: %s\nDump as type %d?", probe->type_name, probe->hw_type);
        if (u2p_ui_popup_buttons(msg, BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            return probe->hw_type;
        }
        // User said no — show full list
    }

    // Non-native type or user wants to change.
    // Ask which CRT type to write in the file header.
    char header[80];
    if (!u64_native) {
        ui_snprintf(header, sizeof(header), "%s (not U64-native) Save as:", probe->type_name);
    } else {
        ui_snprintf(header, sizeof(header), "Save as CRT type:");
    }

    if (is_8k) {
        static const char *opts[] = {
            "Magic Desk (19)",  // works on U64 + VICE
            "Ocean type 1 (5)", // works on U64 + VICE
            "MegaByter (86)",   // VICE only
            "Normal 8K (0)"     // universal
        };
        static const int types[] = { 19, 5, 86, 0 };
        int choice = u2p_ui_menu(header, opts, 4);
        return (choice < 0) ? -1 : types[choice];
    } else if (is_16k) {
        static const char *opts[] = { "Normal 16K (0)", "Ocean 16K (5)", "Super Games (8)" };
        static const int types[] = { 0, 5, 8 };
        int choice = u2p_ui_menu(header, opts, 3);
        return (choice < 0) ? -1 : types[choice];
    } else {
        static const char *opts[] = { "Normal Ultimax (0)", "EasyFlash (32)" };
        static const int types[] = { 0, 32 };
        int choice = u2p_ui_menu(header, opts, 2);
        return (choice < 0) ? -1 : types[choice];
    }
}

// =========================================================================
//  CRT file building
// =========================================================================

// An EasyFlash 3 image (type 90) says EXROM 1 / GAME 0 whatever the pins showed (PROP-002 8.4); its packets are
// flash (chip type 2) as ef3_image.py writes them
#define CRT_TYPE_EF3 90

// One slot of an EasyFlash 3 saved as an EasyFlash (type 32) is an EasyFlash image like ef3_image.py extract writes it
static bool ef3_slot_as_ef(const cart_dumper_t *d)
{
    return d->base.is_ef3 && d->crt_hw_type == 32;
}

static uint16_t s_chip_type; // the type field of the CHIP packets written: 0 ROM, 2 flash (type 90)

static void build_crt_header(cart_dumper_t *d, uint8_t *hdr, const char *name)
{
    memset(hdr, 0, 0x40);
    memcpy(hdr, crt_signature, 16);
    cart_put_dword(hdr + 0x10, 0x00000040);
    cart_put_word(hdr + 0x14, 0x0100);
    cart_put_word(hdr + 0x16, (uint16_t)d->crt_hw_type);
    const bool ultimax = d->crt_hw_type == CRT_TYPE_EF3 || ef3_slot_as_ef(d);
    hdr[0x18] = ultimax ? 1 : d->base.det_exrom;
    hdr[0x19] = ultimax ? 0 : d->base.det_game;
    strncpy((char *)(hdr + 0x20), name, 32);
}

static void build_chip_header(uint8_t *hdr, int bank, uint16_t load, uint16_t size)
{
    memset(hdr, 0, 0x10);
    hdr[0] = 'C';
    hdr[1] = 'H';
    hdr[2] = 'I';
    hdr[3] = 'P';
    cart_put_dword(hdr + 0x04, (uint32_t)(0x10 + size));
    cart_put_word(hdr + 0x08, s_chip_type);
    cart_put_word(hdr + 0x0A, (uint16_t)bank);
    cart_put_word(hdr + 0x0C, load);
    cart_put_word(hdr + 0x0E, size);
}

// Where the CRT goes while it is read: into RAM blocks (nblk > 0), or into a file f: a file on the RAM disk /Temp that
// is copied to the target afterwards, or the target itself when nothing else is possible.
#define STAGE_BLOCK  0x10000u // one heap block; the whole EasyFlash 3 (8.4 MB) is 129 of them
#define STAGE_MAXBLK 136
#define TEMP_DIR     "/Temp"

typedef struct
{
    void *f;
    uint8_t *blk[STAGE_MAXBLK];
    int nblk;
    uint32_t pos, cap;
    bool ef1_eapi; // one EasyFlash 3 slot as an EasyFlash CRT: bank 0 ROMH gets the EF1 EAPI where it has one
} crt_sink_t;

static void sink_free_blocks(crt_sink_t *k)
{
    for (int i = 0; i < k->nblk; i++)
        g_api->mem->free(k->blk[i]);
    k->nblk = 0;
}

// All blocks for cap bytes, or none
static bool sink_alloc_blocks(crt_sink_t *k)
{
    int need = (int)((k->cap + STAGE_BLOCK - 1) / STAGE_BLOCK);
    if (need > STAGE_MAXBLK)
        return false;
    while (k->nblk < need) {
        uint8_t *b = (uint8_t *)g_api->mem->malloc(STAGE_BLOCK);
        if (!b)
            break;
        k->blk[k->nblk++] = b;
    }
    if (k->nblk == need) {
        LOG("[CartDumper] Heap gave all %d blocks of %u KB for the %u byte image\n", need, STAGE_BLOCK / 1024,
            (unsigned)k->cap);
        return true;
    }
    LOG("[CartDumper] Heap gave %d of %d blocks of %u KB (%u of %u bytes)\n", k->nblk, need, STAGE_BLOCK / 1024,
        (unsigned)k->nblk * STAGE_BLOCK, (unsigned)k->cap);
    sink_free_blocks(k);
    return false;
}

static bool sink_put(crt_sink_t *k, const void *data, int len)
{
    if (k->nblk) {
        if (k->pos + (uint32_t)len > k->cap)
            return false;
        const uint8_t *src = (const uint8_t *)data;
        while (len > 0) {
            uint32_t off = k->pos % STAGE_BLOCK;
            int n = (STAGE_BLOCK - off < (uint32_t)len) ? (int)(STAGE_BLOCK - off) : len;
            memcpy(k->blk[k->pos / STAGE_BLOCK] + off, src, (size_t)n);
            k->pos += (uint32_t)n;
            src += n;
            len -= n;
        }
        return true;
    }
    if (file_write(k->f, data, len) != len)
        return false;
    k->pos += (uint32_t)len;
    return true;
}

// Writes one CHIP packet. The data comes from src_buf, or from the C64 bus
// at src_addr when src_buf is NULL.
static FRESULT write_chip(crt_sink_t *k, int bank, uint16_t load, uint16_t size, uint16_t src_addr,
                          const uint8_t *src_buf)
{
    uint8_t chip_hdr[0x10];
    build_chip_header(chip_hdr, bank, load, size);
    if (!sink_put(k, chip_hdr, 0x10))
        return FR_DISK_ERR;

    static const uint8_t eapi_sig[4] = { 0x65, 0x61, 0x70, 0x69 };
    const bool eapi_chip = k->ef1_eapi && bank == 0 && load == 0xA000 && size == 0x2000;
    bool eapi_on = false;
    uint16_t done = 0;
    uint8_t buf[256];
    uint16_t remaining = size;
    while (remaining > 0) {
        int chunk = (remaining > 256) ? 256 : remaining;
        if (src_buf) {
            memcpy(buf, src_buf, chunk);
            src_buf += chunk;
        } else {
            C64_READ(src_addr, buf, chunk);
            src_addr += chunk;
        }
        if (eapi_chip && done >= (EAPI_ROMH_OFFSET & ~0xFF) && done < EAPI_ROMH_OFFSET + EAPI_DRIVER_SIZE) {
            // the EAPI starts on a chunk boundary: the signature decides in its first chunk, then it is overlaid
            if (done == EAPI_ROMH_OFFSET)
                eapi_on = memcmp(buf, eapi_sig, sizeof(eapi_sig)) == 0;
            if (eapi_on)
                memcpy(buf, eapi_am29f040 + (done - EAPI_ROMH_OFFSET), chunk);
        }
        if (!sink_put(k, buf, chunk))
            return FR_DISK_ERR;
        remaining -= chunk;
        done += chunk;
    }
    return FR_OK;
}

// =========================================================================
//  Dump operation
// =========================================================================

// Every dialog leaves the screen it found, not this one; the SDK dialogs call
// this when they close. Open the frame again, which clears the whole screen,
// and draw everything.
static void rebuild(void *ctx)
{
    cart_dumper_t *d = (cart_dumper_t *)ctx;
    if (d->window)
        window_close(d->window);
    d->window = cart_screen_open();
    cart_dumper_redraw(d);
}

static FRESULT dump_run(cart_dumper_t *d, void *f, const char *path, const char *fname, const char *crt_name);

static FRESULT do_dump(cart_dumper_t *d)
{
    // Ask user for CRT type (only now, at save time). The whole EasyFlash 3 is one type 90 file: nothing to choose
    int save_type;
    if (d->base.ef3_whole) {
        if (u2p_ui_popup_buttons("Dump the full EasyFlash 3?\n"
                                 "8 slots incl. the system area\n"
                                 "as ONE type 90 CRT (8 MB).",
                                 BUTTON_YES | BUTTON_NO) != BUTTON_YES)
            return FR_OK; // cancelled
        save_type = CRT_TYPE_EF3;
    } else {
        save_type = select_cart_type(d, &d->probe_result);
    }
    if (save_type < 0)
        return FR_OK; // cancelled
    d->crt_hw_type = save_type;

    // Ask user where to save
    char dest_path[64] = ""; // empty: the picker opens where the last pick went
    char name_buf[32] = "cartridge";
    if (d->base.ef3_whole)
        strcpy(name_buf, "easyflash3");
    // EasyFlash 3: the slot name from the EF-Directory is the default file name and the CRT name
    const bool ef3_named = d->base.is_ef3 && d->base.ef3_name[0];
    void *f = NULL;
    FRESULT res = FR_OK;
    if (u2p_ui_path_input(dest_path, sizeof(dest_path)) < 0 || !dest_path[0])
        return FR_OK; // cancelled
    LOG("[CartDumper] Save path: '%s'\n", dest_path);

    // Ask for filename
    if (ef3_named) {
        strncpy(name_buf, d->base.ef3_name, 22);
        name_buf[22] = 0;
    }
    res = create_user_file("Save CRT as..", ".crt", dest_path, &f, name_buf);
    if (res != FR_OK || !f)
        return FR_OK; // cancelled
    LOG("[CartDumper] Saving as type %d to: '%s/%s.crt'\n", d->crt_hw_type, dest_path, name_buf);
    ui_snprintf(d->file, sizeof(d->file), "%s.crt", name_buf);
    cart_dumper_redraw(d); // with the file name
    char fname[32];
    user_file_name(name_buf, ".crt", fname);
    return dump_run(d, f, dest_path, fname, d->base.ef3_whole ? "EASYFLASH 3" : ef3_named ? d->base.ef3_name : name_buf);
}

// One bank of the cartridge (of one slot of an EasyFlash 3) into the file, with the map and the status line
static FRESULT dump_bank(cart_dumper_t *d, crt_sink_t *f, int slot, int slots, int bank, int det_banks)
{
    const bool is_16k = (d->base.det_exrom == 0 && d->base.det_game == 0);
    const bool is_ultimax = (d->base.det_exrom == 1 && d->base.det_game == 0);
    const int crt_bank = slot * 64 + bank;
    FRESULT res;

    cart_switch_bank(bank, d->crt_hw_type);
    d->bank_status_lo[bank] = BANK_WRITING;
    if (d->has_romh)
        d->bank_status_hi[bank] = BANK_WRITING;
    cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);

    char msg[40];
    if (slots > 1)
        ui_snprintf(msg, sizeof(msg), "Slot %d bank %d/%d...", slot, bank + 1, det_banks);
    else
        ui_snprintf(msg, sizeof(msg), "Dumping bank %d/%d...", bank + 1, det_banks);
    draw_status(d, msg);

    if (d->crt_hw_type == 32 || d->crt_hw_type == CRT_TYPE_EF3) {
        // EasyFlash: ROML + ROMH at $E000 (saved as $A000 in CRT)
        res = write_chip(f, crt_bank, 0x8000, 0x2000, 0x8000, NULL);
        d->bank_status_lo[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
        if (res == FR_OK) {
            res = write_chip(f, crt_bank, 0xA000, 0x2000, 0xE000, NULL);
            d->bank_status_hi[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
        }
    } else if (is_16k) {
        res = write_chip(f, bank, 0x8000, 0x2000, 0x8000, NULL);
        d->bank_status_lo[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
        if (res == FR_OK) {
            res = write_chip(f, bank, 0xA000, 0x2000, 0xA000, NULL);
            d->bank_status_hi[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
        }
    } else if (is_ultimax) {
        res = write_chip(f, bank, 0x8000, 0x2000, 0x8000, NULL);
        d->bank_status_lo[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
        if (res == FR_OK) {
            res = write_chip(f, bank, 0xE000, 0x2000, 0xE000, NULL);
            d->bank_status_hi[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
        }
    } else {
        res = write_chip(f, bank, 0x8000, 0x2000, 0x8000, NULL);
        d->bank_status_lo[bank] = (res == FR_OK) ? BANK_VERIFIED : BANK_ERROR;
    }

    cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);
    return res;
}

// The size of the whole CRT file: header, the CHIP packets of every bank (as dump_bank writes them) and the EEPROM
static uint32_t crt_image_size(const cart_dumper_t *d, int slots)
{
    const bool is_16k = (d->base.det_exrom == 0 && d->base.det_game == 0);
    const bool is_ultimax = (d->base.det_exrom == 1 && d->base.det_game == 0);
    const bool two_chips = d->crt_hw_type == 32 || d->crt_hw_type == CRT_TYPE_EF3 || is_16k || is_ultimax;
    uint32_t size = 0x40 + (uint32_t)slots * (uint32_t)d->det_banks * (two_chips ? 2u : 1u) * (0x10u + 0x2000u);
    if (d->crt_hw_type == 60)
        size += 0x10 + 2048;
    return size;
}

#define COPY_PIECE 0x4000

// The staged image into the target file: from the blocks, or copied from the temp file, in 16 KB pieces
static bool copy_out(const crt_sink_t *k, const char *path, const char *fname, bool from_temp)
{
    uint8_t small[512];
    uint8_t *piece = (uint8_t *)small;
    int piece_len = sizeof(small);
    void *src = NULL;
    if (from_temp) {
        src = file_open(TEMP_DIR, fname, "rb");
        if (!src)
            return false;
        uint8_t *big = (uint8_t *)g_api->mem->malloc(COPY_PIECE);
        if (big) {
            piece = big;
            piece_len = COPY_PIECE;
        }
    }
    bool ok = false;
    void *f = file_open(path, fname, "wb");
    if (f) {
        ok = true;
        if (from_temp) {
            for (uint32_t pos = 0; pos < k->pos && ok; pos += (uint32_t)piece_len) {
                int chunk = (k->pos - pos > (uint32_t)piece_len) ? piece_len : (int)(k->pos - pos);
                ok = file_read(src, piece, chunk) == chunk && file_write(f, piece, chunk) == chunk;
            }
        } else {
            for (uint32_t pos = 0; pos < k->pos && ok; pos += COPY_PIECE) {
                int chunk = (k->pos - pos > COPY_PIECE) ? COPY_PIECE : (int)(k->pos - pos);
                const uint8_t *p = k->blk[pos / STAGE_BLOCK] + pos % STAGE_BLOCK;
                ok = file_write(f, p, chunk) == chunk; // a piece never crosses a block: 64 KB is a multiple of 16 KB
            }
        }
        file_close(f);
    }
    if (src)
        file_close(src);
    if (piece != small)
        g_api->mem->free(piece);
    return ok;
}

// Writes the staged image into the target file. The stick may be gone by now: the image stays (in RAM, or in the temp
// file), the user can plug it again and retry without reading the cartridge a second time.
static FRESULT save_image(cart_dumper_t *d, const crt_sink_t *k, const char *path, const char *fname, bool from_temp)
{
    for (;;) {
        draw_status(d, "Saving to file...");
        if (copy_out(k, path, fname, from_temp))
            return FR_OK;
        LOG("[CartDumper] Writing '%s/%s' failed\n", path, fname);
        draw_status(d, "Saving failed.");
        if (u2p_ui_popup_buttons("Saving failed. Retry?", BUTTON_YES | BUTTON_NO) != BUTTON_YES)
            return FR_DISK_ERR;
    }
}

static FRESULT dump_run(cart_dumper_t *d, void *f, const char *path, const char *fname, const char *crt_name)
{
    FRESULT res = FR_OK;

    bool is_16k = (d->base.det_exrom == 0 && d->base.det_game == 0);
    int det_banks = d->det_banks;
    const int slots = (d->crt_hw_type == CRT_TYPE_EF3) ? EF3_NUM_SLOTS : 1;

    // The whole CRT is read first and written to the target when the cartridge is done: a drive that drops out during
    // the long read cannot spoil the dump. Staged in heap blocks; if the heap has not enough, in a file on the RAM
    // disk /Temp; if that fails too, the target is written while reading.
    static crt_sink_t sink; // the block table is too big for the stack
    memset(&sink, 0, sizeof(sink));
    sink.cap = crt_image_size(d, slots);
    sink.ef1_eapi = ef3_slot_as_ef(d);
    bool from_temp = false;
    if (sink_alloc_blocks(&sink)) {
        file_close(f); // created to see that the drive takes it; opened again to save
        LOG("[CartDumper] Staging in RAM\n");
    } else if ((sink.f = file_open(TEMP_DIR, fname, "wb")) != NULL) {
        file_close(f);
        from_temp = true;
        LOG("[CartDumper] Staging in %s/%s\n", TEMP_DIR, fname);
    } else {
        sink.f = f;
        LOG("[CartDumper] No staging possible, writing while reading\n");
    }
    const bool staged = sink.nblk > 0 || from_temp;

    // Write CRT header
    uint8_t hdr[0x40];
    build_crt_header(d, hdr, crt_name);
    if (!sink_put(&sink, hdr, 0x40)) {
        sink_free_blocks(&sink);
        if (sink.f)
            file_close(sink.f);
        draw_status(d, "Write error (header).");
        return FR_DISK_ERR;
    }

    s_chip_type = (d->crt_hw_type == CRT_TYPE_EF3 || ef3_slot_as_ef(d)) ? 2 : 0;
    cart_ef3_select_slot(&d->base); // EasyFlash 3: the picked slot
    // The whole EasyFlash 3: slot 0 (the system area) to 7, slot-major, bank field = slot * 64 + bank. Slots 1-7 are
    // exactly what the one-slot dump writes as type 32. $DE01 selects the slot, the bank register $DE00 the bank in it.
    for (int slot = 0; slot < slots && res == FR_OK; slot++) {
        if (slots > 1) {
            C64_POKE(EF3_SLOT_REG, (uint8_t)slot);
            memset(d->bank_status_lo, BANK_UNKNOWN, sizeof(d->bank_status_lo));
            memset(d->bank_status_hi, BANK_UNKNOWN, sizeof(d->bank_status_hi));
            char sl[CART_SCREEN_COLS + 1];
            ui_snprintf(sl, sizeof(sl), "Slot %d of 8%s", slot, slot == 0 ? ": system area" : "");
            cart_screen_line(d->window, d->layout.y_slot, 15, sl);
            cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);
        }
        for (int bank = 0; bank < det_banks && res == FR_OK; bank++)
            res = dump_bank(d, &sink, slot, slots, bank, det_banks);
    }

    cart_switch_bank(0, d->crt_hw_type);

    // If GMod2: read EEPROM and append as CHIP packet with load=$DE00
    if (res == FR_OK && d->crt_hw_type == 60) {
        draw_status(d, "Reading EEPROM...");
        uint8_t eeprom_buf[2048];
        if (cart_read_gmod2_eeprom(eeprom_buf)) {
            write_chip(&sink, 0, 0xDE00, 2048, 0, eeprom_buf);
            LOG("[CartDumper] EEPROM data written to CRT (2KB)\n");
            draw_status(d, "EEPROM saved.");
        } else {
            LOG("[CartDumper] No EEPROM data to save\n");
        }
        cart_switch_bank(0, 0);
    }

    // The EEPROM packet is missing when the cartridge had none: the file ends where the image does
    if (staged) {
        if (from_temp)
            file_close(sink.f);
        if (res == FR_OK)
            res = save_image(d, &sink, path, fname, from_temp);
        if (from_temp && res == FR_OK) {
            // There is no delete in the app API: an empty file gives the RAM disk its space back
            void *t = file_open(TEMP_DIR, fname, "wb");
            if (t)
                file_close(t);
        }
        sink_free_blocks(&sink);
    } else {
        file_close(f);
    }

    uint16_t bank_size =
        (is_16k || d->crt_hw_type == 8 || d->crt_hw_type == 32 || d->crt_hw_type == CRT_TYPE_EF3) ? 0x4000 : 0x2000;
    int all_banks = det_banks * slots;
    int total_kb = (all_banks * bank_size) / 1024;

    if (res == FR_OK) {
        char msg[60];
        ui_snprintf(msg, sizeof(msg), "Done: %d bank%s, %dKB saved.", all_banks, all_banks == 1 ? "" : "s",
                    total_kb);
        draw_status(d, msg);
    } else {
        draw_status(d, "DUMP ERROR!");
    }
    return res;
}

// =========================================================================
//  Drawing
// =========================================================================

static void draw_header(cart_dumper_t *d)
{
    if (!d->window)
        return;
    cart_layout_compute(&d->layout, d->det_banks > 0 ? d->det_banks : 1, d->has_romh, d->base.is_ef3);

    uint16_t bank_size = d->has_romh ? 0x4000 : 0x2000;
    int total_kb = (d->det_banks * bank_size * (d->base.ef3_whole ? EF3_NUM_SLOTS : 1)) / 1024;
    const char *name = d->probe_result.type_name ? d->probe_result.type_name : "Cart";
    char line[CART_SCREEN_COLS + 1];
    ui_snprintf(line, sizeof(line), "TREX Cart Dumper - %s (%dKB)", name, total_kb);
    cart_screen_line(d->window, d->layout.y_header, 1, line);

    // EasyFlash 3: the slot to dump
    if (d->base.is_ef3) {
        if (d->base.ef3_whole) {
            ui_snprintf(line, sizeof(line), "All slots, system area first");
        } else if (d->base.ef3_slot) {
            ui_snprintf(line, sizeof(line), "Slot %d%s%s", d->base.ef3_slot, d->base.ef3_name[0] ? ": " : "",
                        d->base.ef3_name);
        } else {
            ui_snprintf(line, sizeof(line), "No slot selected");
        }
        cart_screen_line(d->window, d->layout.y_slot, 15, line);
    }
    cart_screen_file(d->window, &d->layout, d->file);
}

static void draw_status(cart_dumper_t *d, const char *msg)
{
    cart_screen_status(d->window, &d->layout, msg);
}

static void draw_footer(cart_dumper_t *d)
{
    static const char *const keys[] = { "F1", "Scan", "F3", "Dump", "F5", "Inspect", "R/S", "Exit" };
    static const char *const slot_keys[] = { "F7", "Select slot/full" };
    if (!d->window)
        return;
    cart_screen_keys(d->window, d->layout.y_keys, keys, 4);
    if (d->base.is_ef3)
        cart_screen_keys(d->window, d->layout.y_keys2, slot_keys, 1);
}

void cart_dumper_redraw(cart_dumper_t *d)
{
    if (!d->window)
        return;
    window_clear(d->window);
    draw_header(d);
    cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);
    draw_footer(d);
}

// Scans all banks again (EasyFlash 3: of the picked slot) and shows done when finished
static void rescan(cart_dumper_t *d, const char *done)
{
    draw_status(d, "Scanning...");
    memset(d->bank_status_lo, BANK_UNKNOWN, sizeof(d->bank_status_lo));
    memset(d->bank_status_hi, BANK_UNKNOWN, sizeof(d->bank_status_hi));
    cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);
    cart_ef3_select_slot(&d->base);
    cart_scan_all_banks(d->bank_status_lo, d->bank_status_hi, d->det_banks, d->has_romh, d->romh_addr);
    cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);
    draw_status(d, done);
}

// =========================================================================
//  init / deinit / poll
// =========================================================================

void cart_dumper_init(cart_dumper_t *d)
{
    memset(d, 0, sizeof(*d));
    d->romh_addr = 0xA000;

    d->window = cart_screen_open();
    window_clear(d->window);
    u2p_ui_set_redraw(rebuild, d);

    if (!cart_session_open(true)) {
        draw_header(d);
        draw_footer(d);
        draw_status(d, cart_session_status());
        return;
    }

    if (!cart_detect_present(&d->base)) {
        draw_header(d);
        draw_footer(d);
        draw_status(d, "No cartridge detected.");
        cart_notify_no_cartridge();
        return;
    }

    if (!cart_check_dma_access()) {
        draw_header(d);
        draw_footer(d);
        draw_status(d, "No cart access: set Cart Pref Internal");
        return;
    }

    d->has_romh = (d->base.det_exrom == 0 && d->base.det_game == 0) || // 16K
                  (d->base.det_exrom == 1 && d->base.det_game == 0);   // Ultimax
    d->romh_addr = (d->base.det_exrom == 1 && d->base.det_game == 0) ? 0xE000 : 0xA000;

    // Probe cart type (save result for display, but don't ask user yet)
    d->probe_result = probe_cart_type(d);
    d->crt_hw_type = d->probe_result.hw_type;

    // EasyFlash banks are read in Ultimax mode (set by the probe): ROMH is at $E000 whatever the lines showed
    if (d->probe_result.hw_type == 32) {
        d->has_romh = true;
        d->romh_addr = 0xE000;
    }

    // Use bank count from probe (already determined via wrap-around)
    d->det_banks = d->probe_result.banking_found ? d->probe_result.bank_count : 1;
    // The bank maps hold MAX_CART_BANKS entries; a larger wrap count cannot be switched through $DE00 anyway
    if (d->det_banks > MAX_CART_BANKS)
        d->det_banks = MAX_CART_BANKS;
    LOG("[CartDumper] Probe: %s, %d banks, has_romh=%d\n", d->probe_result.type_name, d->det_banks, d->has_romh);

    // EasyFlash 3: one EasyFlash per slot, the user picks the slot to dump
    if (d->base.is_ef3) {
        cart_ef3_pick_slot(&d->base, EF3_PICK_WHOLE);
    }

    // Show UI and scan
    draw_header(d);
    draw_status(d, "Scanning...");
    draw_footer(d);
    if (d->base.is_ef3 && !d->base.ef3_slot && !d->base.ef3_whole) {
        draw_status(d, "No slot selected. F7=Select slot");
        return;
    }
    cart_ef3_select_slot(&d->base);
    cart_scan_all_banks(d->bank_status_lo, d->bank_status_hi, d->det_banks, d->has_romh, d->romh_addr);
    cart_draw_bank_map(d->window, &d->layout, d->bank_status_lo, d->bank_status_hi);

    char msg[60];
    ui_snprintf(msg, sizeof(msg), "%s - F3=Dump", d->probe_result.type_name);
    draw_status(d, msg);
}

void cart_dumper_deinit(cart_dumper_t *d)
{
    u2p_ui_set_redraw(NULL, NULL);
    cart_ef3_restore_slot(&d->base);
    cart_session_close();
    window_close(d->window);
    d->window = NULL;
}

int cart_dumper_poll(cart_dumper_t *d, int c)
{
    switch (c) {
    case KEY_F1:
        if (d->base.is_ef3 && !d->base.ef3_slot && !d->base.ef3_whole) {
            draw_status(d, "No slot selected. F7=Select slot");
        } else if (d->det_banks > 0) {
            rescan(d, "Scan complete.");
        }
        break;

    case KEY_F3:
        if (d->base.is_ef3 && !d->base.ef3_slot && !d->base.ef3_whole) {
            draw_status(d, "No slot selected. F7=Select slot");
        } else if (d->det_banks > 0) {
            do_dump(d);
        }
        break;

    case KEY_F5:
        if (d->det_banks > 0) {
            draw_status(d, "Use Cart Inspector from menu.");
        }
        break;

    case KEY_F7:
        if (d->base.is_ef3 && cart_ef3_pick_slot(&d->base, EF3_PICK_WHOLE) != EF3_PICKED_NONE) {
            cart_dumper_redraw(d);
            rescan(d, "EasyFlash 3 - F3=Dump");
        }
        break;

    case KEY_ESCAPE:
    case KEY_BACK:
    case KEY_BREAK:
        return MENU_CLOSE;
    }
    return 0;
}
