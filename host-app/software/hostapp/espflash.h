// T152 S8: write the ESP32 image set of a .cfw/.ue2 (found by espinspect.cc) into the ESP32's own flash through its ROM loader. The FPGA flash is not
// touched: no Flash object appears in this interface. The protocol is the vendor updater's (esp32.cc Download() / Flash(), update_u64ii.cc update_esp32_impl()):
// SYNC, ATTACH_SPI, CHANGE_BAUDRATE 2 Mbit/s, SET_FLASH_PARAMS, then per image FLASH_BEGIN and 1 KiB FLASH_DATA blocks (bootloader 0x0, partition table 0x8000,
// application 0x10000), then the module is put back to run mode and asked who it is (CMD_IDENTIFY): the version it reports has to be the file's.
// The module owns the protocol and every refusal text; the link below is only bytes, pins and baud rate (espflash_board.cc on the board, a ROM loader model in the tests).
#ifndef HOST_ESPFLASH_H
#define HOST_ESPFLASH_H

#include <stdint.h>
#include "espinspect.h"

#define ESPF_BLOCK       0x400u
#define ESPF_ADDR_BOOT   0x000000u
#define ESPF_ADDR_PTABLE 0x008000u
#define ESPF_ADDR_APP    0x010000u
#define ESPF_BAUD        2000000u

struct EspFlashLink {
    virtual ~EspFlashLink() { }
    virtual void arm(bool on) = 0;                                          // esp_ctl (0x10060910): the app drives WIFI_BOOT / WIFI_DIS only while armed
    virtual bool enter_loader() = 0;                                        // module off, 115200 baud, boot mode; true when the ROM printed its "DOWNLOAD" message
    virtual void send_packet(const uint8_t *p, uint32_t n) = 0;             // one SLIP packet
    virtual int recv_packet(uint8_t *p, uint32_t cap, int timeout_ms) = 0;  // one SLIP packet; <= 0 when none came in time
    virtual void set_baud(uint32_t bps) = 0;
    virtual void leave_loader() = 0;                                        // run mode, the WiFi application started again
    virtual bool identify(uint16_t *major, uint16_t *minor) = 0;           // CMD_IDENTIFY; false = no answer
};

enum EspFlashStage { EFS_ENTER = 0, EFS_SYNC, EFS_SETUP, EFS_BOOT, EFS_PTABLE, EFS_APP, EFS_RUN, EFS_STAGES };
enum EspIdent { EFI_NOT_ASKED = 0, EFI_MATCH, EFI_NO_ANSWER, EFI_DIFFERS };

struct EspFlashResult {
    bool ok;
    bool started;                  // the first FLASH_BEGIN was sent: the ESP32's flash is changed (the ROM loader itself cannot be damaged: the write can be repeated)
    int stage;                     // where it stopped (EspFlashStage)
    const char *why;               // static text, the refusal or failure as the page says it
    int ident;                     // EspIdent
    uint16_t major, minor;         // what the module reported
};

// done / total in 1 KiB blocks of the three images together (0 / total while the stages before them run)
typedef void (*EspFlashProgress)(void *ctx, int stage, uint32_t done, uint32_t total);

// The whole set from `src` (the file inspect looked into) per `info`, which must be a complete, intact set (ESP_FOUND). The link is always left in run mode and
// disarmed, whatever happened.
bool esp_flash(EspFlashLink &link, CorSource &src, const EspInfo &info, EspFlashProgress cb, void *ctx, EspFlashResult *res);

// The version text of the file ("V1.11") as major / minor; false for a text that is not of that form.
bool esp_version_parse(const char *v, uint16_t *major, uint16_t *minor);

// The link of the board (espflash_board.cc); the render bench and the tests bring their own.
EspFlashLink *esp_flash_board_link(void);

#endif
