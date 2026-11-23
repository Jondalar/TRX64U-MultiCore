// The application's code that libexports.cc calls, as a model for tests/loader/test_exports.cc: every function records its call in `log` and answers what the test set. The
// real code (c64_cart.cc, cbm1541.cc, d1541_drive.cc, uci.cc, tempdisk.cc, writeback.cc) needs the board or a model of the machine; what is under test here is the table.
#ifndef TESTS_LOADER_EXPORTS_STUB_H
#define TESTS_LOADER_EXPORTS_STUB_H

#include <stdint.h>
#include <string>
#include "uci.h"

class CbmDrive;

struct ExportsStub {
    std::string log;                       // "name(args);" of every call, in order
    const CoreCtx *ctx = 0;                // the context the last call got
    bool c64core = false;                  // appsvc_c64: the core is the C64 (the cartridge port of the C64 family)
    bool cart_active = false, wb_active = false, ef_ok = true, player = false, pending = false, service_work = false;
    std::string cart_name = "", cart_path = "";
    int crt_type = 0;
    uint32_t image_bytes = 0;
    int rc = 0;                            // what every int function answers
    bool msg_null = false;                 // the last call that takes a message buffer got a non-null buffer of room
    CbmDrive *drive = 0;                   // cbm1541_drive: null = the core has no drive
    std::string slot_spec = "";
    UciHost host;                          // what uci_start was given (a copy)
    const UciHost *host_live = 0;          // the same, not a copy: what the application keeps
    int uci_starts = 0;
    int temp_auto = -1, temp_sub = -1;
};
extern ExportsStub g_stub;

#endif
