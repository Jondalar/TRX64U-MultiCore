// T239.3 P1-4 (D30 condition 2, D31): the one check that stays at run time. The service reads the standard block of the bitstream (the identity block of TOML A, properties "id" and "build")
// and compares it with the record: the pair of ID and build cor_pack wrote. The fixture is the TOML A of tests/coreapp/golden/desc_toml_a.toml (id "TSTA", build 0x1234ABCD, the block
// "ident" at 0xA0000800, id at +0 and build at +4); the board's registers are a model that answers read32.
//
//   test_identity DESC_TOML_A.bin
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "bundle.h"

typedef std::vector<uint8_t> Bytes;

static Bytes slurp(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

// A bitstream's standard block: the two registers at the addresses the record names, or nothing readable.
class BlockIo : public CoreIo {
public:
    uint32_t id, build;
    bool readable;
    uint32_t last;
    BlockIo() : id(0x41545354u), build(0x1234ABCDu), readable(true), last(0) { }       // "TSTA", byte 0 first
    bool read32(uint32_t addr, uint32_t *v)
    {
        last = addr;
        if (!readable) {
            return false;
        }
        if (addr == 0xA0000800u) {
            *v = id;
            return true;
        }
        if (addr == 0xA0000804u) {
            *v = build;
            return true;
        }
        return false;
    }
    uint8_t rd(uint32_t) { return 0; }
    void wr(uint32_t, uint8_t) { }
    void ddr_write(uint32_t, const void *, uint32_t) { }
    void ddr_read(uint32_t, void *, uint32_t) { }
    void rom_write(uint32_t, const void *, uint32_t) { }
    void rom_read(uint32_t, void *, uint32_t) { }
    void ram_write(uint32_t, const void *, uint32_t) { }
    void ram_read(uint32_t, void *, uint32_t) { }
    void stop(bool) { }
    void mode_reset(bool) { }
    uint32_t now_ms() { return 0; }
    void delay_ms(uint32_t) { }
    void delay_us(uint32_t) { }
};

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    Bytes b = slurp(argv[1]);
    uint8_t *exact = new uint8_t[b.size()];
    memcpy(exact, b.data(), b.size());
    DescRecord *d = new DescRecord;
    char why[240];
    CHECK_EQ(descrec_parse(exact, (uint32_t)b.size(), d, why, sizeof(why)), 0);
    CHECK(d->map && d->map->has_build);

    BlockIo io;
    why[0] = 0;
    CHECK_EQ(bundle_check_identity(&io, d, why, sizeof(why)), 0);          // the pair the record was made for
    CHECK_STR(why, "");

    io.build = 0x1234ABCEu;                                                // the same core, another bitstream
    CHECK_EQ(bundle_check_identity(&io, d, why, sizeof(why)), -1);
    CHECK(strstr(why, "1234ABCE") && strstr(why, "1234ABCD") && strstr(why, "TSTA") && strstr(why, "stays held"));
    io.build = 0x1234ABCDu;

    io.id = 0x42545354u;                                                   // "TSTB": another core's bitstream
    CHECK_EQ(bundle_check_identity(&io, d, why, sizeof(why)), -1);
    CHECK(strstr(why, "TSTB") && strstr(why, "TSTA"));
    io.id = 0x41545354u;

    io.readable = false;                                                   // the block cannot be read: not shown to belong
    CHECK_EQ(bundle_check_identity(&io, d, why, sizeof(why)), -1);
    CHECK(strstr(why, "cannot be read"));
    CHECK_EQ(bundle_check_identity(0, d, why, sizeof(why)), -1);           // no register access at all
    io.readable = true;

    // a record without TOML A, or no record, has nothing to compare
    CHECK_EQ(bundle_check_identity(&io, 0, why, sizeof(why)), 0);
    DescRecord *none = new DescRecord;
    CHECK_EQ(bundle_check_identity(&io, none, why, sizeof(why)), 0);

    // a record that carries no build is refused: the pair cannot be shown (D30 condition 2); another ID is refused first
    d->map->has_build = false;
    CHECK_EQ(bundle_check_identity(&io, d, why, sizeof(why)), -1);
    CHECK(strstr(why, "no build word") && strstr(why, "stays held"));
    io.id = 0x42545354u;
    CHECK_EQ(bundle_check_identity(&io, d, why, sizeof(why)), -1);
    CHECK(strstr(why, "TSTB"));
    DONE("identity");
}
