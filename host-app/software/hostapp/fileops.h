// Delete, copy and move of single files for the File Browser (T139 D1). Pure code over FoFs (hostfs.cc implements it on the FileManager,
// tests/hostapp/test_fileops.cc on a RAM disk), so every rule is run without hardware:
//   delete   a file; a folder only when it is empty; the boot menu entries that point at the file are named first (D2)
//   copy     into a folder; the room is checked before the first byte is written (free and needed bytes in the answer); an existing file of
//            that name is a question (FO_EXISTS), not a replacement; the data goes to "<name>.part" and is read back and compared by SHA-256
//            before it gets its name; a replaced file is removed only after that
//   move     rename when the target is on the same volume and free; otherwise copy, read back, and only then delete the source
#ifndef HOST_FILEOPS_H
#define HOST_FILEOPS_H

#include <stdint.h>
#include "bootcfg.h"

struct FoStat {
    bool dir;
    uint64_t size;
};

struct FoFs {
    virtual ~FoFs() { }
    virtual bool stat(const char *path, FoStat *st) = 0;                 // false: no such file or folder
    virtual int  dir_entries(const char *path) = 0;                      // entries of a folder, -1 if it cannot be read
    virtual bool free_bytes(const char *path, uint64_t *bytes) = 0;      // free room on the volume that holds `path`
    virtual bool same_volume(const char *a, const char *b) = 0;
    virtual void *open_read(const char *path) = 0;                       // 0 on error
    virtual void *open_write(const char *path) = 0;                      // creates or empties the file
    virtual int  read(void *h, void *buf, uint32_t n) = 0;              // bytes read (0 = end), -1 on error
    virtual bool write(void *h, const void *buf, uint32_t n) = 0;
    virtual bool close(void *h) = 0;                                     // false if the data could not be finished
    virtual bool remove(const char *path) = 0;
    virtual bool rename(const char *from, const char *to) = 0;           // within one volume; false if it cannot
    virtual bool make_dir(const char *path) { (void)path; return false; }   // one folder; true when it is there afterwards (T137 D9: <medium>/cores)
};

enum FoResult {
    FO_OK = 0,
    FO_EXISTS,            // the target holds a file of that name: ask, then repeat with replace
    FO_CANCELLED,
    FO_ERR_NOSRC,         // the source is gone
    FO_ERR_NOTFILE,       // a folder where a file is needed (copy and move are for files)
    FO_ERR_NOTDIR,        // the target is not a folder
    FO_ERR_SAME,          // source and target are the same file
    FO_ERR_DIR_NOTEMPTY,
    FO_ERR_NOROOM,        // FoPlan::free and ::needed say how much
    FO_ERR_READ,
    FO_ERR_WRITE,
    FO_ERR_VERIFY,        // what was written does not read back as what was read
    FO_ERR_DELETE,        // a move copied the file and could not remove the original: both exist
    FO_ERR_FS             // the file system answered with an error
};

const char *fo_text(FoResult r);

#define FO_PATH_MAX 192

struct FoPlan {
    char src[FO_PATH_MAX];
    char dst[FO_PATH_MAX];
    uint64_t size;        // bytes to write
    uint64_t free;        // free on the target volume (0 when no write is needed)
    bool exists;          // a file of that name is in the target folder
    bool rename_only;     // a move that needs no bytes written
};

// Names the target and checks everything before a byte is written: the source is a file, the target a folder, not the same file, the room.
// FO_EXISTS (plan filled) when the target already holds a file of that name and there is room for the new copy; FO_OK when it does not.
// move: a rename is planned when the volume is the same and the name is free (no room is needed then).
FoResult fo_plan(FoFs &fs, const char *src, const char *dstdir, bool move, FoPlan *plan);

typedef int (*FoProgress)(void *ctx, int phase, uint64_t done, uint64_t total);   // non-zero cancels
enum { FO_PHASE_COPY = 0, FO_PHASE_VERIFY, FO_PHASE_DELETE };

// Runs a plan. replace: the existing target may be replaced (after the new data is complete and verified). buf: the block buffer.
FoResult fo_copy(FoFs &fs, const FoPlan &plan, bool replace, uint8_t *buf, uint32_t buflen, FoProgress cb, void *ctx);
FoResult fo_move(FoFs &fs, const FoPlan &plan, bool replace, uint8_t *buf, uint32_t buflen, FoProgress cb, void *ctx);

// A file, or a folder with nothing in it.
FoResult fo_delete(FoFs &fs, const char *path);

// The boot menu entries whose path is `path` (case-insensitive, like the card); returns how many there are and fills idx with the first max.
int fo_boot_refs(const BootCfg &cfg, const char *path, int *idx, int max);

#endif
