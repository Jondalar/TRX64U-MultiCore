// T139: BlockDevice_Flash::write over a run of sectors (FatFS hands over runs; write_scratch writes 64 KB = 16 sectors at a time).
// The fake flash has the real semantics: a program can only clear bits (AND), only an erase sets a sector to 0xFF, and the erases are counted.
// Board finding 2026-10-28: only the first sector of a run was erased, the other 15 were programmed over old data (one bit left set).
#include "blockdev_flash.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

class FakeFlash : public Flash {
public:
    int psize, ssize, nsect;
    bool erase;
    std::vector<uint8_t> mem;
    int erases;
    std::vector<int> erased;
    FakeFlash(int ps, int ss, int ns, bool er) : psize(ps), ssize(ss), nsect(ns), erase(er), mem(ss * ns, 0xFF), erases(0) { }
    void get_image_addresses(int id, t_flash_address *a)
    {
        a->id = id; a->has_header = 0; a->start = 0; a->device_addr = 0; a->max_length = ssize * nsect;
    }
    int get_page_size(void) { return psize; }
    int get_sector_size(int) { return ssize; }
    int page_to_sector(int page) { return page * psize / ssize; }
    bool need_erase(void) { return erase; }
    bool erase_sector(int s)
    {
        if (s < 0 || s >= nsect) return false;
        memset(&mem[s * ssize], 0xFF, ssize); erases++; erased.push_back(s); return true;
    }
    bool read_page(int p, void *b) { memcpy(b, &mem[p * psize], psize); return true; }
    bool write_page(int p, const void *b)
    {
        const uint8_t *s = (const uint8_t *)b;
        for (int i = 0; i < psize; i++) mem[p * psize + i] = erase ? (mem[p * psize + i] & s[i]) : s[i];
        return true;
    }
};

static std::vector<uint8_t> pattern(int n, int seed) { std::vector<uint8_t> v(n); for (int i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + seed); return v; }

int main()
{
    // NOR flash: 256-byte pages, 4 KB erase sectors, 64 of them. The old content has zero bits where the new one has ones.
    {
        FakeFlash f(256, 4096, 64, true);
        BlockDevice_Flash bd(&f);
        CHECK(bd.status() == 0);
        std::vector<uint8_t> old = pattern(4096 * 64, 0x55);
        for (size_t i = 0; i < old.size(); i++) f.mem[i] = old[i];           // programmed over erased flash: old data as is
        std::vector<uint8_t> nw(16 * 4096);
        for (size_t i = 0; i < nw.size(); i++) nw[i] = (uint8_t)(i * 13 + 0xAA) | 0x88;
        CHECK(bd.write(&nw[0], 2, 16) == RES_OK);
        std::vector<uint8_t> back(16 * 4096);
        CHECK(bd.read(&back[0], 2, 16) == RES_OK);
        CHECK(back == nw);
        CHECK(f.erases == 16);
        for (int i = 0; i < 16; i++) CHECK(i < (int)f.erased.size() && f.erased[i] == 2 + i);
        CHECK(memcmp(&f.mem[0], &old[0], 2 * 4096) == 0);                    // the sectors before and after the run are untouched
        CHECK(memcmp(&f.mem[18 * 4096], &old[18 * 4096], 46 * 4096) == 0);
        // the single-sector write and a run of two, again over non-erased data
        f.erases = 0; f.erased.clear();
        CHECK(bd.write(&nw[0], 40, 1) == RES_OK && f.erases == 1);
        CHECK(bd.write(&nw[4096], 41, 2) == RES_OK && f.erases == 3);
        CHECK(bd.read(&back[0], 40, 3) == RES_OK);
        CHECK(memcmp(&back[0], &nw[0], 4096) == 0 && memcmp(&back[4096], &nw[4096], 8192) == 0);
        // a run that leaves the flash fails, a first sector outside is a parameter error
        CHECK(bd.write(&nw[0], 100000, 1) == RES_PARERR);
    }
    // AT45: 528-byte pages, 512-byte sectors, 1 page each, no erase: the pages are overwritten, nothing is erased
    {
        FakeFlash f(528, 528, 64, false);
        BlockDevice_Flash bd(&f);
        std::vector<uint8_t> nw = pattern(528 * 8, 0x21), back(528 * 8);
        CHECK(bd.write(&nw[0], 3, 8) == RES_OK);
        CHECK(bd.read(&back[0], 3, 8) == RES_OK);
        CHECK(back == nw);
        CHECK(f.erases == 0);
    }
    if (fails) { printf("%d check(s) red\n", fails); return 1; }
    printf("test_flashwrite: all checks green\n");
    return 0;
}
