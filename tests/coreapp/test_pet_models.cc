// T134: the PET models of the core app (coreapp/cores/pet_models.h): the ROM file each model takes (VICE's names, 50/60 Hz editor, the 4 KiB
// character file of the 8032) and the PRG loader's numbers for BASIC 1 (2001) next to BASIC 2/4.
#include "pet_models.h"
#include "check.h"
#include "fakes.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static bool first_is(int model, bool hz50, int role, const char *want)
{
    const char *n[6];
    int c = pet_rom_names(model, hz50, role, n);
    return c > 0 && !strcmp(n[0], want) && n[c] == 0;
}

static std::vector<uint8_t> prg(uint16_t load)        // "10 PRINT": link, line number, one token, 0; the end marker
{
    std::vector<uint8_t> v;
    v.push_back((uint8_t)(load & 0xFF)); v.push_back((uint8_t)(load >> 8));
    uint16_t l2 = (uint16_t)(load + 6);
    v.push_back((uint8_t)(l2 & 0xFF)); v.push_back((uint8_t)(l2 >> 8));
    v.push_back(10); v.push_back(0); v.push_back(0x99); v.push_back(0);
    v.push_back(0); v.push_back(0);
    return v;
}

int main()
{
    // --- the files per model
    CHECK(first_is(PET_M_2001, false, PET_R_BASIC, "basic-1.901439-09-05-02-06.bin"));
    CHECK(first_is(PET_M_2001, false, PET_R_EDITOR, "edit-1-n.901439-03.bin"));
    CHECK(first_is(PET_M_2001, false, PET_R_KERNAL, "kernal-1.901439-04-07.bin"));
    CHECK(first_is(PET_M_2001, false, PET_R_CHAR, "characters-1.901447-08.bin"));
    CHECK(first_is(PET_M_3032, false, PET_R_BASIC, "basic-2.901465-01-02.bin"));
    CHECK(first_is(PET_M_3032, false, PET_R_EDITOR, "edit-2-n.901447-24.bin"));
    CHECK(first_is(PET_M_3032, false, PET_R_KERNAL, "kernal-2.901465-03.bin"));
    CHECK(first_is(PET_M_3032, false, PET_R_CHAR, "characters-2.901447-10.bin"));
    CHECK(first_is(PET_M_4032, false, PET_R_BASIC, "basic-4.901465-23-20-21.bin"));
    CHECK(first_is(PET_M_4032, false, PET_R_EDITOR, "edit-4-40-n-60Hz.901499-01.bin"));
    CHECK(first_is(PET_M_4032, true, PET_R_EDITOR, "edit-4-40-n-50Hz.901498-01.bin"));
    CHECK(first_is(PET_M_4032, false, PET_R_KERNAL, "kernal-4.901465-22.bin"));
    CHECK(first_is(PET_M_4032, false, PET_R_CHAR, "characters-2.901447-10.bin"));
    for (int m = PET_M_8032; m <= PET_M_SUPERPET; m++) {         // 8032, 8096 and the SuperPET (starting as an 8032) share the 80 column set
        CHECK(first_is(m, false, PET_R_BASIC, "basic-4.901465-23-20-21.bin"));
        CHECK(first_is(m, false, PET_R_EDITOR, "edit-4-80-b-60Hz.901474-03.bin"));
        CHECK(first_is(m, true, PET_R_EDITOR, "edit-4-80-b-50Hz.901474-04.bin"));
        CHECK(first_is(m, false, PET_R_CHAR, "characters.901640-01.bin"));
    }
    const char *n[6];
    pet_rom_names(PET_M_8032, false, PET_R_CHAR, n);
    CHECK(!strcmp(n[1], "characters-2.901447-10.bin"));            // without the 4 KiB file the 2 KiB set serves
    CHECK(pet_crtc(PET_M_8032) && pet_crtc(PET_M_8096) && !pet_crtc(PET_M_4032));

    // --- the loader's numbers
    CbmPrgParams p1 = pet_prg_params(PET_M_2001), p4 = pet_prg_params(PET_M_4032);
    CHECK_EQ(p1.basic_starts[0], 0x0400); CHECK_EQ(p1.txttab, 0x7A); CHECK_EQ(p1.vartab, 0x7C); CHECK_EQ(p1.arytab, 0x7E); CHECK_EQ(p1.strend, 0x80);
    CHECK_EQ(p1.kbd_buffer, 0x0270); CHECK_EQ(p1.kbd_count, 0x020D);
    CHECK_EQ(p4.basic_starts[0], 0x0401); CHECK_EQ(p4.txttab, 0x28); CHECK_EQ(p4.vartab, 0x2A); CHECK_EQ(p4.arytab, 0x2C); CHECK_EQ(p4.strend, 0x2E);
    CHECK_EQ(p4.kbd_buffer, 0x026F); CHECK_EQ(p4.kbd_count, 0x9E);

    // --- a BASIC program placed on the 2001: text pointer pair at $7A holds $0400, the end pointers at $7C, $7E, $80 follow the program, $28.. stay zero
    {
        FakeIo io;
        char msg[160];
        CbmPrgResult r;
        io.ram_write(p1.txttab, "\x00\x04", 2);
        std::vector<uint8_t> f = prg(0x0400);
        CHECK_EQ(cbm_prg_place(&io, f.data(), (uint32_t)f.size(), p1, false, &r, msg, sizeof(msg)), 0);
        CHECK(r.basic);
        CHECK_EQ(r.load, 0x0400);
        uint8_t b[2];
        uint16_t end = (uint16_t)(0x0400 + f.size() - 2);
        io.ram_read(0x7C, b, 2); CHECK_EQ(b[0] | (b[1] << 8), end);
        io.ram_read(0x7E, b, 2); CHECK_EQ(b[0] | (b[1] << 8), end);
        io.ram_read(0x80, b, 2); CHECK_EQ(b[0] | (b[1] << 8), end);
        io.ram_read(0x2A, b, 2); CHECK_EQ(b[0] | (b[1] << 8), 0);
    }
    // --- the same file on a 4032 (BASIC 4): loaded at $0401, pointers at $2A..$2E
    {
        FakeIo io;
        char msg[160];
        CbmPrgResult r;
        io.ram_write(p4.txttab, "\x01\x04", 2);
        std::vector<uint8_t> f = prg(0x0401);
        CHECK_EQ(cbm_prg_place(&io, f.data(), (uint32_t)f.size(), p4, false, &r, msg, sizeof(msg)), 0);
        CHECK(r.basic);
        uint8_t b[2];
        io.ram_read(0x2E, b, 2); CHECK_EQ(b[0] | (b[1] << 8), (uint16_t)(0x0401 + f.size() - 2));
        io.ram_read(0x80, b, 2); CHECK_EQ(b[0] | (b[1] << 8), 0);
    }
    printf("test_pet_models: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
