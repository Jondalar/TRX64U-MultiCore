// Official firmware files the host recognises by the SHA-256 of the whole file (T137 D3, Q6): a converted .cor carries the name and version of its row.
// The rows were made on 2026-10-29 from files that lay on the machine of the one who builds this (no download, nothing typed from memory);
// where each lay is in docs/specs/done/T137-host-convert-and-restore.md section 5. Every one of them parses as a firmware for the XC7A50T (cfw_probe).
// A row means "named like the vendor's release and found in the place such a release goes"; that is a recognition, not a signature (Q5).
// The table ends with a row of zeros.
#ifndef HOST_KNOWN_FW_H
#define HOST_KNOWN_FW_H

#include "fwknown.h"

#define FW_KNOWN_STR2(x) #x
#define FW_KNOWN_STR(x) FW_KNOWN_STR2(x)

static const FwKnown known_fw[] = {
    { "487a7d2104d6d32f61f33ca36a16439d947cd1b0eab75d5b1e78248de51bb58d", "Commodore", "64 Ultimate", "1.1.0" },     // c64u_v1.1.0.ue2
    { "09f77e951b1be2f091c01c1a20d4817736442c05eb72dfe303698f00351bf828", "Gideon", "Ultimate firmware", "3.14" },     // update_v3.14.ue2
    { "7db93be62acf6d9876d1e21543b5239953881bb63ac9507e3c4f094b51b03dfa", "Gideon", "Ultimate firmware", "3.14d" },    // update_v3.14d.ue2
    { "4a05af5876edffdf8a90ab4ce3c140a2daa26fb85160203e1086ac411fb0f345", "Gideon", "Ultimate firmware", "3.12a" },    // update_v3.12a-15.ue2
#ifdef FW_TEST_SHA                 // the render bench adds one row for its synthetic vendor file (the hash as a bare token; never defined in a build for a board)
    { FW_KNOWN_STR(FW_TEST_SHA), "Test", "Vendor image", "9.9" },
#endif
    { 0, 0, 0, 0 }
};

#endif
