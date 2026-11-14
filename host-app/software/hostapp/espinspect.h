// T152 D8/S9: does a .cfw/.ue2 carry an ESP32 image set, and which version? Read-only: the file is scanned, nothing is written anywhere.
// An ESP32 image set is what the vendor updater writes to the ESP32-S3 (TRX64-Ultimate TRX64-Ultimate/docs/specs/T26.0-esp32-facts.md section 2): the bootloader image, the
// partition table and the application image `u64ctrl`. All three are found by their structure, not by a name or an offset of a particular vendor file:
//   image     0xE9, 1..16 segments, SPI mode 0..4, chip id 0x0009 (ESP32-S3) at byte 12; its end follows from the segment lengths, the SHA-256 appended
//             behind it (header byte 23) is compared with the bytes of the image
//   app       an image whose first segment starts with the esp_app_desc magic 0xABCD5432
//   ptable    entries 0xAA 0x50 ... with an app/factory entry at 0x10000
//   version   the text "ESP32 WiFi Bridge Vx.y" (the application's IDENT_STRING) inside the application image
#ifndef HOST_ESPINSPECT_H
#define HOST_ESPINSPECT_H

#include <stdint.h>
#include "cor.h"

enum EspResult {
    ESP_FOUND = 0,         // bootloader, partition table and a complete application image: a set the vendor updater would write
    ESP_NONE,              // no ESP32 image in the file
    ESP_INCOMPLETE,        // an application image, but no bootloader or no partition table in front of it
    ESP_DAMAGED,           // the application image's SHA-256 does not match its bytes
    ESP_IO,                // unreadable
    ESP_ABORTED
};

struct EspInfo {
    bool boot, ptable, app;
    bool digest_ok;
    char version[16];      // "V1.11", "" when the application carries no IDENT_STRING
    char project[24];      // the esp_app_desc project name ("u64ctrl")
    uint32_t boot_off, ptable_off, app_off, app_len;
    uint32_t boot_len, ptable_len;     // what the vendor updater writes: the image and its SHA-256 / the 0xC00 bytes of the partition table (less if the file ends sooner)
};

typedef void (*EspProgress)(void *ctx, uint32_t done, uint32_t total);
EspResult esp_inspect(CorSource &src, EspInfo *out, EspProgress cb, void *ctx, bool (*abort)(void *));
const char *esp_result_text(EspResult r);

#endif
