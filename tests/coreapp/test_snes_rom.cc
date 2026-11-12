// T142 S9/S12: the header reader of the SNES application (coreapp/cores/snes_rom.cc) against synthetic headers made here - no cartridge image, no third party
// code. Every check names the SNES.sv / DSP_LHRomMap.vhd decision it stands for: LoROM, HiROM and ExHiROM are found in their places with and without the
// 512 byte copier header, the mapper byte and the two masks are the ones SNES.sv computes from the header's size bytes, the region follows the country
// code, and an image whose header names a coprocessor the core does not have is refused with a sentence that names it.
#include <vector>
#include <string>
#include "check.h"
#include "snes_rom.h"

// An image of `bytes` bytes (a multiple of 32 KiB) with a header at `place`: the title, the map mode, the chipset, the size bytes, the country and the
// checksum pair, the reset vector. `copier`: 512 zero bytes in front.
struct Img {
    std::vector<uint8_t> v;
    uint32_t copier;
    Img(uint32_t bytes, uint32_t place, uint8_t mode, uint8_t chipset, uint8_t romsz, uint8_t ramsz, uint8_t country, bool copier_hdr = false, const char *title = "SYNTHETIC TEST CART")
        : v(bytes + (copier_hdr ? 512 : 0), 0x00), copier(copier_hdr ? 512 : 0)
    {
        uint8_t *h = &v[copier + place];
        memset(h, ' ', 21);
        memcpy(h, title, strlen(title) < 21 ? strlen(title) : 21);
        h[0x15] = mode; h[0x16] = chipset; h[0x17] = romsz; h[0x18] = ramsz; h[0x19] = country;
        uint16_t sum = 0x1234, comp = (uint16_t)~sum;
        h[0x1C] = (uint8_t)comp; h[0x1D] = (uint8_t)(comp >> 8); h[0x1E] = (uint8_t)sum; h[0x1F] = (uint8_t)(sum >> 8);
        h[0x3C] = 0x00; h[0x3D] = 0x80;                          // reset vector $8000
    }
    SnesInfo parse() const
    {
        static const uint32_t places[3] = { SNES_HDR_LO, SNES_HDR_HI, SNES_HDR_EX };
        const uint8_t *hp[3] = { 0, 0, 0 };
        for (int i = 0; i < 3; i++) if (v.size() >= (size_t)copier + places[i] + 64) hp[i] = &v[copier + places[i]];
        return snes_parse(hp, v.size());
    }
};

