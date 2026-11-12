#include "atari_atr.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

AtrGeometry atr_geometry(const uint8_t *h, uint32_t file_size, char *err, int errlen)
{
    AtrGeometry g;
    memset(&g, 0, sizeof(g));
    if (file_size >= 16 && h[0] == 0x96 && h[1] == 0x02) {
        uint32_t declared = ((uint32_t)h[2] | (uint32_t)h[3] << 8 | (uint32_t)h[6] << 16) * 16;
        uint16_t ss = (uint16_t)(h[4] | h[5] << 8);
        if (ss != 128 && ss != 256) {
            snprintf(err, errlen, "ATR sector size %u is not supported (128 or 256)", ss);
            return g;
        }
        uint32_t data = file_size - 16;
        if (declared && declared < data) {
            data = declared;                           // trailing bytes after the declared size are not disk
        }
        if (data < 128) {
            snprintf(err, errlen, "the ATR holds no complete sector");
            return g;
        }
        g.header = 16;
        if (ss == 256 && data >= 384) {
            g.sector_size = 256;
            g.packed_boot = true;
            g.sectors = 3 + (data - 384) / 256;
        } else {                                       // 128-byte sectors, or a 256-byte image too short to have a data sector
            g.sector_size = 128;
            g.sectors = data / 128;
        }
        g.ok = true;
        return g;
    }
    // XFD: no header, sectors back to back. 128-byte sectors for any whole number of them, 256-byte ones only for the one
    // size such images have (720 sectors); there the first three sectors are padded to 256 bytes.
    if (file_size == 184320) {
        g.ok = true;
        g.sector_size = 256;
        g.sectors = 720;
        return g;
    }
    if (file_size >= 128 && file_size % 128 == 0) {
        g.ok = true;
        g.sector_size = 128;
        g.sectors = file_size / 128;
        return g;
    }
    snprintf(err, errlen, "not an ATR (no 0x0296 header) and not a whole number of 128-byte sectors");
    return g;
}

uint32_t atr_sector_offset(const AtrGeometry &g, uint32_t n)
{
    if (!g.ok || n < 1 || n > g.sectors) {
        return 0;
    }
    if (g.sector_size == 128) {
        return g.header + (n - 1) * 128;
    }
    if (g.packed_boot) {
        return n <= 3 ? g.header + (n - 1) * 128 : g.header + 384 + (n - 4) * g.sector_size;
    }
    return g.header + (n - 1) * g.sector_size;         // XFD double density: every sector is 256 bytes in the file
}

namespace {

class AtrDisk : public AtariDisk {
    CoreFs *fs_;
    CoreFile *f_;
    AtrGeometry g_;
    char name_[56];
public:
    AtrDisk(CoreFs *fs, CoreFile *f, const AtrGeometry &g, const char *name) : fs_(fs), f_(f), g_(g)
    {
        fs_short_name(name_, sizeof(name_), name);
    }
    ~AtrDisk() { f_->sync(); fs_->close(f_); }
    uint32_t sectors() { return g_.sectors; }
    uint16_t sector_size(uint32_t n) { return n <= 3 ? 128 : g_.sector_size; }
    uint16_t image_sector_size() { return g_.sector_size; }
    bool read(uint32_t n, uint8_t *buf)
    {
        uint32_t off = atr_sector_offset(g_, n);
        return off && f_->read(off, buf, sector_size(n));
    }
    bool write(uint32_t n, const uint8_t *buf)
    {
        uint32_t off = atr_sector_offset(g_, n);
        return off && f_->writable() && f_->write(off, buf, sector_size(n));
    }
    bool writable() { return f_->writable(); }
    bool flush() { return f_->sync(); }
    const char *name() { return name_; }
};

}

AtariDisk *atr_open(CoreFs *fs, const char *path, char *err, int errlen)
{
    CoreFile *f = fs->open(path, true);
    if (!f) {
        snprintf(err, errlen, "cannot open %s", path);
        return 0;
    }
    uint8_t head[16];
    memset(head, 0, sizeof(head));
    if (f->size() >= 16 && !f->read(0, head, 16)) {
        fs->close(f);
        snprintf(err, errlen, "cannot read %s", path);
        return 0;
    }
    AtrGeometry g = atr_geometry(head, fs_size32(f->size()), err, errlen);
    if (!g.ok) {
        fs->close(f);
        return 0;
    }
    const char *slash = strrchr(path, '/');
    return new AtrDisk(fs, f, g, slash ? slash + 1 : path);
}
