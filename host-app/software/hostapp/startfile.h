// T203 (issue #69): what the host saw while it decided how to start, kept in RAM during the start and written once to the SD card as one block of
// text in startlog.txt (the last STARTFILE_KEEP starts). Pure code: the clock, the SD card and the keys' raw bytes come in through the arguments, so
// tests/hostapp/test_startfile.cc runs the same functions hostui.cc and menukeys.cc call. Nothing here touches the flash.
#ifndef HOST_STARTFILE_H
#define HOST_STARTFILE_H

#include <stdint.h>
#include "directboot.h"

#define STARTFILE_DIR   "/SD/report"           // the folder the bug reports already use on the SD card
#define STARTFILE_NAME  "startlog.txt"
#define STARTFILE_KEEP  10                     // starts kept in the file
#define STARTFILE_MAX   8192                   // the file never grows past this; the oldest blocks go first
#define STARTREC_BODY   1024                   // one block's text

// The medium, as hostfs.cc has it. load returns the length or -1; a missing SD card (make_dir false) or a save that fails skips the file silently.
struct StartLogFs {
    bool (*make_dir)(void *ctx, const char *dir);
    int (*load)(void *ctx, const char *path, char *buf, int max);
    bool (*save)(void *ctx, const char *dir, const char *name, const void *buf, int len);
    void *ctx;
};

void startrec_init(uint32_t (*clock)(void));                   // a new start: forget everything; the clock is ms since the host began
// menukeys.cc: one look at the keys. joy = the port-1 byte as read (low = pressed, bit 4 = fire), row = the keyboard row of column 7 (bit 5 low = C=).
void startrec_key(uint8_t joy, uint8_t row);
bool startrec_cfg(int n, int def, int direct, int countdown);   // boot.cfg was read (try_load_cfg); true: a line was added (a read after the decision)
void startrec_window_open(uint32_t ms);                        // the decision begins: the key window of `ms` runs from now
void startrec_window_close(void);                              // the key window loop ended
// The decision: builds the block and, for DB_START, the line "started by: direct start".
void startrec_decide(const DirectIn &in, DbReason why);
// A later event with its time ("started by: countdown", "direct start cancelled ..."); needs startrec_decide before it.
void startrec_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool startrec_decided(void);
const char *startrec_body(void);

// Writes the block: the file is read, the block replaces this start's earlier one (or is added with the next number), the oldest beyond STARTFILE_KEEP
// go, the file is saved. False: nothing written (no SD card, read-only); the block stays in RAM and the next call tries again.
bool startfile_store(const StartLogFs *fs, const char *app, const char *built, const char *clock);

#endif
