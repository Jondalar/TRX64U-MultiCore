#include "fm_fs.h"
#include "board.h"
#include "filemanager.h"
#include "file_info.h"
#include "indexed_list.h"
#include "filesystem_fat.h"
#include "fastseek.h"
#include "file_device.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

// The media level of the Library lives in fm_medium.cc (host-app headers and FreeRTOS); the fs64 bench links a stub.
bool fm_medium_info(const char *name, MediumInfo *out);
bool fm_path_writable(const char *path);

namespace {

#if FF_USE_FASTSEEK
// A FatFs file of the Ultimate file system is a FileFAT; a file inside a mounted image, on FTP or the like is another class and has no FIL.
// There is no RTTI, so the class is told by the vtable pointer of a FileFAT that is only ever constructed here: a File of any other class
// cannot match, the worst case is a FileFAT that is not recognised (and stays on the slow path).
static bool is_fat_file(File *f)
{
    static FileFAT probe(NULL);
    return *(void *const *)f == *(void *const *)&probe;
}
#endif

class FmFile : public CoreFile {
    File *f;
    bool rw;
#if FF_USE_FASTSEEK
    FastSeek seekmap;           // the cluster link map of the open file (T104 5.16)
    bool fat;                   // f is a FileFAT
    void access(uint64_t off, uint32_t len, bool write)
    {
        if (fat) {
            seekmap.access(static_cast<FileFAT *>(f)->getFIL(), off, len, write, f->get_path());
        }
    }
#else
    void access(uint64_t, uint32_t, bool) { }
#endif
public:
    FmFile(File *file, bool write) : f(file), rw(write)
    {
#if FF_USE_FASTSEEK
        fat = is_fat_file(file);
#endif
    }
    uint64_t size() { return f->get_size(); }
    bool writable() { return rw; }
    bool read(uint64_t off, void *buf, uint32_t len)
    {
        uint8_t *p = (uint8_t *)buf;
        if (off > f->get_size() || len > f->get_size() - off) {
            return false;
        }
        access(off, len, false);
        while (len) {
            uint32_t n = len > 32768 ? 32768 : len, got = 0;
            if (f->seek(off) != FR_OK || f->read(p, n, &got) != FR_OK || got != n) {
                return false;
            }
            off += n; p += n; len -= n;
        }
        return true;
    }
    bool write(uint64_t off, const void *buf, uint32_t len)
    {
        if (!rw) {
            return false;
        }
        access(off, len, true);
        const uint8_t *p = (const uint8_t *)buf;
        while (len) {
            uint32_t n = len > 32768 ? 32768 : len, wr = 0;
            if (f->seek(off) != FR_OK || f->write(p, n, &wr) != FR_OK || wr != n) {
                return false;
            }
            off += n; p += n; len -= n;
        }
        return true;
    }
    bool sync() { return f->sync() == FR_OK; }
    File *file() { return f; }
};

class FmFs : public CoreFs {
public:
    int list(const char *dir, FsEntry *out, int max) { return list_t(dir, out, max); }
    int list_short(const char *dir, FsEntryShort *out, int max) { return list_t(dir, out, max); }       // straight from the FileManager's infos: no 272 byte entries held
    // The one listing for both entry layouts: folders first, the FAT long name cut to what the entry holds (FsEntry: all 255 characters).
    template <class E> int list_t(const char *dir, E *out, int max)
    {
        Path p;
        p.cd(dir);
        IndexedList<FileInfo *> infos(16, NULL);
        FileManager *fm = FileManager :: getFileManager();
        if (fm->get_directory(&p, infos, NULL) != FR_OK) {
            return -1;
        }
        int n = 0;
        for (int pass = 0; pass < 2; pass++) {              // folders first
            for (int i = 0; i < infos.get_elements(); i++) {
                FileInfo *f = infos[i];
                bool d = (f->attrib & AM_DIR) != 0;
                if ((pass == 0) != d || n >= max) {
                    continue;
                }
                set_entry(out[n], f, d);
                n++;
            }
        }
        for (int i = 0; i < infos.get_elements(); i++) {
            delete infos[i];
        }
        return n;
    }
    // The FileManager reads an entry into a FileInfo of lfsize (INFO_SIZE, 128) bytes with strncpy: a name that long is cut there and has no end. Never read past lfsize.
    static size_t info_len(const FileInfo *f)
    {
        size_t n = 0;
        while (n < f->lfsize && f->lfname[n]) {
            n++;
        }
        return n;
    }
    static void set_entry(FsEntry &e, FileInfo *f, bool d)
    {
        size_t l = info_len(f), cap = sizeof(e.name) - 1;
        l = l < cap ? l : cap;
        memcpy(e.name, f->lfname, l);
        e.name[l] = 0;
        fill(e, f, d);
    }
    static void set_entry(FsEntryShort &e, FileInfo *f, bool d)
    {
        size_t full = info_len(f), cap = sizeof(e.name) - 1, l = full < cap ? full : cap;
        memcpy(e.name, f->lfname, l);
        e.name[l] = 0;
        e.cut = full > cap;
        fill(e, f, d);
    }
    template <class E> static void fill(E &e, FileInfo *f, bool d)
    {
        e.dir = d;
        e.size = f->size;
        e.fdate = f->date;
        e.ftime = f->time;
        e.attr = (uint8_t)(((f->attrib & AM_HID) ? FS_ATTR_HIDDEN : 0) | ((f->attrib & AM_SYS) ? FS_ATTR_SYSTEM : 0) |
                           ((f->attrib & AM_RDO) ? FS_ATTR_RDONLY : 0));
    }
    bool stat(const char *path, uint64_t *size, bool *dir)
    {
        FileInfo info(INFO_SIZE);
        if (FileManager :: getFileManager()->fstat(path, info) != FR_OK) {
            return false;
        }
        if (size) *size = info.size;
        if (dir) *dir = (info.attrib & AM_DIR) != 0;
        return true;
    }
    int load(const char *path, void *buf, uint32_t max)
    {
        FileManager *fm = FileManager :: getFileManager();
        File *f = NULL;
        if (fm->fopen(path, FA_READ, &f) != FR_OK || !f) {
            return -1;
        }
        if (f->get_size() > max) {
            fm->fclose(f);
            return -1;
        }
        uint32_t got = 0;
        FRESULT r = f->read(buf, max, &got);
        fm->fclose(f);
        return r == FR_OK ? (int)got : -1;
    }
    bool save(const char *path, const void *buf, uint32_t len)
    {
        if (fs_write_refused(path)) {
            return false;
        }
        char dir[FS_PATH_MAX];
        const char *slash = strrchr(path, '/');
        if (!slash || slash == path || (size_t)(slash - path) >= sizeof(dir)) {
            return false;
        }
        memcpy(dir, path, slash - path);
        dir[slash - path] = 0;
        if (fs_path_is_flash(path) && fs_flash_exception(path) && strcasecmp(path, FS_FLASH_BOOT_MULTICORE)) {   // the standalone core's two files: the directory may not exist yet (FR_EXIST is fine)
            FileManager :: getFileManager()->create_dir(dir);
        }
        uint32_t tr = 0;
        FRESULT r = FileManager :: getFileManager()->save_file(true, dir, slash + 1, (const uint8_t *)buf, len, &tr);
        return r == FR_OK && tr == len;
    }
    bool remove(const char *path)
    {
        if (fs_write_refused(path)) {
            return false;
        }
        return FileManager :: getFileManager()->delete_file(path) == FR_OK;
    }
    bool free_bytes(const char *path, uint64_t *out)
    {
        Path p;
        p.cd(path);
        uint32_t clusters = 0, csize = 0;
        if (FileManager :: getFileManager()->get_free(&p, clusters, csize) != FR_OK) {
            return false;
        }
        *out = (uint64_t)clusters * csize;
        return true;
    }
    bool mkdir(const char *path)
    {
        if (fs_write_refused(path)) {
            return false;
        }
        uint64_t size;
        bool dir;
        if (stat(path, &size, &dir)) {
            return dir;                                      // there already: a directory is success, a file is not
        }
        return FileManager :: getFileManager()->create_dir(path) == FR_OK;
    }
    bool rename(const char *from, const char *to)
    {
        if (fs_write_refused(from) || fs_write_refused(to)) {
            return false;
        }
        return FileManager :: getFileManager()->rename(from, to) == FR_OK;
    }
    CoreFile *open(const char *path, bool rw)
    {
        FileManager *fm = FileManager :: getFileManager();
        File *f = NULL;
        // One look-up of the path (T128 S9: in a folder of 200 files a look-up reads the whole directory, 38 sectors, and the folder disk opens a
        // file for most requests): a file that is read only on the medium simply fails to open for writing and is then opened for reading.
        bool write = false;
        if (rw && !fs_write_refused(path) && fm->fopen(path, FA_READ | FA_WRITE, &f) == FR_OK && f) {
            write = true;
        } else {
            f = NULL;
            if (fm->fopen(path, FA_READ, &f) != FR_OK || !f) {
                return 0;
            }
        }
        FmFile *w = new FmFile(f, write);
        return w;
    }
    bool medium_info(const char *name, MediumInfo *out) { return fm_medium_info(name, out); }
    bool writable(const char *path) { return fm_path_writable(path); }
    void close(CoreFile *cf)
    {
        FmFile *w = (FmFile *)cf;
        FileManager :: getFileManager()->fclose(w->file());      // closes the FIL; the link map is freed with the wrapper
        delete w;
    }
};

}

CoreFs *fm_fs(void)
{
    static FmFs fs;
    return &fs;
}
