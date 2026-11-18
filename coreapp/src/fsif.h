// The files a core application touches, as an interface (T93 D5, D6, D10). The board implements it over the FileManager
// (fm_fs.cc); tests/coreapp/ implements it over a map of byte vectors, with switches to make a medium read only, full or gone.
#ifndef COREAPP_FSIF_H
#define COREAPP_FSIF_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// T93 S14, D11 of T84: on the MultiCore host a core application never writes the flash. The board's CoreFs refuses every write, delete
// and read-write open under /flash (any case, "/flash" itself or "/flash/..."), so a .cfg beside a .cor that lies in the flash stays in
// memory - except the files below. T228 R21: on the monolith board profile the flash drive is writable as in TRX64-Ultimate's firmware
// (its FLASH_ID_FLASHDRIVE window, vendor blockdev_flash.cc, is the only part of the chip the block device can reach); the startup sets
// fs_flash_writable() from the board profile and fs_write_refused then lets /flash through.
static inline bool fs_path_is_flash(const char *path)
{
    return !strncasecmp(path, "/flash", 6) && (path[6] == 0 || path[6] == '/');
}

// T104.2 Q1 (Alex, 2026-10-18: "1 /flash/trx64u/c64.cfg"): the exception. On the monolith there is no .cor and so no .cfg beside
// it; the C64's settings live in the internal flash drive, in this file and nowhere else. Gideon's own config sectors (0xFF0000) are not
// touched, so his firmware stays a clean way back. The comparison is the whole path, case-insensitive like FAT: a sibling, a
// subdirectory or any other file under /flash is still refused.
// T104.1 5.5 (Alex, 2026-10-18, the rescue from a crashing library): the boot-attempt counter is a key in one file per profile - on the
// monolith the second file of /flash/trx64u, on the MultiCore host the host's own boot menu file (host-app bootcfg.h BOOTCFG_PATH). Those
// two paths are the rest of the exception; every other path under /flash is refused.
#define FS_FLASH_CFG_PATH "/flash/trx64u/c64.cfg"
#define FS_FLASH_BOOT_MONOLITH "/flash/trx64u/boot.cfg"
#define FS_FLASH_BOOT_MULTICORE "/Flash/boot.cfg"
static inline bool fs_flash_exception(const char *path)
{
    return !strcasecmp(path, FS_FLASH_CFG_PATH) || !strcasecmp(path, FS_FLASH_BOOT_MONOLITH) || !strcasecmp(path, FS_FLASH_BOOT_MULTICORE);
}

// One flag for the whole app (a function-local static of an extern inline function is one object across translation units).
inline bool &fs_flash_writable()
{
    static bool v = false;
    return v;
}

// What the board's CoreFs asks before every write, delete and read-write open.
static inline bool fs_write_refused(const char *path)
{
    return fs_path_is_flash(path) && !fs_flash_writable() && !fs_flash_exception(path);
}

// T104.1 D16: sizes and offsets are 64 bit, an image on an exFAT stick can be 4 GiB and more (an Amiga HDF). Code that keeps a size in 32 bit
// takes it through this: 4 GiB and more reads as 0xFFFFFFFF, so a size check still refuses the file where a truncated value would let a
// small number through (a file of 4 GiB + 184320 bytes is not an XFD of 184320).
static inline uint32_t fs_size32(uint64_t size)
{
    return size > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)size;
}

// FsEntry::attr (T128 section 3 D4): what the folder disk needs of a host entry beyond its name.
#define FS_ATTR_HIDDEN 1
#define FS_ATTR_SYSTEM 2
#define FS_ATTR_RDONLY 4

// T180 #38: a FAT long name is up to 255 characters, and a path that ends in one is a folder path and that name. FsEntry::name holds the whole
// name; the app ABI (SvcFsEntry, svctab.h) keeps its own 55 characters and the bridge (LibLoader::svc_list) cuts. A buffer that takes a listed
// name to build a path from is FS_PATH_MAX; a buffer that only shows it may be short.
#define FS_NAME_MAX 255
#define FS_PATH_MAX 520
struct FsEntry {
    char name[FS_NAME_MAX + 1];
    uint16_t fdate, ftime;                // FAT packed modification date and time (0: unknown)
    uint8_t attr;                         // FS_ATTR_*
    bool dir;
    uint64_t size;
};

// A listing entry in the layout FsEntry had until T180 #38: 72 bytes, the name cut to 55 characters (`cut`: it was longer). For the one caller that
// keeps thousands of entries and has no use for long names (the folder disk: an Amiga name is 30 bytes, 16384 objects), CoreFs::list_short.
struct FsEntryShort {
    char name[56];
    uint16_t fdate, ftime;
    uint8_t attr;
    bool dir;
    bool cut;
    uint64_t size;
};

