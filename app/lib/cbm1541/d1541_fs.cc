#include "d1541_fs.h"
#include "d1541_gcr.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// ---- the layout ------------------------------------------------------------------------------------------------------------------
bool DiskReader::read_sector(int track, int sector, uint8_t *buf)
{
    uint64_t off;
    if (d81_) {
        if (track < 1 || track > 80 || sector < 0 || sector >= 40) {
            return false;
        }
        off = ((uint64_t)(track - 1) * 40 + (uint64_t)sector) * 256;
    } else {
        if (track < 1 || track > D64_MAX_TRACKS || sector < 0 || sector >= d64_sectors(track - 1)) {
            return false;
        }
        off = (uint64_t)(d64_sectors_before(track - 1) + sector) * 256;
    }
    return f_->read(off, buf, 256);
}

static char ascii_of(uint8_t c)
{
    if (c >= 0x41 && c <= 0x5A) return (char)c;                        // letters as the machine types them
    if (c >= 0xC1 && c <= 0xDA) return (char)(c - 0x80);               // shifted letters
    if (c >= 0x20 && c <= 0x5F && c != '/' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '\\' && c != '|') return (char)c;
    return '_';
}

void disk_display_name(const uint8_t *pet, int len, int type, char *out, int outlen)
{
    static const char *const ext[5] = { "del", "seq", "prg", "usr", "rel" };
    int n = 0;
    for (int i = 0; i < len && n < outlen - 5; i++) {
        out[n++] = ascii_of(pet[i]);
    }
    if (n == 0 && outlen > 5) {
        out[n++] = '_';
    }
    snprintf(out + n, (size_t)(outlen - n), ".%s", ext[type > 4 ? 0 : type]);
}

int DiskReader::list(DiskEntry *out, int max)
{
    uint8_t sec[256];
    int t, s;
    if (d81_) {
        if (!read_sector(40, 0, sec)) {
            return -1;
        }
        t = sec[0];
        s = sec[1];
    } else {
        t = 18;
        s = 1;
    }
    int n = 0;
    for (int guard = 0; guard < 64 && t; guard++) {
        if (!read_sector(t, s, sec)) {
            return n > 0 ? n : -1;
        }
        for (int e = 0; e < 8; e++) {
            const uint8_t *p = sec + e * 32;
            uint8_t ty = p[2];
            if (!(ty & 0x80) || !(ty & 7) || n >= max) {
                continue;                                              // scratched, not closed, or no room
            }
            DiskEntry &d = out[n++];
            memset(&d, 0, sizeof(d));
            d.type = ty & 7;
            d.track = p[3];
            d.sector = p[4];
            d.blocks = (uint16_t)(p[30] | (p[31] << 8));
            int len = 16;
            while (len > 0 && (p[5 + len - 1] == 0xA0)) {
                len--;
            }
            d.pet_len = (uint8_t)len;
            memcpy(d.pet, p + 5, (size_t)len);
            disk_display_name(d.pet, len, d.type, d.name, sizeof(d.name));
        }
        t = sec[0];
        s = sec[1];
    }
    return n;
}

int DiskReader::file_size(int track, int sector)
{
    uint8_t sec[256];
    int total = 0;
    for (int guard = 0; guard < 3200; guard++) {
        if (!read_sector(track, sector, sec)) {
            return -1;
        }
        if (sec[0] == 0) {
            if (sec[1] < 1) {
                return total;
            }
            return total + (sec[1] - 1);
        }
        total += 254;
        track = sec[0];
        sector = sec[1];
    }
    return -1;                                                         // a loop
}

