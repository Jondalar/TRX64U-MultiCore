// Files for the host boot app on top of the FileManager (SD, USB, Flash): directory listing,
// whole-file load/save and a CorSource over a file.
#ifndef HOST_FS_H
#define HOST_FS_H

#include <stdint.h>
#include "cor.h"
#include "fileops.h"
#include "coremaint.h"

struct HostDirEntry {
    char name[56];
    bool dir;
    uint32_t size;           // saturated: a file of 4 GiB or more reads as 0xFFFFFFFF
    uint64_t size64;         // the real size (an exFAT file of 9 GB is 9 GB); only the File Browser's size column reads it
};

// Lists `path` ("/" = the mounted media). Folders first, then the files; with only_bundles only *.cor and *.cfw files (discover_is_bundle).
// Returns the number of entries (at most max), -1 if the path cannot be read.
int hostfs_list(const char *path, HostDirEntry *out, int max, bool only_bundles);

// Whole file into buf (max bytes); returns the length, -1 if it cannot be read.
int hostfs_load(const char *path, char *buf, int max);

// Replaces dir/name with buf; false on any error.
bool hostfs_save(const char *dir, const char *name, const void *buf, int len);

bool hostfs_exists(const char *path);

// The directory entry of one file: size (saturated) and mtime = FAT date << 16 | time, never 0. One stat, no file opened, no listing.
bool hostfs_stat(const char *path, uint32_t *size, uint32_t *mtime);

// T139: the file layer fileops.cc works on, over the FileManager (delete, copy and move in the File Browser).
FoFs *hostfs_fo(void);

// T148: the listing and the manifest names the "Cores and ROMs" page (coremaint.cc) reads, over the FileManager.
CmFs *hostfs_cm(void);

struct File;
class FileSource : public CorSource {
    void *file;
    uint32_t sz;
public:
    explicit FileSource(const char *path);
    ~FileSource();
    bool ok() { return file != 0; }
    uint32_t size() { return sz; }
    bool read(uint32_t off, void *buf, uint32_t len);
};

#endif
