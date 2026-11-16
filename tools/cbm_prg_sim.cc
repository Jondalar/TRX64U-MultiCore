// T217: the writer of the real-ROM program-start benches (cores/c64scpu/sim/run_prgrun.sh, cores/c128/sim/run_prgrun.sh). It places a .prg with the SAME library
// code and the SAME numbers the core application uses on the board (app/loaders/cbm_prg.cc; coreapp/cores/c64scpu_prg.h, c128_prg.h; the key matrix of
// cbm_run.cc's typed start) against a recording model of the machine, and writes what reached the machine as the bench's script:
//   1 <DDR2 byte address, hex> <byte>      the application's write into the DDR2 window (the SuperCPU's SRAM)
//   2 <C64 RAM address, hex> <byte>        the application's write into the RAM window
//   3 1                                    the CPU cache flush (ROM_CTRL.b0) that follows the placement
//   4 <ms after the start> <64 bit hex>    the key matrix the application presses, in time
//   0 text                                 the log line the board prints
//   5 <ms>                                 wait that long (cache lines emptied first), 6 <n> <hex>  wait until the 40-column screen holds those n screen codes
// Usage: cbm_prg_sim <scpu|c128> <new|old|go64> <file.prg>
//   new  the way the application starts a program now (T217); old  what it did before (the RAM window and the keyboard buffer only: the control of the bench)
//   go64 (c128 only, T217 D4) the C128 starts a C64 program: GO64 typed, the wait for its question, Y typed, the wait for the C64's screen, the program placed with the C64 numbers
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

static void wait_screen(const uint8_t *b, int n)
{
    printf("6 %d ", n);
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

static void put_keys_ram(Rec &io, const CbmPrgParams &p, const char *s, size_t n)
{
    io.rw.clear();
    io.ram_write(p.kbd_buffer, s, (uint32_t)n);
    uint8_t c = (uint8_t)n;
    io.ram_write(p.kbd_count, &c, 1);
    for (auto &w : io.rw) printf("2 %x %02x\n", w.first, w.second);
    io.rw.clear();
}

// the C128 starts a C64 program (cbm_run.cc service_c64, the steps in time): GO64 typed; its count taken, then the question is on the screen; Y typed; the
// C64's screen with READY and TXTTAB $0801; the program placed with the C64 numbers, RUN typed into the C64's keyboard buffer
static int go64_script(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    std::vector<uint8_t> file(0x20000);
    size_t n = fread(file.data(), 1, file.size(), f);
    fclose(f);
    Rec io;
    io.ram.assign(65536, 0);
    CbmPrgParams p128, p64;
    c128_prg_params(&p128);
    c128_prg_params_c64(&p64);
    const char *sw = C128_SWITCH_TEXT, *bar = strchr(sw, '|');
    printf("0 c128 C64 mode: GO64, the question, Y, the C64's READY, the program\n");
    put_keys_ram(io, p128, sw, (size_t)(bar - sw));
    printf("7 %x 00\n", p128.kbd_count);
    printf("5 400\n");
    wait_screen(c128_go64_ask, (int)sizeof(c128_go64_ask));
    put_keys_ram(io, p128, bar + 1, strlen(bar + 1));
    static const uint8_t banner[8] = { 2, 1, 19, 9, 3, 32, 22, 50 };                    // BASIC V2
    static const uint8_t ready[6] = { 18, 5, 1, 4, 25, 46 };                             // READY.
    wait_screen(banner, sizeof(banner));
    printf("7 %x %02x\n7 %x %02x\n", p64.txttab + 1, 0x08, p64.txttab, 0x01);
    wait_screen(ready, sizeof(ready));
    printf("5 500\n");
    io.ram[p64.txttab] = 0x01; io.ram[p64.txttab + 1] = 0x08;                         // the C64's READY: BASIC's TXTTAB
    CbmPrgResult r;
    char msg[200] = "";
    if (cbm_prg_place(&io, file.data(), (uint32_t)n, p64, false, &r, msg, sizeof(msg))) { fprintf(stderr, "place: %s\n", msg); return 1; }
    for (auto &w : io.rw) printf("2 %x %02x\n", w.first, w.second);
    io.rw.clear();
    cbm_prg_type_start(&io, p64, r);
    for (auto &w : io.rw) printf("2 %x %02x\n", w.first, w.second);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: cbm_prg_sim <scpu|c128> <new|old> <file.prg>\n"); return 2; }
    bool scpu = !strcmp(argv[1], "scpu"), neu = !strcmp(argv[2], "new"), go64 = !strcmp(argv[2], "go64");
    if (go64) neu = true;
    FILE *f = fopen(argv[3], "rb");
    if (!f) { perror(argv[3]); return 2; }
    std::vector<uint8_t> file(0x20000);
    size_t n = fread(file.data(), 1, file.size(), f);
    fclose(f);
    if (go64) return go64_script(argv[3]);
    if (go64) return go64_script(argv[3]);
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
    printf("0 %s: $%04X-$%04X, %s\n", scpu ? "c64scpu" : "c128", r.load, (unsigned)(r.end - 1) & 0xFFFF, r.basic ? "RUN" : "SYS");
    for (auto &w : io.dw) printf("1 %x %02x\n", 0x01000000u + w.first, w.second);
    for (auto &w : io.rw) printf("2 %x %02x\n", w.first, w.second);
    if (p.mem_ddr) printf("3 1\n");
    if (!neu || !scpu) {                                // the keyboard buffer: the old way, and the C128's (its CPU reads the window's RAM)
        io.rw.clear();
        cbm_prg_type_start(&io, p, r);
        for (auto &w : io.rw) printf("2 %x %02x\n", w.first, w.second);
        return 0;
    }
    char line[32];
    snprintf(line, sizeof(line), r.basic ? "RUN\r" : "SYS%u\r", (unsigned)r.start);
    uint32_t t = 0;
    for (const char *c = line; *c; c++) {
        CbmKey k;
        if (!cbm_key_for((uint8_t)*c, &k)) { fprintf(stderr, "no key for %02x\n", *c); return 1; }
        uint64_t key = 1ull << k.idx, sh = 1ull << 15;
        if (k.shift) { printf("4 %u %016llx\n", t, (unsigned long long)sh); t += 40; key |= sh; }
        printf("4 %u %016llx\n", t, (unsigned long long)key);
        t += SCPU_KEY_HOLD_MS;
        printf("4 %u %016llx\n", t, 0ull);
        t += SCPU_KEY_GAP_MS;
    }
    return 0;
}
