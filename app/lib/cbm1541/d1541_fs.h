// The file system of a .d64 / .d81 (T104.1 D15 scope, Alex 2026-10-22: Mount & Run of a PRG inside a disk image), read only: the directory and the
// chain of a file's sectors. Gideon's filesystem_d64.cc (2,539 lines) is a full read/write CBM file system with GEOS, relative files and the
// 1571's second side; what the browser needs is what is here - list the closed files of the directory and read one - from the layout of the
// 1541 (directory at 18/1 behind the BAM at 18/0) and the 1581 (header at 40/0, directory chain from the header's link, normally 40/3).
//
// DiskFs makes the image a folder for everything that goes through CoreFs: "/usb0/games/x.d64/HELLO.prg" lists, stats, loads and opens
// like a file of a stick, so the browser, "Run PRG" and the drive's Mount & Run need nothing of their own. Pure: CoreFs and CoreFile only.
#ifndef CBM1541_FS_H
#define CBM1541_FS_H

#include <stdint.h>
#include "fsif.h"

struct DiskEntry {
    char name[24];                // as the browser shows it: "HELLO.prg"; the PETSCII name mapped to ASCII, '/' and others as '_'
    uint8_t pet[16];              // the name as the disk holds it, without the $A0 padding
    uint8_t pet_len;
    uint8_t type;                 // 0 DEL, 1 SEQ, 2 PRG, 3 USR, 4 REL
    uint8_t track, sector;        // the first sector
    uint16_t blocks;
};

class DiskReader {
public:
    DiskReader(CoreFile *f, bool d81) : f_(f), d81_(d81) { }
    // The closed, not scratched files of the directory in disk order. Returns the count, -1 when the directory cannot be read.
    int list(DiskEntry *out, int max);
    // The bytes of the file whose chain starts at track/sector: the length, -1 when the chain is broken. size_only: only counts.
    int file_size(int track, int sector);
    // Reads the file into buf (cap bytes). The length, -1 when the chain is broken or does not fit.
    int read_file(int track, int sector, uint8_t *buf, uint32_t cap);
    bool read_sector(int track, int sector, uint8_t *buf);
private:
    CoreFile *f_;
    bool d81_;
};

// "x.d64" / "x.d81" (any case) at the end of a path component that is followed by '/' or the end: the image's path and what follows.
// false when the path names no image. entry points into path ("" for the image itself).
bool disk_split(const char *path, char *image, int imagelen, const char **entry);

// The browser's name of a PETSCII file name and type ("HELLO.prg").
void disk_display_name(const uint8_t *pet, int len, int type, char *out, int outlen);

// The directory entry of the file a path names inside an image, read through `fs`. false when the path is no file of an image.
bool disk_lookup(CoreFs *fs, const char *path, DiskEntry *e, char *image, int imagelen);

class DiskFs : public CoreFs {
public:
    explicit DiskFs(CoreFs *inner) : inner_(inner) { for (int i = 0; i < LIVE; i++) live_[i] = 0; }
    int list(const char *dir, FsEntry *out, int max);
    bool stat(const char *path, uint64_t *size, bool *dir);
    int load(const char *path, void *buf, uint32_t max);
    bool save(const char *path, const void *buf, uint32_t len);
    bool remove(const char *path);
    CoreFile *open(const char *path, bool rw);
    void close(CoreFile *f);
    // T148: the core folder is made through this wrapper; a path inside an image is no place for either.
    bool mkdir(const char *path);
    bool rename(const char *from, const char *to);
    bool free_bytes(const char *path, uint64_t *out);
    bool medium_info(const char *name, MediumInfo *out);

    // Finds the entry of a path inside an image: fills e. false when the path is not inside an image or has no such file.
    bool find(const char *path, DiskEntry *e, char *image, int imagelen);

private:
    enum { LIVE = 4 };
    CoreFs *inner_;
    CoreFile *live_[LIVE];
    bool container(const char *path, char *image, int imagelen, const char **entry);
};

#endif