int main()
{
    // --- LoROM, 1 MiB, no RAM, NTSC: the header at $7FC0
    {
        Img i(0x100000, SNES_HDR_LO, 0x20, 0x00, 0x0A, 0x00, 0x01);
        SnesInfo r = i.parse();
        CHECK(r.ok);
        CHECK_EQ(r.mapping, 0);
        CHECK_EQ(r.rom_type, 0x00);
        CHECK_EQ(r.rom_mask, 0x0FFFFFu);                       // (1024 << 10) - 1
        CHECK_EQ(r.ram_mask, 0u);
        CHECK(!r.pal && !r.battery);
        CHECK_EQ(r.copier, 0u);
        CHECK(!strcmp(r.title, "SYNTHETIC TEST CART"));
    }
    // --- LoROM with a battery RAM of 8 KiB and a copier header: the same image 512 bytes further, ram_mask = (1024 << 3) - 1
    {
        Img i(0x80000, SNES_HDR_LO, 0x20, 0x02, 0x09, 0x03, 0x00, true);
        SnesInfo r = i.parse();
        CHECK(r.ok);
        CHECK_EQ(r.copier, 512u);
        CHECK_EQ(r.rom_bytes, 0x80000u);
        CHECK_EQ(r.rom_mask, 0x7FFFFu);
        CHECK_EQ(r.ram_mask, 0x1FFFu);
        CHECK_EQ(r.ram_bytes, 0x2000u);
        CHECK(r.battery);
    }
    // --- HiROM, 2 MiB, 32 KiB battery RAM, PAL (country 2): the header at $FFC0, mapper byte 1
    {
        Img i(0x200000, SNES_HDR_HI, 0x21, 0x02, 0x0B, 0x05, 0x02);
        SnesInfo r = i.parse();
        CHECK(r.ok);
        CHECK_EQ(r.mapping, 1);
        CHECK_EQ(r.rom_type, 0x01);
        CHECK_EQ(r.rom_mask, 0x1FFFFFu);
        CHECK_EQ(r.ram_mask, 0x7FFFu);
        CHECK(r.pal);
    }
    // --- ExHiROM, 6 MiB: the header at $40FFC0, mapper byte 2, the size byte says 8 MiB
    {
        Img i(0x600000, SNES_HDR_EX, 0x25, 0x02, 0x0D, 0x03, 0x01);
        SnesInfo r = i.parse();
        CHECK(r.ok);
        CHECK_EQ(r.mapping, 2);
        CHECK_EQ(r.rom_type, 0x02);
        CHECK_EQ(r.rom_mask, 0x7FFFFFu);
    }
    // --- the image is larger than its header says: the image wins (the mask must cover it)
    {
        Img i(0x100000, SNES_HDR_LO, 0x20, 0x00, 0x08, 0x00, 0x01);      // header: 256 KiB, file: 1 MiB
        SnesInfo r = i.parse();
        CHECK(r.ok);
        CHECK_EQ(r.rom_mask, 0xFFFFFu);
    }
    // --- the S-RTC cartridge (ExHiROM, chipset 0x55): mapper byte 0x0A (bit 3 = the RTC's decoding, DSP_LHRomMap.vhd), the OBC-1 (LoROM, chipset 0x25): 0xC0
    {
        Img i(0x500000, SNES_HDR_EX, 0x25, 0x55, 0x0C, 0x03, 0x00);
        SnesInfo r = i.parse();
        CHECK(r.ok);
        CHECK_EQ(r.rom_type, 0x0A);
        Img o(0x100000, SNES_HDR_LO, 0x20, 0x25, 0x0A, 0x03, 0x01);
        SnesInfo q = o.parse();
        CHECK(q.ok);
        CHECK_EQ(q.rom_type, 0xC0);
        // an RTC cartridge that is not ExHiROM, an OBC-1 that is not LoROM: refused
        Img bad(0x100000, SNES_HDR_LO, 0x20, 0x55, 0x0A, 0x03, 0x00);
        CHECK(!bad.parse().ok);
        Img bad2(0x200000, SNES_HDR_HI, 0x21, 0x25, 0x0B, 0x03, 0x00);
        CHECK(!bad2.parse().ok);
    }
    // --- S12: a header that names a coprocessor the core does not have is refused, with the chip in the sentence
    {
        struct { uint8_t chipset; const char *word; } chips[] = {
            { 0x03, "DSP" }, { 0x05, "DSP" }, { 0x13, "Super FX" }, { 0x15, "Super FX" }, { 0x34, "SA-1" }, { 0x35, "SA-1" }, { 0x43, "S-DD1" }, { 0x45, "S-DD1" },
            { 0xE3, "add-on" }, { 0xF3, "Cx4" }, { 0xF5, "SPC7110" }, { 0xF9, "SPC7110" }, { 0xF6, "ST010" } };
        for (auto &c : chips) {
            Img i(0x200000, SNES_HDR_LO, 0x20, c.chipset, 0x0B, 0x03, 0x00);
            SnesInfo r = i.parse();
            CHECK(!r.ok);
            CHECK(strstr(r.err, c.word) != 0);
        }
    }
    // --- the region: 50 Hz countries are PAL, Japan, USA, Korea, Canada, Brazil NTSC
    {
        uint8_t pal[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 17 }, ntsc[] = { 0, 1, 13, 15, 16 };
        for (uint8_t c : pal) { Img i(0x80000, SNES_HDR_LO, 0x20, 0x00, 0x09, 0x00, c); CHECK(i.parse().pal); }
        for (uint8_t c : ntsc) { Img i(0x80000, SNES_HDR_LO, 0x20, 0x00, 0x09, 0x00, c); CHECK(!i.parse().pal); }
    }
    // --- not a cartridge: zeros, a short file, an image over 8 MiB
    {
        std::vector<uint8_t> z(0x100000, 0);
        const uint8_t *hp[3] = { &z[SNES_HDR_LO], &z[SNES_HDR_HI], 0 };
        SnesInfo r = snes_parse(hp, z.size());
        CHECK(!r.ok);
        CHECK(strstr(r.err, "No Super Nintendo header") != 0);
        const uint8_t *none[3] = { 0, 0, 0 };
        CHECK(!snes_parse(none, 1000).ok);
        Img big(0x900000, SNES_HDR_EX, 0x25, 0x00, 0x0D, 0x00, 0x01);
        CHECK(!big.parse().ok);
    }
    // --- two candidate places, the valid one wins: a HiROM image with LoROM-place garbage
    {
        Img i(0x200000, SNES_HDR_HI, 0x21, 0x00, 0x0B, 0x00, 0x01);
        for (int k = 0; k < 64; k++) i.v[SNES_HDR_LO + k] = (uint8_t)(k * 37);
        CHECK_EQ(i.parse().mapping, 1);
    }
    DONE("snes_rom");
}