static inline void fs_entry_shorten(FsEntryShort *d, const struct FsEntry &s)
{
    size_t l = strlen(s.name);
    d->cut = l >= sizeof(d->name);
    memcpy(d->name, s.name, d->cut ? sizeof(d->name) - 1 : l);
    d->name[d->cut ? sizeof(d->name) - 1 : l] = 0;
    d->fdate = s.fdate;
    d->ftime = s.ftime;
    d->attr = s.attr;
    d->dir = s.dir;
    d->size = s.size;
}

// A listed name for a place that only shows it, in n bytes (NUL included): a name that fits is copied; a longer one keeps its beginning and its
// last characters (the extension) with ".." between, so the cut is seen and the file type still reads. Never for a name a path is built from.
static inline void fs_short_name(char *dst, size_t n, const char *name)
{
    if (n == 0) {
        return;
    }
    size_t l = strlen(name);
    if (l < n) {
        memcpy(dst, name, l + 1);
        return;
    }
    if (n < 12) {                                                   // no room for a head, "..", and a tail
        memcpy(dst, name, n - 1);
        dst[n - 1] = 0;
        return;
    }
    size_t tail = 6, head = n - 1 - 2 - tail;
    memcpy(dst, name, head);
    dst[head] = dst[head + 1] = '.';
    memcpy(dst + head + 2, name + l - tail, tail + 1);
}

// What a medium is, in the words of the host's File Browser (media level): "27.4 GiB", "exFAT  'TRX64U-MC'". `ready` false: no medium in (contents says so).
struct MediumInfo {
    bool ready;
    bool writable;
    char size[24];
    char contents[72];
    const char *access() const { return ready ? (writable ? "readable and writable" : "read only") : ""; }     // the host's word for it
};

class CoreFile {
public:
    virtual ~CoreFile() { }
    virtual uint64_t size() = 0;
    virtual bool writable() = 0;
    virtual bool read(uint64_t off, void *buf, uint32_t len) = 0;        // all or nothing
    virtual bool write(uint64_t off, const void *buf, uint32_t len) = 0; // all or nothing
    virtual bool sync() = 0;
};

class CoreFs {
public:
    virtual ~CoreFs() { }
    // Entries of a directory ("/" lists the mounted media); -1 when it cannot be read. At most max entries.
    virtual int list(const char *dir, FsEntry *out, int max) = 0;
    // The same listing in FsEntryShort: 72 bytes an entry instead of 272. The default lists through list() into a buffer of `max` whole entries and
    // shortens (-1 when there is no room for it); a medium that has the entries at hand (the board's FileManager) overrides it and needs no such buffer.
    virtual int list_short(const char *dir, FsEntryShort *out, int max)
    {
        if (max <= 0) {
            FsEntry none;
            return list(dir, &none, 0);
        }
        FsEntry *tmp = (FsEntry *)malloc((size_t)max * sizeof(FsEntry));
        if (!tmp) {
            return -1;
        }
        int n = list(dir, tmp, max);
        for (int i = 0; i < n; i++) {
            fs_entry_shorten(&out[i], tmp[i]);
        }
        free(tmp);
        return n;
    }
    virtual bool stat(const char *path, uint64_t *size, bool *dir) = 0;
    // The whole file into buf (at most max bytes); the length, -1 when it cannot be read or does not fit.
    virtual int load(const char *path, void *buf, uint32_t max) = 0;
    // Replaces the file; false on any error (read only medium, full, medium gone).
    virtual bool save(const char *path, const void *buf, uint32_t len) = 0;
    virtual bool remove(const char *path) = 0;
    // The open file or null. rw = true asks for write access and gives a read only file when the file or the medium is
    // read only (CoreFile::writable() says which).
    virtual CoreFile *open(const char *path, bool rw) = 0;
    virtual void close(CoreFile *f) = 0;
    // T128 D6, D10: what the folder disk needs and the interface lacked. A medium that cannot do it says false and the folder disk then
    // does not offer / does not write (the defaults), the board and the benches implement them.
    // The free space of the medium that holds `path`, in bytes (FatFs f_getfree).
    virtual bool free_bytes(const char *path, uint64_t *out) { (void)path; (void)out; return false; }
    // The media level of the Library (T104.2, as the host's File Browser): what the medium `name` (an entry of "/") is. False when the board cannot tell (a name that is no medium).
    virtual bool medium_info(const char *name, MediumInfo *out) { (void)name; (void)out; return false; }
    // T228 R21 (FW user_file_interaction.cc:45-55, 87-93; file_system.cc:5-17): may a file or folder at `path`, or a new one in it, be written, moved, renamed or deleted?
    // The menu offers Move, Cut, Rename, Delete, New folder and Paste only where this says yes. The default: yes (a medium that has no read-only places).
    virtual bool writable(const char *path) { (void)path; return true; }
    // A new empty directory; false when it exists already as a file or cannot be made. An existing directory is success.
    virtual bool mkdir(const char *path) { (void)path; return false; }
    // Moves a file or directory; the target must not exist (FatFs f_rename).
    virtual bool rename(const char *from, const char *to) { (void)from; (void)to; return false; }
};

#endif
