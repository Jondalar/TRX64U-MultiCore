// The Ultimate Command Interface ($DF1C) as the Ultimate firmware serves it: the server (host-app/software/io/command_interface/command_intf.cc, the firmware's
// io/command_interface/command_intf.cc), the DOS targets 1 and 2 (the firmware's filemanager/dos.cc, over the FileManager), the CONTROL target 4 (control_target.cc,
// the commands the machine can answer) and the NETWORK target 3 (network_target.cc, verbatim). It belongs here, in the generic app, because the interface is a machine
// function of the cartridge port that every core with the "uci" capability shares; what is the machine's own (the REU, the drives) comes through UciHost.
#ifndef COREAPP_UCI_H
#define COREAPP_UCI_H

#include "ctx.h"

#ifndef DRVTYPE_MP3_DNP
#define DRVTYPE_MP3_DNP 31                    // io/c64/c64.h:169, the RAM drive type of a MegaPatch 3 native drive
#endif

struct UciHost {
    // The REU of the machine, for CONTROL LOAD_REU / SAVE_REU: 0 and size, offset (bytes) and the image path; -2 the REU is not enabled; -4 its memory is in use
    // (busy has the reason).
    int (*reu)(CoreCtx *c, uint32_t *size, uint32_t *offset, char *img, int imgn, char *busy, int busyn);
    // Drive idx (0 = A, 1 = B): false when there is none. type: the drive's type in the encoding of its type register (0 1541, 2 1581; 1, the 1571, is out: T103 Q3), addr the IEC address.
    bool (*drive_info)(CoreCtx *c, int idx, uint8_t *type, uint8_t *addr, bool *power);
    // what: 0 mount path, 1 eject, 2 swap, 3 power off, 4 power on. 0 = done, else the reason is in msg.
    int (*drive_do)(CoreCtx *c, int idx, int what, const char *path, char *msg, int msglen);
    // CONTROL GET_HWINFO device 1 (the SID map, control_target.cc:290-340): fills data (at most 16 bytes) as the firmware does and returns its length, 0 when the machine has none.
    int (*sid_map)(CoreCtx *c, uint8_t *data);
    // CONTROL U64_SAVEMEM: the file the 64 KB go to when the command names none (the firmware's default, a name of the machine's own).
    const char *memory_file;
    // CONTROL EASYFLASH 0: clears the 8 banks of 8 KB of the cartridge in the port that start at bank (a multiple of 8), the ROMH half when high. False: no cartridge.
    bool (*ef_clear)(CoreCtx *c, uint8_t bank, bool high);
    // The MegaPatch 3 RAM drive n (0..3) in the REU: type 0 none, 1541, 1571, 1581 or DRVTYPE_MP3_DNP; size in bytes; base the REU offset of its first byte.
    void (*ramdrive)(CoreCtx *c, int n, int *type, uint32_t *size, uint32_t *base);
    // The palette (CONTROL 0x51-0x54): what 0 reads all 16 colours into rgb, 1 sets all, 2 sets colour idx from rgb[0], 3 resets to the default colours.
    void (*palette)(CoreCtx *c, int what, uint8_t idx, uint8_t (*rgb)[3]);
};

// Starts the command interface once, when the bitstream has it (CAPAB_COMMAND_INTF and CAPAB_CARTRIDGE); does nothing otherwise and on a second call.
void uci_start(CoreCtx *c, const UciHost *host);

// CONTROL FREEZE (the firmware's C64_PUSH_BUTTON): a request for the run loop, which opens the menu as the menu button does. True once per request.
bool uci_take_freeze_request(void);

#endif
