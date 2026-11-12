#include "atari_xex.h"
#include "6502/xexboot_bin.h"
#include <stdio.h>
#include <string.h>

#define FILELEN_OFS 9      // xexboot.asm: jmp at 6, filelen at 9 (checked by tests/coreapp/test_atari_xex.cc)

void xex_boot_sectors(uint32_t n, uint8_t out[384])
{
    memset(out, 0, 384);
    memcpy(out, xexboot_bin, sizeof(xexboot_bin) < 384 ? sizeof(xexboot_bin) : 384);
    out[FILELEN_OFS] = (uint8_t)n;
    out[FILELEN_OFS + 1] = (uint8_t)(n >> 8);
    out[FILELEN_OFS + 2] = (uint8_t)(n >> 16);
}

namespace {

class XexDisk : public AtariDisk {
    CoreFs *fs_;
    CoreFile *f_;
    uint32_t len_;
    uint8_t boot_[384];
    char name_[56];
public:
    XexDisk(CoreFs *fs, CoreFile *f, const char *name) : fs_(fs), f_(f), len_(fs_size32(f->size()))
    {
        xex_boot_sectors(len_, boot_);
        fs_short_name(name_, sizeof(name_), name);
    }
    ~XexDisk() { fs_->close(f_); }
    uint32_t sectors() { return 3 + (len_ + 127) / 128; }
    uint16_t sector_size(uint32_t) { return 128; }
    uint16_t image_sector_size() { return 128; }
    bool read(uint32_t n, uint8_t *buf)
    {
        if (n < 1 || n > sectors()) {
            return false;
        }
        if (n <= 3) {
            memcpy(buf, boot_ + (n - 1) * 128, 128);
            return true;
        }
        uint32_t off = (n - 4) * 128, take = len_ - off < 128 ? len_ - off : 128;
        memset(buf, 0, 128);
        return f_->read(off, buf, take);
    }
    bool write(uint32_t, const uint8_t *) { return false; }
    bool writable() { return false; }
    const char *name() { return name_; }
};

}

AtariDisk *xex_open(CoreFs *fs, const char *path, char *err, int errlen)
{
    CoreFile *f = fs->open(path, false);
    if (!f) {
        snprintf(err, errlen, "cannot open %s", path);
        return 0;
    }
    uint8_t h[2] = { 0, 0 };
    if (f->size() < 6 || f->size() > XEX_MAX_BYTES || !f->read(0, h, 2) || h[0] != 0xFF || h[1] != 0xFF) {
        fs->close(f);
        snprintf(err, errlen, "%s is not an Atari binary file (it must start with $FFFF and be 6 bytes to 16 MB)", path);
        return 0;
    }
    const char *slash = strrchr(path, '/');
    return new XexDisk(fs, f, slash ? slash + 1 : path);
}
