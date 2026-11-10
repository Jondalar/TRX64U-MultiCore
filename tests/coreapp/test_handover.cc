// T93 D11 / S6: the hand-over page and the .cfg path that follows the .cor.
#include <string.h>
#include <stddef.h>
#include "check.h"
#include "handover.h"

int main()
{
    CHECK(sizeof(Handover) <= 4096);
    CHECK_EQ(HANDOVER_ADDR & 0xFFF, 0);
    // the page lies outside what the spec names: the application's load range, the staging areas, the trampoline
    CHECK(HANDOVER_ADDR >= 0x03C00000u && HANDOVER_ADDR + 4096 <= 0x03F00000u);

    Handover h;
    handover_fill(&h, "/USB1/cores/atari800.cor", "Atari 800 XL", "0.0.1", 0x9bf12877u);
    CHECK(handover_valid(&h));
    const uint8_t *raw = (const uint8_t *)&h;
    CHECK(raw[0] == 'T' && raw[1] == 'R' && raw[2] == 'X' && raw[3] == 'H');     // the bytes DDR2 holds
    CHECK_STR(h.path, "/USB1/cores/atari800.cor");
    CHECK_STR(h.core_name, "Atari 800 XL");
    CHECK_STR(h.core_version, "0.0.1");
    CHECK_STR(h.host_build, "9bf12877");

    // noise is not a block: every single flipped byte is seen (the version-1 bytes; the id behind them has its own check below)
    int missed = 0;
    for (unsigned i = 0; i < offsetof(Handover, id_sum); i++) {
        Handover g = h;
        ((uint8_t *)&g)[i] ^= 0x04;
        if (handover_valid(&g)) {
            missed++;
        }
    }
    CHECK_EQ(missed, 0);
    Handover z;
    memset(&z, 0, sizeof(z));
    CHECK(!handover_valid(&z));
    memset(&z, 0xFF, sizeof(z));
    CHECK(!handover_valid(&z));

    // T115 7 H3, I6: the core's id. No id: "". With an id: read back; version 1 and the same sum, so a reader of before accepts the block
    CHECK_STR(handover_core_id(&h), "");
    Handover g;
    handover_fill(&g, "/SD/amiga1200.cor", "Amiga 1200 (AGA)", "0.3.0", 0, 0, "amiga1200");
    CHECK(handover_valid(&g));
    CHECK_EQ(g.version, 1u);
    CHECK_STR(handover_core_id(&g), "amiga1200");
    {   // the old layout: the same block without the id fields has the same sum (what the version-1 reader computes over its 360 bytes)
        Handover f;
        handover_fill(&f, "/SD/amiga1200.cor", "Amiga 1200 (AGA)", "0.3.0", 0, 0);
        CHECK_EQ(f.sum, g.sum);
        CHECK(offsetof(Handover, id_sum) == 360);
    }
    // an id that was damaged or belongs to another block is never read as a different id
    int wrong = 0;
    for (unsigned i = offsetof(Handover, id_sum); i < sizeof(g); i++) {
        for (int bit = 0; bit < 8; bit++) {
            Handover x = g;
            ((uint8_t *)&x)[i] ^= (uint8_t)(1 << bit);
            wrong += strcmp(handover_core_id(&x), "") && strcmp(handover_core_id(&x), "amiga1200");
            wrong += !strcmp(handover_core_id(&x), "amiga1200") && i < offsetof(Handover, core_id) + 9;   // a flip inside the id text must not survive
        }
    }
    CHECK_EQ(wrong, 0);
    {   // bytes left behind by an earlier start (a block with another id, then an old host that fills only the old size)
        Handover old;
        handover_fill(&old, "/SD/amiga.cor", "Amiga", "0.2.0", 0, 0, "amiga");
        Handover mixed = g;
        memcpy(&mixed, &old, offsetof(Handover, id_sum));          // the old host wrote its version-1 bytes over this block
        CHECK(handover_valid(&mixed));
        CHECK_STR(handover_core_id(&mixed), "");                   // the id behind belongs to g's sum, not to this one
    }
    memset(&z, 0xFF, sizeof(z));
    CHECK_STR(handover_core_id(&z), "");
    memset(&z, 0, sizeof(z));
    CHECK_STR(handover_core_id(&z), "");

    // a long path is cut and stays terminated; the block is still valid
    char longp[600];
    memset(longp, 'x', sizeof(longp) - 1);
    longp[sizeof(longp) - 1] = 0;
    handover_fill(&h, longp, longp, longp, 0);
    CHECK(handover_valid(&h));
    CHECK_EQ(strlen(h.path), HANDOVER_PATH_MAX - 1);
    CHECK_STR(h.host_build, "");

    // S6: the .cfg lies beside the .cor, whichever medium it is on, never in a fixed place
    char out[300];
    CHECK(handover_cfg_path("/USB1/cores/amiga.cor", out, sizeof(out)));
    CHECK_STR(out, "/USB1/cores/amiga.cfg");
    CHECK(handover_cfg_path("/SD/cores/amiga.cor", out, sizeof(out)));
    CHECK_STR(out, "/SD/cores/amiga.cfg");
    CHECK(handover_cfg_path("/Flash/Cores/A.COR", out, sizeof(out)));
    CHECK_STR(out, "/Flash/Cores/A.cfg");
    // T112: a core in its own folder keeps its .cfg in that folder
    CHECK(handover_cfg_path("/USB1/cores/apple2/apple2.cor", out, sizeof(out)));
    CHECK_STR(out, "/USB1/cores/apple2/apple2.cfg");
    CHECK(!handover_cfg_path("/USB1/cores/amiga.bin", out, sizeof(out)));
    CHECK(!handover_cfg_path("/USB1/cores/.cor", out, sizeof(out)) || strcmp(out, "/USB1/cores/.cfg") == 0);
    CHECK(!handover_cfg_path("", out, sizeof(out)));
    CHECK(!handover_cfg_path("/a/b.cor", out, 8));       // does not fit: refused, not cut
    DONE("test_handover");
}
