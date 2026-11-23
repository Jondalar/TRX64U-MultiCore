// Cores and ROMs maintenance (T148 D7): what the media hold of the cores, what is missing, what belongs to no core, what is there twice -
// and the removal of what belongs to no core. Pure code over CmFs (the listing, the manifest name) and FoFs (stat, read, delete):
// hostui.cc's F8 page runs it on the FileManager, tests/hostapp/test_coremaint.cc on a RAM disk.
//
// A medium holds "<medium>/cores/<core>.cor" and, beside it, the core's folder "<medium>/cores/<core>/" with <core>.cfg, roms/ and saves/
// (spec T148 D1). The report is made by cm_scan; cm_delete removes the entries of the report that are marked deletable and nothing else.
#ifndef HOST_COREMAINT_H
#define HOST_COREMAINT_H

#include <stdint.h>
#include "fileops.h"

#define CM_PATH 120
#define CM_CORES 24
#define CM_ORPHANS 80
#define CM_DUPS 16
#define CM_FILES 160          // ROM files looked at for duplicates

struct CmEntry {
    char name[56];
    bool dir;
    uint64_t size;
};

struct CmFs {
    virtual ~CmFs() { }
    virtual int list(const char *dir, CmEntry *out, int max) = 0;                    // "/" lists the media; -1 if it cannot be read
    virtual bool core_name(const char *cor_path, char *name, int n) = 0;             // the manifest's name of a .cor; false if it cannot be read
};

// What a core needs (romreq_table.inc, generated from the descriptors: tests/coreapp/gen_romreq.cc).
struct CmReq {
    const char *core;         // the manifest name
    const char *folder;
    const char *role;
    const char *names;        // "a.rom|b.rom"; for a ROM set the extension
    uint32_t sizes[3];
    int flags;                // 1 required, 2 shared device ROM, 4 one file of the extension out of a set, 8 a part of another ROM's image (bits 4 and up: that ROM's number in the core's rows, from 1)
    int set;                  // bit mask of the variants that need it (0: not in a set)
};

struct CmCore {
    char medium[12];          // "SD"
    char file[56];            // "apple2.cor"
    char name[48];            // the manifest's name, "" when the .cor could not be read
    bool folder;              // <core>/ exists beside the .cor
    int need, have;           // ROM files the core must have (a set of variants counts once), and how many of them are in <core>/roms/
    char missing[120];        // what is not there, "" when all are
    int saves;                // files in <core>/saves/
};

enum CmKind { CMK_FOLDER = 0, CMK_FILE, CMK_OLD_ROMS };

struct CmOrphan {
    char path[CM_PATH];
    uint8_t kind;             // CMK_*
    bool dir;
    bool deletable;           // listed for "delete files that belong to no core"
    uint64_t size;
    char why[40];
};

struct CmDup {
    char name[56];
    uint64_t size;
    int n;                    // places
    char where[3][CM_PATH];   // the first three
};

struct CmReport {
    int ncores, norph, ndups;
    CmCore core[CM_CORES];
    CmOrphan orph[CM_ORPHANS];
    CmDup dup[CM_DUPS];
    int ndelete;              // deletable entries
    uint64_t delete_bytes;
    bool truncated;           // a list was longer than the report holds
};

// Reads every medium's cores/ folder (not /Flash: Gideon's firmware keeps its ROMs there). Fills *rep.
void cm_scan(CmFs &list, FoFs &fs, CmReport *rep);

// The ROM files of one core against the table: need/have and the text of what is missing. entries = the files of <core>/roms/.
void cm_rom_status(const char *core_name, const CmEntry *entries, int n, int *need, int *have, char *missing, int missing_len);

// True when `path` may be removed by "delete files that belong to no core": never a .cor or .cfw, never anything inside the folder of a .cor
// that is there (checked against the card now, not against the report).
bool cm_may_delete(FoFs &fs, const char *path);

struct CmDelete {
    int deleted, failed, refused;
    uint64_t bytes;
};

// Removes the deletable entries of the report, files first, then the folders that are empty by then. Only entries the report lists, each one
// through fo_delete after cm_may_delete.
void cm_delete(FoFs &fs, const CmReport &rep, CmDelete *out);

#endif
