#include "hostfs.h"
#include "discover.h"
#include "filemanager.h"
#include "file_info.h"
#include "indexed_list.h"
#include "path.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

int hostfs_list(const char *path, HostDirEntry *out, int max, bool only_bundles) { return hostfs_list_n(path, out, max, only_bundles, NULL); }

int hostfs_list_n(const char *path, HostDirEntry *out, int max, bool only_bundles, int *total)
{
    Path p;
    p.cd(path);
    IndexedList<FileInfo *> infos(16, NULL);
    FileManager *fm = FileManager :: getFileManager();
    if (fm->get_directory(&p, infos, NULL) != FR_OK) {
        return -1;
    }
    int n = 0, all = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < infos.get_elements(); i++) {
            FileInfo *f = infos[i];
            bool dir = (f->attrib & AM_DIR) != 0;
            if ((pass == 0) != dir) {
                continue;
            }
            if (!dir && only_bundles && !discover_is_bundle(f->lfname)) {
                continue;
            }
            all++;
            if (n >= max) {
                continue;
            }
            strncpy(out[n].name, f->lfname, sizeof(out[n].name) - 1);
            out[n].name[sizeof(out[n].name) - 1] = 0;
            out[n].dir = dir;
            out[n].size = f->size32();   // saturated: a file of 4 GiB or more reads as 0xFFFFFFFF, never as a small wrapped size
            out[n].size64 = f->size;     // the real size, for the File Browser's size column
            n++;
        }
    }
    for (int i = 0; i < infos.get_elements(); i++) {
        delete infos[i];
    }
    if (total) {
        *total = all;
    }
    return n;
}

int hostfs_load(const char *path, char *buf, int max)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = NULL;
    if (fm->fopen(path, FA_READ, &f) != FR_OK || !f) {
        return -1;
    }
    uint32_t got = 0;
    FRESULT r = f->read(buf, max, &got);
    fm->fclose(f);
    return (r == FR_OK) ? (int)got : -1;
}

bool hostfs_save(const char *dir, const char *name, const void *buf, int len)
{
    uint32_t tr = 0;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT r = fm->save_file(true, dir, name, (const uint8_t *)buf, len, &tr);
    if (r != FR_OK || (int)tr != len) {
        return false;
    }
    // read back: a write the medium did not keep (a flash sector not erased before it was programmed) is a failed save, not a file that
    // differs silently from what the menu shows
    char path[200];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    File *f = NULL;
    if (fm->fopen(path, FA_READ, &f) != FR_OK || !f) {
        return false;
    }
    bool same = true;
    uint8_t chunk[256];
    const uint8_t *want = (const uint8_t *)buf;
    for (int off = 0; same && off < len; off += (int)sizeof(chunk)) {
        uint32_t n = len - off < (int)sizeof(chunk) ? (uint32_t)(len - off) : (uint32_t)sizeof(chunk), got = 0;
        same = f->read(chunk, n, &got) == FR_OK && got == n && !memcmp(chunk, want + off, n);
    }
    fm->fclose(f);
    return same;
}

bool hostfs_exists(const char *path)
{
    FileInfo info(INFO_SIZE);
    return FileManager :: getFileManager()->fstat(path, info) == FR_OK;
}

bool hostfs_stat(const char *path, uint32_t *size, uint32_t *mtime)
{
    FileInfo info(INFO_SIZE);
    if (FileManager :: getFileManager()->fstat(path, info) != FR_OK || (info.attrib & AM_DIR)) {
        return false;
    }
    *size = info.size32();
    uint32_t m = ((uint32_t)info.date << 16) | info.time;
    *mtime = m ? m : 1;
    return true;
}

bool hostfs_stat_dir(const char *path, uint32_t *mtime)
{
    FileInfo info(INFO_SIZE);
    if (FileManager :: getFileManager()->fstat(path, info) != FR_OK || !(info.attrib & AM_DIR)) {
        return false;
    }
    uint32_t m = ((uint32_t)info.date << 16) | info.time;
    *mtime = m ? m : 1;
    return true;
}

FileSource::FileSource(const char *path) : file(0), sz(0)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = NULL;
    if (fm->fopen(path, FA_READ, &f) == FR_OK && f) {
        file = f;
        sz = f->get_size32();   // saturated, see the listing above
    }
}

FileSource::~FileSource()
{
    if (file) {
        FileManager :: getFileManager()->fclose((File *)file);
    }
}

