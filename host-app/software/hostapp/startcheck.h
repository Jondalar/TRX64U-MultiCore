// What the boot menu checks of an entry (T101 S10, T115 section 7). Pure code: the file system comes through StartFs, so the bench
// counts every byte read and every call and the board runs the same code over the FileManager (hostui.cc).
//
//   at ADD   cor_full_check: every part read and hashed once; the entry stores size, mtime and the header digest.
//   at START startcheck_entry: (a) size and mtime of the directory entry, (b) the package header (cor_probe: manifest, contract, host
//            key, the SHA values). No part is read, no directory is listed, nothing is searched for.
#ifndef HOST_STARTCHECK_H
#define HOST_STARTCHECK_H

#include <stdint.h>
#include "bootcfg.h"
#include "cor.h"

struct FileStat {
    uint32_t size;
    uint32_t mtime;           // FAT date << 16 | time
};

class StartFs {
public:
    virtual ~StartFs() { }
    virtual bool stat(const char *path, FileStat *st) = 0;
    virtual CorSource *open(const char *path) = 0;     // NULL = cannot open; the caller deletes it
    virtual int list(const char *dir) = 0;             // a directory listing. NEVER called by the start check; it is here so the bench can prove that
};

enum EntryCheck {
    EC_OK,            // size, mtime and header are the ones stored: the start may trust the stored hash
    EC_CHANGED,       // the file or its header is not what the entry stored (a start checks every part)
    EC_MISSING,       // no such file
    EC_BAD,           // not a package (or unreadable)
    EC_CONTRACT,      // a package for another host contract
    EC_HOST,          // routed against another host
    EC_UNCHECKED      // an entry from an older boot.cfg: no mtime stored yet (startcheck_migrate does the full check once)
};

// A .cor entry. ci receives the header, st the directory entry. Reads the header and the manifest, nothing else.
EntryCheck startcheck_entry(const BootEntry &e, StartFs &fs, CorInfo &ci, FileStat &st);

// The one rule both the menu badge and the start use: size, mtime and header digest of the file are the stored ones, so the stored
// hash stands in for reading the parts again.
bool startcheck_trusts(const BootEntry &e, const CorInfo &ci, const FileStat &st);

// A .cfw entry (its content is parsed by cfw.cc, not here): only (a). EC_OK, EC_CHANGED, EC_MISSING or EC_UNCHECKED.
EntryCheck startcheck_firmware(const BootEntry &e, StartFs &fs, FileStat &st);

// The full check: every part of the package read and hashed against the manifest. For adding an entry, and the one time an
// older entry gets its stored fields.
cor_result_t cor_full_check(CorSource &src, const CorInfo &ci);

// An entry without mtime: the full check, then size, mtime and header digest are stored. A firmware image (fw) has no hash; it only gets
// size and mtime. True if the entry now carries the fields; false leaves it as it was.
bool startcheck_migrate(BootEntry &e, StartFs &fs, bool fw);

#endif