int DiskReader::read_file(int track, int sector, uint8_t *buf, uint32_t cap)
{
    uint8_t sec[256];
    uint32_t total = 0;
    for (int guard = 0; guard < 3200; guard++) {
        if (!read_sector(track, sector, sec)) {
            return -1;
        }
        uint32_t n = sec[0] == 0 ? (sec[1] < 1 ? 0 : (uint32_t)sec[1] - 1) : 254;
        if (total + n > cap) {
            return -1;
        }
        memcpy(buf + total, sec + 2, n);
        total += n;
        if (sec[0] == 0) {
            return (int)total;
        }
        track = sec[0];
        sector = sec[1];
    }
    return -1;
}

// ---- paths -----------------------------------------------------------------------------------------------------------------------
bool disk_split(const char *path, char *image, int imagelen, const char **entry)
{
    for (const char *p = path; *p; p++) {
        if (*p != '.') {
            continue;
        }
        if ((strncasecmp(p, ".d64", 4) && strncasecmp(p, ".d81", 4)) || (p[4] != '/' && p[4] != 0)) {
            continue;
        }
        int l = (int)(p + 4 - path);
        if (l >= imagelen) {
            return false;
        }
        memcpy(image, path, (size_t)l);
        image[l] = 0;
        *entry = p[4] == '/' ? p + 5 : p + 4;
        return true;
    }
    return false;
}

static bool is_d81(const char *image)
{
    size_t l = strlen(image);
    return l >= 4 && !strcasecmp(image + l - 4, ".d81");
}

// ---- the folder ------------------------------------------------------------------------------------------------------------------
class MemFile : public CoreFile {
public:
    MemFile(uint8_t *data, uint32_t n) : data_(data), n_(n) { }
    ~MemFile() { delete[] data_; }
    uint64_t size() { return n_; }
    bool writable() { return false; }
    bool read(uint64_t off, void *buf, uint32_t len)
    {
        if (off > n_ || len > n_ - off) {
            return false;
        }
        memcpy(buf, data_ + off, len);
        return true;
    }
    bool write(uint64_t, const void *, uint32_t) { return false; }
    bool sync() { return true; }
private:
    uint8_t *data_;
    uint32_t n_;
};

bool DiskFs::container(const char *path, char *image, int imagelen, const char **entry)
{
    if (!disk_split(path, image, imagelen, entry)) {
        return false;
    }
    uint64_t sz;
    bool dir = true;
    return inner_->stat(image, &sz, &dir) && !dir;
}

bool disk_lookup(CoreFs *fs, const char *path, DiskEntry *e, char *image, int imagelen)
{
    const char *entry;
    if (!disk_split(path, image, imagelen, &entry) || !*entry) {
        return false;
    }
    CoreFile *f = fs->open(image, false);
    if (!f) {
        return false;
    }
    DiskReader r(f, is_d81(image));
    static DiskEntry all[160];
    int n = r.list(all, 160);
    bool found = false;
    for (int i = 0; i < n && !found; i++) {
        if (!strcasecmp(all[i].name, entry)) {
            *e = all[i];
            found = true;
        }
    }
    fs->close(f);
    return found;
}

bool DiskFs::find(const char *path, DiskEntry *e, char *image, int imagelen)
{
    const char *entry;
    if (!container(path, image, imagelen, &entry) || !*entry) {
        return false;
    }
    return disk_lookup(inner_, path, e, image, imagelen);
}

int DiskFs::list(const char *dir, FsEntry *out, int max)
{
    char image[FS_PATH_MAX];
    const char *entry;
    if (!container(dir, image, sizeof(image), &entry)) {
        return inner_->list(dir, out, max);
    }
    if (*entry && strcmp(entry, "/")) {
        return -1;                                                     // an image has no folders
    }
    CoreFile *f = inner_->open(image, false);
    if (!f) {
        return -1;
    }
    DiskReader r(f, is_d81(image));
    static DiskEntry all[160];
    int n = r.list(all, 160);
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        memset(&out[k], 0, sizeof(FsEntry));
        snprintf(out[k].name, sizeof(out[k].name), "%s", all[i].name);
        out[k].size = (uint64_t)all[i].blocks * 254;
        k++;
    }
    inner_->close(f);
    return n < 0 ? -1 : k;
}

