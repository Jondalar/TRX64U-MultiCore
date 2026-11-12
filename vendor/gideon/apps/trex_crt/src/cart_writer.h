/*
 * cart_writer.h
 *
 *  Flash .CRT files to physical flash cartridges (EasyFlash, EasyFlash 3,
 *  GMod2, MegaByter, C64MegaCart) via the U64 cart slot. Port of the firmware
 *  CartWriter (software/io/c64/cart_writer.h).
 */

#ifndef CART_WRITER_H
#define CART_WRITER_H

#include "cart_base.h"
#include "cart_screen.h"

// Flash mode control: how to enter Ultimax/flash mode
typedef enum {
    FLASH_MODE_DE02,   // MegaByter: $DE02=val
    FLASH_MODE_DF00,   // C64MegaCart: $DF00=val
    FLASH_MODE_DE00,   // GMod2: $DE00=$C0 (bit7+bit6)
    FLASH_MODE_DE02_EF // EasyFlash: $DE02=$05 (MXG=101)
} flash_mode_type_t;

typedef struct
{
    const char *name;
    uint16_t cmd_addr_1;  // unlock addr 1 (ROML chip)
    uint16_t cmd_addr_2;  // unlock addr 2 (ROML chip)
    uint16_t cmd_addr_h1; // unlock addr 1 for ROMH chip (EF only, 0=no ROMH)
    uint16_t cmd_addr_h2; // unlock addr 2 for ROMH chip (EF only)
    flash_mode_type_t mode_type;
    uint8_t ultimax_val;
    uint8_t normal_val;
    int max_banks;
    int total_size;
    uint16_t crt_type;
    bool has_romh; // true if cart has separate ROMH chip (EasyFlash)
} flash_chip_info_t;

typedef struct
{
    cart_base_t base;
    Window *window;

    // CRT file info
    char crt_path[256];
    char crt_filename[256];
    uint8_t *crt_data; // the whole CRT, read once when the writer opens (NULL when it is streamed)
    uint32_t crt_size;
    bool crt_stream;   // the CRT is read from the file by every pass (our core, or too big for the heap)

    // Detected cart
    const flash_chip_info_t *chip;

    // Bank map (separate arrays for ROML and ROMH chips)
    uint8_t bank_status_lo[MAX_CART_BANKS];
    uint8_t bank_status_hi[MAX_CART_BANKS];
    int num_banks_found;

    // CRT data
    int crt_num_chips;
    int crt_total_bytes;
    char crt_name[33];
    uint16_t crt_type;                // hardware type from the CRT header
    bool crt_has_eeprom;              // the CRT holds GMod2 EEPROM contents (load $DE00)
    char ef_name[EF3_NAME_LEN + 1];   // "EF-Name:" found while writing (EasyFlash 3), empty if none

    int ef_current_bank; // target bank for EF byte programming

    // Where each part of the screen goes
    cart_layout_t layout;
} cart_writer_t;

// Checks a CRT file before the writer opens on it (header, type, whole chip packets, banks, at least one chip).
// NULL = ok, else the reason for a popup. Reads the file only.
const char *cart_writer_check_crt(const char *path, const char *filename);
// Opens the window, sets up the bus, detects the flash cart and reads the
// CRT summary. An empty filename opens the scan-only view.
void cart_writer_init(cart_writer_t *w, const char *path, const char *filename);
// Restores the bus and closes the window
void cart_writer_deinit(cart_writer_t *w);
// Handles one keymapped key; returns MENU_CLOSE to leave
int cart_writer_poll(cart_writer_t *w, int key);
void cart_writer_redraw(cart_writer_t *w);

#endif
