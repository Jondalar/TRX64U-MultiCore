// What the SIO drive emulation needs of a disk (T93 D6, T88 D8): sectors, 1-based. An ATR or XFD file is one kind
// (atari_atr.cc), a .xex presented as a boot disk is another (atari_xex.cc).
#ifndef COREAPP_ATARI_DISK_H
#define COREAPP_ATARI_DISK_H

#include <stdint.h>

class AtariDisk {
public:
    virtual ~AtariDisk() { }
    virtual uint32_t sectors() = 0;                         // sector numbers 1..sectors()
    // Bytes transferred for sector n: 128 for n = 1..3 whatever the density (the OS boots from 128-byte sectors), else the
    // image's sector size.
    virtual uint16_t sector_size(uint32_t n) = 0;
    virtual uint16_t image_sector_size() = 0;               // 128 or 256: what the drive reports as its density
    virtual bool read(uint32_t n, uint8_t *buf) = 0;        // sector_size(n) bytes
    virtual bool write(uint32_t n, const uint8_t *buf) = 0;
    virtual bool writable() = 0;
    virtual bool flush() { return true; }                   // data written reaches the medium (sync)
    virtual const char *name() = 0;
};

#endif