bool DiskFs::stat(const char *path, uint64_t *size, bool *dir)
{
    char image[FS_PATH_MAX];
    DiskEntry e;
    if (find(path, &e, image, sizeof(image))) {
        CoreFile *f = inner_->open(image, false);
        if (!f) {
            return false;
        }
        DiskReader r(f, is_d81(image));
        int n = r.file_size(e.track, e.sector);
        inner_->close(f);
        if (n < 0) {
            return false;
        }
        if (size) *size = (uint64_t)n;
        if (dir) *dir = false;
        return true;
    }
    return inner_->stat(path, size, dir);
}

CoreFile *DiskFs::open(const char *path, bool rw)
{
    char image[FS_PATH_MAX];
    DiskEntry e;
    const char *entry;
    if (!container(path, image, sizeof(image), &entry) || !*entry) {
        return inner_->open(path, rw);
    }
    if (!find(path, &e, image, sizeof(image))) {
        return 0;
    }
    int slot = -1;
    for (int i = 0; i < LIVE && slot < 0; i++) {
        if (!live_[i]) slot = i;
    }
    CoreFile *f = slot < 0 ? 0 : inner_->open(image, false);
    if (!f) {
        return 0;
    }
    DiskReader r(f, is_d81(image));
    int n = r.file_size(e.track, e.sector);
    uint8_t *data = n >= 0 ? new uint8_t[n ? n : 1] : 0;
    if (data && r.read_file(e.track, e.sector, data, (uint32_t)n) != n) {
        delete[] data;
        data = 0;
    }
    inner_->close(f);
    if (!data) {
        return 0;
    }
    live_[slot] = new MemFile(data, (uint32_t)n);
    return live_[slot];
}

void DiskFs::close(CoreFile *f)
{
    for (int i = 0; i < LIVE; i++) {
        if (live_[i] && live_[i] == f) {
            delete f;
            live_[i] = 0;
            return;
        }
    }
    inner_->close(f);
}

int DiskFs::load(const char *path, void *buf, uint32_t max)
{
    char image[FS_PATH_MAX];
    const char *entry;
    if (!container(path, image, sizeof(image), &entry) || !*entry) {
        return inner_->load(path, buf, max);
    }
    CoreFile *f = open(path, false);
    if (!f) {
        return -1;
    }
    uint64_t n = f->size();
    int rc = -1;
    if (n <= max && f->read(0, buf, (uint32_t)n)) {
        rc = (int)n;
    }
    close(f);
    return rc;
}

bool DiskFs::save(const char *path, const void *buf, uint32_t len)
{
    char image[FS_PATH_MAX];
    const char *entry;
    if (container(path, image, sizeof(image), &entry) && *entry) {
        return false;                                                  // an image is read only here
    }
    return inner_->save(path, buf, len);
}

bool DiskFs::mkdir(const char *path)
{
    char image[FS_PATH_MAX];
    const char *entry;
    if (container(path, image, sizeof(image), &entry) && *entry) {
        return false;
    }
    return inner_->mkdir(path);
}

bool DiskFs::rename(const char *from, const char *to)
{
    char image[FS_PATH_MAX];
    const char *entry;
    if ((container(from, image, sizeof(image), &entry) && *entry) || (container(to, image, sizeof(image), &entry) && *entry)) {
        return false;
    }
    return inner_->rename(from, to);
}

bool DiskFs::free_bytes(const char *path, uint64_t *out)
{
    return inner_->free_bytes(path, out);
}

bool DiskFs::medium_info(const char *name, MediumInfo *out)
{
    return inner_->medium_info(name, out);
}

bool DiskFs::remove(const char *path)
{
    char image[FS_PATH_MAX];
    const char *entry;
    if (container(path, image, sizeof(image), &entry) && *entry) {
        return false;
    }
    return inner_->remove(path);
}
