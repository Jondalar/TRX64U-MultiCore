// T93 D6, T95 5.8: the iNES header against the mapper flags word, with words computed by hand from nes_flags.v's expressions.
#include "check.h"
#include "ines.h"

static void hdr(uint8_t h[16], int prg, int chr, int f6, int f7)
{
    memset(h, 0, 16);
    memcpy(h, "NES\x1A", 4);
    h[4] = prg; h[5] = chr; h[6] = f6; h[7] = f7;
}

int main()
{
    uint8_t h[16];
    // (1) NROM-256, vertical mirroring: prg 2 pages -> size code 1, chr 1 page -> 0, no CHR RAM. mapper 0.
    //     bit 8 (prg code 1) + bit 14 (vertical) = 0x100 + 0x4000
    hdr(h, 2, 1, 0x01, 0);
    CHECK(ines_mapper_flags(h) == 0x4100ULL);
    InesInfo i = ines_parse(h, 16 + 32768 + 8192);
    CHECK(i.ok && i.mapper == 0 && i.prg_bytes == 32768 && i.chr_bytes == 8192 && !i.chr_ram && i.vertical && !i.trainer);
    CHECK_EQ(i.prg_off, 16);
    CHECK_EQ(i.chr_off, 16 + 32768);
    CHECK(i.flags == 0x4100ULL);

    // (2) UNROM (mapper 2), 8 pages = 128 KB (code 3), no CHR ROM -> CHR RAM (bit 15): 2 + 0x300 + 0x8000
    hdr(h, 8, 0, 0x20, 0);
    CHECK(ines_mapper_flags(h) == 0x8302ULL);
    i = ines_parse(h, 16 + 131072);
    CHECK(i.ok && i.mapper == 2 && i.chr_ram && i.chr_bytes == 0);

    // (3) MMC3 (4) with battery: prg 16 pages (code 4), chr 16 pages (code 4), has_saves bit 25:
    //     4 + (4 << 8) + (4 << 11) + (1 << 25) = 4 + 0x400 + 0x2000 + 0x2000000
    hdr(h, 16, 16, 0x42, 0);
    CHECK(ines_mapper_flags(h) == 0x2002404ULL);
    i = ines_parse(h, 16 + 262144 + 131072);
    CHECK(i.ok && i.mapper == 4 && i.battery);

    // the same header with the mapper nibble wrong gives a different word (the control of this file: the parser must not lose the nibble)
    hdr(h, 8, 0, 0x30, 0);
    CHECK(ines_mapper_flags(h) == 0x8303ULL);
    CHECK(ines_mapper_flags(h) != 0x8302ULL);

    // (4) NES 2.0: mapper 1, submapper 1 in byte 8, CHR RAM 8 KB (byte 11 low nibble 7 -> bit 15), prg 2 pages
    //     1 + 0x100 + 0x8000 + (0x10 << 17) + (1 << 35)
    hdr(h, 2, 1, 0x10, 0x08);
    h[8] = 0x10; h[11] = 0x07;
    CHECK(ines_mapper_flags(h) == 0x800208101ULL);
    i = ines_parse(h, 16 + 32768 + 8192);
    CHECK(i.ok && i.nes20 && i.mapper == 1 && i.submapper == 1 && i.chr_ram);
    // NES 2.0 mapper number above 255: byte 8 low nibble is bits 11:8
    h[8] = 0x01;                                       // mapper 256 + 1 = 257: not built
    i = ines_parse(h, 16 + 32768 + 8192);
    CHECK(!i.ok && i.mapper == 257 && strstr(i.err, "257") != 0);

    // (5) a "dirty" iNES 1.0 header (DiskDude! in bytes 7-15): byte 7's upper nibble is junk and must not become the mapper
    hdr(h, 2, 1, 0x10, 0x44);
    memcpy(h + 7, "DiskDude!", 9);                      // byte 7 = 'D' (0x44)
    CHECK(ines_mapper_flags(h) == (1ULL | (1ULL << 8) | 0ULL));
    i = ines_parse(h, 16 + 32768 + 8192);
    CHECK(i.ok && i.mapper == 1);
    hdr(h, 2, 1, 0x10, 0x40);                          // the same byte 7 on a clean header IS the mapper's upper nibble: 0x41 = 65
    CHECK((ines_mapper_flags(h) & 0xFF) == 0x41);
    i = ines_parse(h, 16 + 32768 + 8192);
    CHECK(!i.ok && i.mapper == 0x41);

    // a trainer moves the image by 512 bytes
    hdr(h, 1, 1, 0x04, 0);
    i = ines_parse(h, 16 + 512 + 16384 + 8192);
    CHECK(i.ok && i.trainer && i.prg_off == 528 && i.chr_off == 528 + 16384);
    // too short, too big, wrong magic, no PRG
    CHECK(!ines_parse(h, 16 + 512 + 16384 + 8191).ok);
    hdr(h, 255, 0, 0, 0);
    i = ines_parse(h, 16 + 255 * 16384);
    CHECK(!i.ok && strstr(i.err, "2 MB") != 0);
    hdr(h, 128, 0, 0, 0);                              // exactly 2 MB
    CHECK(ines_parse(h, 16 + 128 * 16384).ok);
    hdr(h, 0, 0, 0, 0);
    CHECK(!ines_parse(h, 16).ok);
    hdr(h, 2, 1, 0, 0);
    h[3] = 0x1B;
    CHECK(!ines_parse(h, 16 + 32768 + 8192).ok);
    CHECK(!ines_parse(h, 10).ok);
    // the supported list: spot checks against T95 5.10
    CHECK(ines_mapper_supported(0) && ines_mapper_supported(4) && ines_mapper_supported(268) && ines_mapper_supported(232));
    CHECK(!ines_mapper_supported(5) && !ines_mapper_supported(69) && !ines_mapper_supported(24));
    DONE("test_ines");
}
