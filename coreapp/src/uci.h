// The Ultimate Command Interface ($DF1C) as the Ultimate firmware serves it: the server (host-app/software/io/command_interface/command_intf.cc, the firmware's
// io/command_interface/command_intf.cc), the DOS targets 1 and 2 (the firmware's filemanager/dos.cc, over the FileManager), the CONTROL target 4 (control_target.cc,
// the commands the machine can answer) and the NETWORK target 3 (network_target.cc, verbatim). It belongs here, in the generic app, because the interface is a machine
// function of the cartridge port that every core with the "uci" capability shares; what is the machine's own (the REU, the drives) comes through UciHost.
#ifndef COREAPP_UCI_H
#define COREAPP_UCI_H

#include "ctx.h"

struct UciHost {
    // The REU of the machine, for CONTROL LOAD_REU / SAVE_REU: 0 and size, offset (bytes) and the image path; -2 the REU is not enabled; -4 its memory is in use
    // (busy has the reason).
    int (*reu)(CoreCtx *c, uint32_t *size, uint32_t *offset, char *img, int imgn, char *busy, int busyn);
    // Drive idx (0 = A, 1 = B): false when there is none. type: the drive type register (0 1541, 1 1571, 2 1581), addr the IEC address.
    bool (*drive_info)(CoreCtx *c, int idx, uint8_t *type, uint8_t *addr, bool *power);
    // what: 0 mount path, 1 eject, 2 swap, 3 power off, 4 power on. 0 = done, else the reason is in msg.
    int (*drive_do)(CoreCtx *c, int idx, int what, const char *path, char *msg, int msglen);
};

// Starts the command interface once, when the bitstream has it (CAPAB_COMMAND_INTF and CAPAB_CARTRIDGE); does nothing otherwise and on a second call.
void uci_start(CoreCtx *c, const UciHost *host);

#endif
