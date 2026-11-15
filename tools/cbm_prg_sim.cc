// T217: the writer of the real-ROM program-start benches (cores/c64scpu/sim/run_prgrun.sh, cores/c128/sim/run_prgrun.sh). It places a .prg with the SAME library
// code and the SAME numbers the core application uses on the board (app/loaders/cbm_prg.cc; coreapp/cores/c64scpu_prg.h, c128_prg.h; the key matrix of
// cbm_run.cc's typed start) against a recording model of the machine, and writes what reached the machine as the bench's script:
//   W|<DDR2 byte address, hex>|<byte>     the application's write into the DDR2 window (the SuperCPU's SRAM)
//   R|<C64 RAM address, hex>|<byte>       the application's write into the RAM window
//   F|1                                   the CPU cache flush (ROM_CTRL.b0) that follows the placement
//   K|<ms after the start>|<64 bit hex>   the key matrix the application presses, in time
//   I|text                                the log line the board prints
// Usage: cbm_prg_sim <scpu|c128> <new|old> <file.prg>
//   new  the way the application starts a program now (T217); old  what it did before (the RAM window and the keyboard buffer only: the control of the bench)
// The state the machine is in at READY is preset: BASIC's TXTTAB points at the start of BASIC (the bench checks it afterwards against the real ROM).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "fakes.h"
#include "cbm_prg.h"
#include "c64scpu_prg.h"
#include "c128_prg.h"

struct Rec : FakeIo {
    std::vector<std::pair<uint32_t, uint8_t>> dw, rw;
    void ddr_write(uint32_t off, const void *p, uint32_t n)
    {
        FakeIo::ddr_write(off, p, n);
        for (uint32_t i = 0; i < n; i++) dw.push_back({ off + i, ((const uint8_t *)p)[i] });
    }
    void ram_write(uint32_t a, const void *p, uint32_t n)
    {
        FakeIo::ram_write(a, p, n);
        for (uint32_t i = 0; i < n; i++) rw.push_back({ a + i, ((const uint8_t *)p)[i] });
    }
};

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: cbm_prg_sim <scpu|c128> <new|old> <file.prg>\n"); return 2; }
    bool scpu = !strcmp(argv[1], "scpu"), neu = !strcmp(argv[2], "new");
    FILE *f = fopen(argv[3], "rb");
    if (!f) { perror(argv[3]); return 2; }
    std::vector<uint8_t> file(0x20000);
    size_t n = fread(file.data(), 1, file.size(), f);
    fclose(f);
    Rec io;
    io.ram.assign(65536, 0);
    io.ddr.assign(0x04000000, 0);
    CbmPrgParams p;
    uint16_t txt = scpu ? 0x0801 : 0x1C01;
    if (scpu) scpu_prg_params(&p); else c128_prg_params(&p);
    if (!neu) p.mem_ddr = 0;
    io.ram[p.txttab] = (uint8_t)txt; io.ram[p.txttab + 1] = (uint8_t)(txt >> 8);
    if (p.mem_ddr) { io.ddr[p.mem_ddr + p.txttab] = (uint8_t)txt; io.ddr[p.mem_ddr + p.txttab + 1] = (uint8_t)(txt >> 8); }
    CbmPrgResult r;
    char msg[200] = "";
    if (cbm_prg_place(&io, file.data(), (uint32_t)n, p, false, &r, msg, sizeof(msg))) { fprintf(stderr, "place: %s\n", msg); return 1; }
    printf("I|%s: $%04X-$%04X, %s\n", scpu ? "c64scpu" : "c128", r.load, (unsigned)(r.end - 1) & 0xFFFF, r.basic ? "RUN" : "SYS");
    for (auto &w : io.dw) printf("W|%x|%02x\n", 0x01000000u + w.first, w.second);
    for (auto &w : io.rw) printf("R|%x|%02x\n", w.first, w.second);
    if (p.mem_ddr) printf("F|1\n");
    if (!neu || !scpu) {                                // the keyboard buffer: the old way, and the C128's (its CPU reads the window's RAM)
        io.rw.clear();
        cbm_prg_type_start(&io, p, r);
        for (auto &w : io.rw) printf("R|%x|%02x\n", w.first, w.second);
        return 0;
    }
    char line[32];
    snprintf(line, sizeof(line), r.basic ? "RUN\r" : "SYS%u\r", (unsigned)r.start);
    uint32_t t = 0;
    for (const char *c = line; *c; c++) {
        CbmKey k;
        if (!cbm_key_for((uint8_t)*c, &k)) { fprintf(stderr, "no key for %02x\n", *c); return 1; }
        uint64_t key = 1ull << k.idx, sh = 1ull << 15;
        if (k.shift) { printf("K|%u|%016llx\n", t, (unsigned long long)sh); t += 40; key |= sh; }
        printf("K|%u|%016llx\n", t, (unsigned long long)key);
        t += SCPU_KEY_HOLD_MS;
        printf("K|%u|%016llx\n", t, 0ull);
        t += SCPU_KEY_GAP_MS;
    }
    return 0;
}
