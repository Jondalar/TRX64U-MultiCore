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

    // T198 S7, D7: "start with this config" is a tail field behind the time; the version-1 bytes and the older checks do not move
    {
        Handover c;
        handover_fill(&c, "/SD/amiga1200.cor", "Amiga 1200 (AGA)", "0.3.0", 0, 0, "amiga1200");
        CHECK_STR(handover_config(&c), "");                              // a block without one starts plain
        uint32_t sum0 = c.sum;
        CHECK(handover_set_config(&c, "Workbench 3.1 HD"));
        CHECK_STR(handover_config(&c), "Workbench 3.1 HD");
        CHECK_EQ(c.sum, sum0);                                           // outside handover_sum(): an old reader still accepts the block
        CHECK(handover_valid(&c));
        CHECK_STR(handover_core_id(&c), "amiga1200");
        CHECK(offsetof(Handover, config_sum) > offsetof(Handover, time_time));
        CHECK(sizeof(c.config) > HANDOVER_CONFIG_MAX);
        // an old reader: it computes the sum over the same 360 bytes, whatever follows
        Handover o = c;
        memset((uint8_t *)&o + offsetof(Handover, config_sum), 0, sizeof(Handover) - offsetof(Handover, config_sum));
        CHECK(handover_valid(&o));
        CHECK_STR(handover_config(&o), "");                              // a block of an older host: no config, plain start
        // names: empty, too long, and the longest that fits
        CHECK(!handover_set_config(&c, ""));
        CHECK_STR(handover_config(&c), "");
        CHECK(!handover_set_config(&c, "0123456789012345678901234567890123"));
        CHECK_STR(handover_config(&c), "");
        CHECK(handover_set_config(&c, "01234567890123456789012345678901"));
        CHECK_EQ(strlen(handover_config(&c)), 32);
        // a damaged or stale tail is never read as a name: every flipped bit is a plain start or the very name
        CHECK(handover_set_config(&c, "Demo collection"));
        int wrongc = 0;
        for (unsigned i = offsetof(Handover, config_sum); i < offsetof(Handover, safe_sum); i++) {   // the config's own field: its check word and its 48 bytes
            for (int bit = 0; bit < 8; bit++) {
                Handover x = c;
                ((uint8_t *)&x)[i] ^= (uint8_t)(1 << bit);
                wrongc += strcmp(handover_config(&x), "") != 0;
            }
        }
        CHECK_EQ(wrongc, 0);
        // the same for the host version (T221) and the safe flag (T224): a flipped bit in their own field is never read as the value
        {
            Handover v = c;
            CHECK(handover_set_host_version(&v, "0.2.0-dev+1a2b3c4d"));
            handover_set_safe(&v, true);
            CHECK(handover_safe(&v));
            int wrongv = 0, wrongs = 0;
            for (unsigned i = offsetof(Handover, safe_sum); i < sizeof(v); i++) {
                for (int bit = 0; bit < 8; bit++) {
                    Handover x = v;
                    ((uint8_t *)&x)[i] ^= (uint8_t)(1 << bit);
                    if (i < offsetof(Handover, hostver_sum)) {
                        wrongs += handover_safe(&x) ? 1 : 0;
                    } else {
                        wrongv += strcmp(handover_host_version(&x), "") != 0;
                    }
                }
            }
            CHECK_EQ(wrongs, 0);
            CHECK_EQ(wrongv, 0);
        }
        // a config left in the page by another start (the other block has another path, hence another sum) is not this block's
        Handover n2;
        handover_fill(&n2, "/SD/other.cor", "Other", "1.0", 0, 0, "other");
        memcpy(n2.config, c.config, sizeof(c.config));
        n2.config_sum = c.config_sum;
        CHECK_STR(handover_config(&n2), "");
        // the time field does not disturb it
        handover_set_time(&c, 2026, 11, 10, 12, 0, 0);
        CHECK_STR(handover_config(&c), "Demo collection");
        int y, mo, d, hh, mm, ss;
        CHECK(handover_time(&c, &y, &mo, &d, &hh, &mm, &ss));
    }
    // T224 D2: the safe-graphics flag is a tail field of its own: set, read back, never from noise or from another block, independent of the config
    {
        Handover s;
        handover_fill(&s, "/SD/cores/amiga1200.cor", "Amiga 1200", "0.1.0", 0x12345678u, 0, "amiga1200");
        CHECK(!handover_safe(&s));                                    // a page nobody asked
        handover_set_config(&s, "Demo collection");
        handover_set_safe(&s, true);
        CHECK(handover_safe(&s) && handover_valid(&s));
        CHECK_STR(handover_config(&s), "Demo collection");
        handover_set_safe(&s, false);
        CHECK(!handover_safe(&s));
        handover_set_safe(&s, true);
        Handover o;
        handover_fill(&o, "/SD/other.cor", "Other", "1.0", 0, 0, "other");
        o.safe_sum = s.safe_sum;                                      // left behind by another start: its sum is another one
        CHECK(!handover_safe(&o));
        Handover z;
        memset(&z, 0xA5, sizeof(z));
        CHECK(!handover_safe(&z));                                    // noise
    }
    // T223 D4: the start file of a large core: path on the .cor's medium, text round trip, refusals
    {
        char p[96], t[STARTFILE_MAX], cor[HANDOVER_PATH_MAX], cfg[HANDOVER_CONFIG_MAX + 2];
        bool safe;
        CHECK(startfile_path("/SD/cores/c64scpu.cor", p, sizeof(p)));
        CHECK_STR(p, "/SD/trx64-start.txt");
        CHECK(startfile_path("/USB0/cores/c20.cor", p, sizeof(p)) && !strcmp(p, "/USB0/trx64-start.txt"));
        CHECK(!startfile_path("c.cor", p, sizeof(p)) && !startfile_path("/c.cor", p, sizeof(p)));
        int n = startfile_format(t, sizeof(t), "/SD/cores/c64scpu.cor", "Demo collection", true);
        CHECK(n > 0 && startfile_parse(t, n, cor, sizeof(cor), cfg, sizeof(cfg), &safe));
        CHECK_STR(cor, "/SD/cores/c64scpu.cor");
        CHECK_STR(cfg, "Demo collection");
        CHECK(safe);
        n = startfile_format(t, sizeof(t), "/SD/cores/c64scpu.cor", "", false);
        CHECK(n > 0 && startfile_parse(t, n, cor, sizeof(cor), cfg, sizeof(cfg), &safe) && !cfg[0] && !safe);
        CHECK(!startfile_parse("TRXSTART2\ncor=/SD/x.cor\n", 25, cor, sizeof(cor), cfg, sizeof(cfg), &safe));      // another version
        CHECK(!startfile_parse("TRXSTART1\nconfig=a\n", 19, cor, sizeof(cor), cfg, sizeof(cfg), &safe));           // no cor=
        CHECK(!startfile_parse("noise", 5, cor, sizeof(cor), cfg, sizeof(cfg), &safe));
        CHECK(startfile_format(t, sizeof(t), "/SD/x.cor", "0123456789012345678901234567890123", false) == 0);         // 34 characters: over HANDOVER_CONFIG_MAX
    }
    DONE("test_handover");
}
