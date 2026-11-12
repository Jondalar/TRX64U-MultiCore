// The ROM manager (T93 D5, S2): where a core's ROM files are looked for, which file wins, what is said when none does, and
// how often it is looked again. Pure: the file system is a CoreFs, the clock is the caller's.
#ifndef COREAPP_ROMFIND_H
#define COREAPP_ROMFIND_H

#include "desc.h"

// The roots, in order: the flash first (the files a user keeps with the machine), then the sticks, then the card. Each
// root is searched under "<root>/roms/<folder>".
extern const char *const rom_roots[];      // "/flash", "/usb0" ... "/usb3", "/sd", null-terminated

enum RomStatus {
    ROM_FOUND = 0,
    ROM_ABSENT,          // not required and no file of any of its names: fine
    ROM_MISSING,         // required, no file of any name in any root
    ROM_WRONG_SIZE,      // a file of the right name exists but not of an allowed size (required: judged; optional: only out[i] says so, T185 S3)
    ROM_BAD_CONTENT      // a file of the right name and size exists but the core's check refuses its content (same)
};

struct RomHit {
    char dir[96];
    char name[56];
    uint32_t size;
};

struct RomResult {
    RomStatus status;
    RomHit hit;          // valid for ROM_FOUND
    char why[160];       // an optional ROM (not required, no set) whose file was refused (status ROM_WRONG_SIZE / ROM_BAD_CONTENT): the reason, as the summary would tell it; else ""
};

// One pass over all specs. out[i] for spec i. Returns the worst status among the required ROMs (ROM_FOUND when all are
// there). ROMs with a `set` (desc.h) are judged together: the result is ROM_FOUND when at least one set is complete, else the status of
// the first set's first ROM that is not found. summary gets one line for the first ROM that is not found, "" when all are. The roots are tried in the order of
// rom_roots, and within a root the names in the spec's order; the first file of an allowed size (and with an accepted content)
// wins. A file with the right name and a wrong size is remembered for the message only.
//
// first_dir (T104.2 D8): a directory searched before all the others, with the files in it as they are. The monolith has Gideon's layout
// there (/flash/roms/kernal.bin ...). The order is first_dir, then <root>/roms/<folder> of the roots in rom_roots - /flash first, so
// the files in the flash win over a stick, as on every other board. Null: only the roots.
//
// own_dir (T148 D2): the roms folder of the core's folder, "<dir>/<name>/roms" for "<dir>/<name>.cor" (rom_own_dir). When it is given the
// core reads ROMs from it ALONE: first_dir, the roots and the shared folders are not listed, and the message names this one folder (D4).
// Files elsewhere get there by rom_import. Null: the old search over first_dir and the roots (no .cor path: a developer start, the monolith
// without a hand-over).
RomStatus rom_scan(CoreFs *fs, const char *folder, const RomSpec *specs, int n, RomResult *out, char *summary, int sumlen,
                   const char *first_dir = 0, const char *own_dir = 0);

// "<dir>/<name>.cor" -> "<dir>/<name>/roms" into out (corefolder.h). False (out empty) when the path is empty, relative, not a .cor, has no
// folder below a medium, or does not fit.
bool rom_own_dir(const char *cor_path, char *out, int outlen);

// T148 D2: the one-time copy into the core folder. For every spec that has no file of one of its names in own_dir, the old places are looked
// at in the old order - first_dir, <root>/roms/<folder> of every root, for a `shared` spec also <root>/roms, and old_roms (the roms folder
// beside the .cor, shared by every core there: only a file the spec NAMES, like everywhere) - and the first file of an allowed size and with an
// accepted content is copied to own_dir under its own name, verified by size and SHA-256 (corefolder_copy_verified), the original untouched.
// A name that already lies in own_dir (a wrong size too) is never overwritten. Returns the number of files copied.
int rom_import(CoreFs *fs, const char *folder, const RomSpec *specs, int n, const char *own_dir, const char *first_dir, const char *old_roms);

// The one buffer the ROM manager reads files into (512 KiB), lent to the ROM set's import so the application holds one.
uint8_t *rom_scratch(uint32_t *size);

// Reads the file of a hit into buf (at least hit.size bytes). False when it cannot be read any more (stick pulled).
bool rom_read(CoreFs *fs, const RomHit *hit, uint8_t *buf);

// The wait loop's policy. The caller scans, calls step(), shows the text when step() says so, sleeps delay_ms(), scans again.
class RomWait {
public:
    RomWait();
    // true: show msg now. A missing file is told after 10 s (the sticks have had their time), a file that is there but wrong
    // at once; the same text is not told twice. A pass with status ROM_FOUND resets everything.
    bool step(RomStatus worst, const char *msg);
    uint32_t delay_ms() const { return 500; }
    int tries() const { return tries_; }
    bool told() const { return told_; }
private:
    int tries_;
    bool told_;
    uint32_t last_;                  // hash of the text last told (the texts are longer than any buffer worth keeping)
};

#endif