bool FileSource::read(uint32_t off, void *buf, uint32_t len)
{
    File *f = (File *)file;
    if (!f || off > sz || len > sz - off) {
        return false;
    }
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        uint32_t n = (len > 32768) ? 32768 : len, got = 0;
        if (f->seek(off) != FR_OK || f->read(p, n, &got) != FR_OK || got != n) {
            return false;
        }
        off += n; p += n; len -= n;
    }
    return true;
}

// ---- T139: FoFs over the FileManager --------------------------------------------------------------------------------------------
namespace {
struct FoHandle {
    File *f;
    bool w;
};

class HostFo : public FoFs {
    static FileManager *fm() { return FileManager :: getFileManager(); }
public:
    bool stat(const char *path, FoStat *st)
    {
        FileInfo info(INFO_SIZE);
        if (fm()->fstat(path, info) != FR_OK) {
            return false;
        }
        st->dir = (info.attrib & AM_DIR) != 0;
        st->size = info.size;
        return true;
    }
    int dir_entries(const char *path)
    {
        HostDirEntry e[2];
        return hostfs_list(path, e, 2, false);
    }
    bool free_bytes(const char *path, uint64_t *bytes)
    {
        Path p;
        p.cd(path);
        uint32_t clusters = 0, cs = 0;
        if (fm()->get_free(&p, clusters, cs) != FR_OK) {
            return false;
        }
        *bytes = (uint64_t)clusters * cs;
        return true;
    }
    bool same_volume(const char *a, const char *b)
    {
        const char *ea = strchr(a + 1, '/'), *eb = strchr(b + 1, '/');
        size_t la = ea ? (size_t)(ea - a) : strlen(a), lb = eb ? (size_t)(eb - b) : strlen(b);
        return la == lb && !strncasecmp(a, b, la);                   // the first path component is the medium
    }
    void *open_read(const char *path)
    {
        File *f = NULL;
        if (fm()->fopen(path, FA_READ, &f) != FR_OK || !f) {
            return 0;
        }
        FoHandle *h = new FoHandle;
        h->f = f;
        h->w = false;
        return h;
    }
    void *open_write(const char *path)
    {
        File *f = NULL;
        if (fm()->fopen(path, FA_WRITE | FA_CREATE_ALWAYS, &f) != FR_OK || !f) {
            return 0;
        }
        FoHandle *h = new FoHandle;
        h->f = f;
        h->w = true;
        return h;
    }
    int read(void *hh, void *buf, uint32_t n)
    {
        uint32_t got = 0;
        return ((FoHandle *)hh)->f->read(buf, n, &got) == FR_OK ? (int)got : -1;
    }
    bool write(void *hh, const void *buf, uint32_t n)
    {
        uint32_t tr = 0;
        return ((FoHandle *)hh)->f->write(buf, n, &tr) == FR_OK && tr == n;
    }
    bool close(void *hh)
    {
        FoHandle *h = (FoHandle *)hh;
        bool ok = !h->w || h->f->sync() == FR_OK;
        fm()->fclose(h->f);
        delete h;
        return ok;
    }
    bool remove(const char *path) { return fm()->delete_file(path) == FR_OK; }
    bool rename(const char *from, const char *to) { return fm()->rename(from, to) == FR_OK; }
    bool make_dir(const char *path)
    {
        FRESULT r = fm()->create_dir(path);
        return r == FR_OK || r == FR_EXIST;
    }
};
}

FoFs *hostfs_fo(void)
{
    static HostFo fo;
    return &fo;
}

namespace {
class HostCm : public CmFs {
public:
    int list(const char *dir, CmEntry *out, int max)
    {
        static HostDirEntry tmp[64];
        int n = hostfs_list(dir, tmp, max < 64 ? max : 64, false);
        for (int i = 0; i < n; i++) {
            snprintf(out[i].name, sizeof(out[i].name), "%s", tmp[i].name);
            out[i].dir = tmp[i].dir;
            out[i].size = tmp[i].size64;
        }
        return n;
    }
    bool dir_mtime(const char *dir, uint32_t *mtime) { return hostfs_stat_dir(dir, mtime); }
    bool core_name(const char *cor_path, char *name, int n)
    {
        FileSource src(cor_path);
        if (!src.ok()) {
            return false;
        }
        CorInfo ci;
        cor_result_t r = cor_probe(src, ci);               // a large core, another contract or another host still names itself
        if (r != COR_OK && r != COR_ERR_LARGE && r != COR_ERR_CONTRACT && r != COR_ERR_HOST) {
            return false;
        }
        snprintf(name, n, "%s", ci.name);
        return ci.name[0] != 0;
    }
};
}

CmFs *hostfs_cm(void)
{
    static HostCm cm;
    return &cm;
}
