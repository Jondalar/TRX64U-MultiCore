// T104.1 (Alex 2026-10-26): every core registers its own machine.read_mem / machine.write_mem. For each descriptor of the table the pair is there, owned by
// the core's contributor, with the core's address width in the argument range; a read comes back with the result (never as an argument), a write takes hex;
// the access goes where the core's memory is - the base RAM window (with C64_DMA_MEMONLY on for the C64 family, and put back), the same window through a
// page register that is put back to 0, or the DDR2 window at the region's base - and stays inside one region. A core whose memory the application cannot
// reach answers OPE_UNSUPPORTED and says why. The model of the window here is the one the descriptors' own code assumes (ram_write(addr), ddr_write(off)).
#include <string>
#include <vector>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "desc_check.h"
#include "ops_glue.h"
#include "svc.h"
#ifdef TEST_APP_CPC
extern const CoreDesc cpc_desc;                 // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)
#include "cpc_regs.h"
#endif
#ifdef TEST_APP_ZX
extern const CoreDesc zx_desc;                  // cores.h lists the shared app's cores; the ZX Spectrum is its own app (cores/zx/app/lib)
#include "zx_regs.h"
#endif
#ifdef TEST_APP_AMIGA
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)
#endif
#ifdef TEST_APP_DDT
extern const CoreDesc ddt_novic_desc;           // cores.h lists the shared app's cores; DDT Look Ma, No VIC is its own app (cores/ddt_novic/app/lib)
#endif
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_ATARI800) && !defined(TEST_APP_NES) && !defined(TEST_APP_C264) && !defined(TEST_APP_NEOGEO) && !defined(TEST_APP_AMIGA)
#include "apple2_regs.h"
#include "zxnext_regs.h"
#endif
#ifdef TEST_APP_ZXNEXT
#include "zxnext_regs.h"
#endif

#ifdef TEST_APP_VC20                     // the VIC-20's app is its own build (run.sh vc20app): its core is the only one in its table
const CoreDesc *const core_table[] = { &vc20_desc, 0 };
#define HEX_CORE vc20_desc
#elif defined(TEST_APP_A1200)             // the Amiga 1200's app is its own build (run.sh a12app): its core is the only one in its table
const CoreDesc *const core_table[] = { &amiga1200_desc, 0 };
#define HEX_CORE amiga1200_desc
#elif defined(TEST_APP_ATARI800)          // the Atari 800 XL's app is its own build (run.sh a8app): its core is the only one in its table
const CoreDesc *const core_table[] = { &atari800_desc, 0 };
#define HEX_CORE atari800_desc
#elif defined(TEST_APP_NES)               // the NES's app is its own build (run.sh nesapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &nes_desc, 0 };
#elif defined(TEST_APP_C264)              // the Commodore 264's app is its own build (run.sh c264app): its core is the only one in its table
const CoreDesc *const core_table[] = { &c264_desc, 0 };
#define HEX_CORE c264_desc
#elif defined(TEST_APP_ZXNEXT)           // the ZX Spectrum Next's app is its own build (run.sh zxnextapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &zxnext_desc, 0 };
#elif defined(TEST_APP_NEOGEO)          // the Neo Geo's app is its own build (run.sh neogeoapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &neogeo_desc, 0 };
#elif defined(TEST_APP_AMIGA)           // the Amiga 500's app is its own build (run.sh amigaapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &amiga_desc, 0 };
#elif defined(TEST_APP_DDT)               // DDT Look Ma, No VIC's app is its own build (run.sh ddtapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &ddt_novic_desc, 0 };
#define HEX_CORE ddt_novic_desc
#elif defined(TEST_APP_ZX)                // the ZX Spectrum's app is its own build (run.sh zxapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &zx_desc, 0 };
#define HEX_CORE zx_desc
#elif defined(TEST_APP_CPC)               // the CPC's app is its own build (run.sh cpcapp): its core is the only one in its table
const CoreDesc *const core_table[] = { &cpc_desc, 0 };
#define HEX_CORE cpc_desc
#else
const CoreDesc *const core_table[] = { &atari800_desc, &nes_desc, &apple2_desc, &c264_desc, &zxnext_desc,
                                       &portdiag_desc, 0 };
#define HEX_CORE c264_desc
#endif

// 128 KB behind a window of 64 KB: the page register picks the half (the model of zx_win.v / cpc: "b0: the 64 KB half of the RAM window")
struct PagedIo : FakeIo {
    std::vector<uint8_t> big;
    int page_reg = -1;
    int memonly_on = 0, memonly_off = 0;
    bool memonly_state = false;
    std::vector<int> pages_used;
    PagedIo() : big(0x20000, 0) { }
    uint32_t at(uint32_t a) { return (page_reg >= 0 ? (regs[page_reg] & 1) * 0x10000u : 0) + a; }
    void ram_write(uint32_t a, const void *p, uint32_t n) { if (page_reg >= 0) pages_used.push_back(regs[page_reg] & 1); memcpy(&big.at(at(a)), p, n); }
    void ram_read(uint32_t a, void *p, uint32_t n) { if (page_reg >= 0) pages_used.push_back(regs[page_reg] & 1); memcpy(p, &big.at(at(a)), n); }
    void ram_memonly(bool on) { memonly_state = on; (on ? memonly_on : memonly_off)++; }
};

struct Rig {
    PagedIo io; FakeFs fs; CoreCtx c;
    explicit Rig(const CoreDesc *d, int page_reg = -1)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        io.page_reg = page_reg;
    }
    ~Rig() { delete c.ops; }
    int call(const char *op, int addr, int len, OpResult *r)
    {
        JsonObj a;
        memset(&a, 0, sizeof(a));
        snprintf(a.key[0], JSON_KEY_MAX, "address"); a.v[0].t = J_NUM; a.v[0].n = addr;
        snprintf(a.key[1], JSON_KEY_MAX, "length"); a.v[1].t = J_NUM; a.v[1].n = len;
        a.n = 2;
        return svc_call(&c, op, &a, r);
    }
    int write(int addr, const char *hex, OpResult *r)
    {
        JsonObj a;
        memset(&a, 0, sizeof(a));
        snprintf(a.key[0], JSON_KEY_MAX, "address"); a.v[0].t = J_NUM; a.v[0].n = addr;
        snprintf(a.key[1], JSON_KEY_MAX, "data"); a.v[1].t = J_STR; a.v[1].s = hex;
        a.n = 2;
        return svc_call(&c, "machine.write_mem", &a, r);
    }
};

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    // every core has the pair, once, owned by its own contributor, and the descriptor still passes its check
    for (const CoreDesc *const *p = core_table; *p; p++) {
        const CoreDesc *d = *p;
        CHECK(!desc_check(d));
        Rig r(d);
        OpRegistry *reg = ops_of(&r.c);
        const OpDef *rd = reg->find_op("machine.read_mem"), *wr = reg->find_op("machine.write_mem");
        CHECK(rd && wr);
        if (!rd || !wr) continue;
        CHECK_EQ(rd->nargs, 2); CHECK_EQ(wr->nargs, 2);
        CHECK(strcmp(rd->owner, "base") != 0 && !strcmp(rd->owner, wr->owner));
        CHECK_EQ(rd->args[1].hi, OP_DATA_MAX);
    }
    // the address width of each core (the last address the pair takes)
#ifdef TEST_APP_VC20
    struct W { const CoreDesc *d; int last; } widths[] = { { &vc20_desc, 0xFFFF } };
#elif defined(TEST_APP_A1200)
    struct W { const CoreDesc *d; int last; } widths[] = { { &amiga1200_desc, 0xFFFFFF } };
#elif defined(TEST_APP_ATARI800)
    struct W { const CoreDesc *d; int last; } widths[] = { { &atari800_desc, 0xFFFF } };
#elif defined(TEST_APP_NES)
    struct W { const CoreDesc *d; int last; } widths[] = { { &nes_desc, 0xFFFF } };
#elif defined(TEST_APP_C264)
    struct W { const CoreDesc *d; int last; } widths[] = { { &c264_desc, 0xFFFF } };
#elif defined(TEST_APP_ZXNEXT)
    struct W { const CoreDesc *d; int last; } widths[] = { { &zxnext_desc, 0x1FFFFF } };
#elif defined(TEST_APP_NEOGEO)
    struct W { const CoreDesc *d; int last; } widths[] = { { &neogeo_desc, 0xFFFFFF } };
#elif defined(TEST_APP_AMIGA)
    struct W { const CoreDesc *d; int last; } widths[] = { { &amiga_desc, 0xFFFFFF } };
#elif defined(TEST_APP_DDT)
    struct W { const CoreDesc *d; int last; } widths[] = { { &ddt_novic_desc, 0xFFFF } };
#elif defined(TEST_APP_ZX)
    struct W { const CoreDesc *d; int last; } widths[] = { { &zx_desc, 0x1FFFF } };
#elif defined(TEST_APP_CPC)
    struct W { const CoreDesc *d; int last; } widths[] = { { &cpc_desc, 0x1FFFF } };
#else
    struct W { const CoreDesc *d; int last; } widths[] = { { &atari800_desc, 0xFFFF }, { &nes_desc, 0xFFFF }, { &apple2_desc, 0x1FFFF },
        { &c264_desc, 0xFFFF }, { &zxnext_desc, 0x1FFFFF } };       // T239.5 D13: the Amiga 500's width is its unit amiga_memops
#endif
    for (auto &w : widths) {
        Rig r(w.d);
        const OpDef *rd = ops_of(&r.c)->find_op("machine.read_mem");
        CHECK(rd != 0);
        if (rd) CHECK_EQ(rd->args[0].hi, w.last);
    }

    OpResult res;
    // ---- the C64 family: the 64 KB window, RAM only (C64_DMA_MEMONLY on for the access and off again)
#if !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_ATARI800) && !defined(TEST_APP_NES) && !defined(TEST_APP_ZXNEXT) && !defined(TEST_APP_NEOGEO) && !defined(TEST_APP_AMIGA)   // the A1200's, DDT's ... memory is not reachable by the application: it has no window to read or write (its case is the one of the unreachable cores below)
#ifdef TEST_APP_VC20
    for (const CoreDesc *d : { &vc20_desc }) {
#else
    for (const CoreDesc *d : { &c264_desc }) {
#endif
        Rig r(d);
        CHECK_EQ(r.write(0x0400, "deadbeef", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x400], 0xDE); CHECK_EQ(r.io.big[0x403], 0xEF);
        CHECK_EQ(r.io.memonly_on, 1); CHECK_EQ(r.io.memonly_off, 1); CHECK(!r.io.memonly_state);
        CHECK_EQ(r.call("machine.read_mem", 0x0400, 4, &res), OPE_OK);
        CHECK_EQ(res.ndata, 4); CHECK(res.data[0] == 0xDE && res.data[3] == 0xEF);
        CHECK(strstr(res.msg, "4 bytes at $400") != 0);
        CHECK_EQ(r.io.memonly_on, 2);
        // the last byte of the space, and one past it
        CHECK_EQ(r.write(0xFFFF, "55", &res), OPE_OK);
        CHECK_EQ(r.io.big[0xFFFF], 0x55);
        CHECK_EQ(r.call("machine.read_mem", 0xFFFF, 2, &res), OPE_ARG);          // crosses the end: it does not wrap
        CHECK(strstr(res.msg, "cross the end of") != 0);
        CHECK_EQ(r.call("machine.read_mem", 0x10000, 1, &res), OPE_ARG);         // outside the argument's range: the registry refuses
        CHECK_EQ(res.ndata, 0);
        CHECK(!r.io.overrun);
    }
#endif
#if !defined(TEST_APP_VC20) && !defined(TEST_APP_A1200) && !defined(TEST_APP_DDT) && !defined(TEST_APP_ZX) && !defined(TEST_APP_CPC) && !defined(TEST_APP_NES) && !defined(TEST_APP_C264) && !defined(TEST_APP_ZXNEXT) && !defined(TEST_APP_NEOGEO) && !defined(TEST_APP_AMIGA)          // the cases that stand on the shared app's cores
    // ---- the Atari: the same window, no ram-only bit
    {
        Rig r(&atari800_desc);
        CHECK_EQ(r.write(0x600, "a9", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x600], 0xA9); CHECK_EQ(r.io.memonly_on, 0);
    }
#ifndef TEST_APP_ATARI800                 // the other cores of the shared app
    // ---- the Apple //e: main RAM in the window, aux RAM in the DDR2 window at A2_W_AUX
    {
        Rig r(&apple2_desc);
        CHECK_EQ(r.write(0x0300, "0102", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x300], 1);
        CHECK_EQ(r.write(0x10010, "aabb", &res), OPE_OK);
        CHECK_EQ(r.io.ddr[A2_W_AUX + 0x10], 0xAA); CHECK_EQ(r.io.ddr[A2_W_AUX + 0x11], 0xBB);
        CHECK_EQ(r.io.big[0x10], 0);
        CHECK_EQ(r.call("machine.read_mem", 0x10010, 2, &res), OPE_OK);
        CHECK(res.data[0] == 0xAA && res.data[1] == 0xBB);
        CHECK_EQ(r.call("machine.read_mem", 0xFFF0, 0x20, &res), OPE_ARG);       // main RAM ends at $FFFF, the aux region is another one
    }
    // ---- the ZX Spectrum Next: 2 MB in the DDR2 window
    {
        Rig r(&zxnext_desc);
        CHECK_EQ(r.write(0x1FFFFE, "7788", &res), OPE_OK);
        CHECK_EQ(r.io.ddr[ZXN_W_RAM + 0x1FFFFE], 0x77);
        CHECK_EQ(r.call("machine.read_mem", 0x1FFFFE, 2, &res), OPE_OK);
        CHECK(res.data[0] == 0x77 && res.data[1] == 0x88);
        CHECK_EQ(r.call("machine.read_mem", 0x1FFFFF, 2, &res), OPE_ARG);
    }
    // ---- cores whose memory the application cannot reach say so, and why
    for (const CoreDesc *d : { &nes_desc, &portdiag_desc }) {               // T239.5 D13: the Amiga 500's case is its unit amiga_memops
        Rig r(d);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
    {
        Rig r(&nes_desc);
        OpResult x;
        r.call("machine.read_mem", 0, 1, &x);
        CHECK(strstr(x.msg, "NES") != 0);                                       // the Amiga 500's message names it in its unit amiga_memops
    }
#endif
#ifdef TEST_APP_ZX
    // ---- the ZX Spectrum: 128 KB through a window of 64 KB, the page register put back to 0
    {
        const CoreDesc *d = &zx_desc;
        int reg = ZX_WIN_PAGE;
        Rig r(d, reg);
        CHECK_EQ(r.write(0x1F000, "c0de", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x1F000], 0xC0); CHECK_EQ(r.io.big[0x1F001], 0xDE);
        CHECK_EQ(r.io.regs[reg], 0);                                              // back on the first half
        CHECK_EQ(r.write(0x00010, "11", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x10], 0x11);
        CHECK_EQ(r.call("machine.read_mem", 0x1F000, 2, &res), OPE_OK);
        CHECK(res.data[0] == 0xC0 && res.data[1] == 0xDE);
        // an access across the 64 KB boundary of the window is split into two accesses, one per half
        r.io.pages_used.clear();
        CHECK_EQ(r.write(0xFFFE, "01020304", &res), OPE_OK);
        CHECK_EQ(r.io.big[0xFFFE], 1); CHECK_EQ(r.io.big[0xFFFF], 2); CHECK_EQ(r.io.big[0x10000], 3); CHECK_EQ(r.io.big[0x10001], 4);
        CHECK(r.io.pages_used.size() == 2 && r.io.pages_used[0] == 0 && r.io.pages_used[1] == 1);
        CHECK_EQ(r.io.regs[reg], 0);
        CHECK_EQ(r.call("machine.read_mem", 0x1FFFF, 2, &res), OPE_ARG);
    }
#endif
#ifdef TEST_APP_CPC
    // ---- the CPC: 128 KB through a window of 64 KB, the page register put back to 0
    {
        const CoreDesc *d = &cpc_desc;
        int reg = CPC_WIN_PAGE;
        Rig r(d, reg);
        CHECK_EQ(r.write(0x1F000, "c0de", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x1F000], 0xC0); CHECK_EQ(r.io.big[0x1F001], 0xDE);
        CHECK_EQ(r.io.regs[reg], 0);                                              // back on the first half
        CHECK_EQ(r.write(0x00010, "11", &res), OPE_OK);
        CHECK_EQ(r.io.big[0x10], 0x11);
        CHECK_EQ(r.call("machine.read_mem", 0x1F000, 2, &res), OPE_OK);
        CHECK(res.data[0] == 0xC0 && res.data[1] == 0xDE);
        // an access across the 64 KB boundary of the window is split into two accesses, one per half
        r.io.pages_used.clear();
        CHECK_EQ(r.write(0xFFFE, "01020304", &res), OPE_OK);
        CHECK_EQ(r.io.big[0xFFFE], 1); CHECK_EQ(r.io.big[0xFFFF], 2); CHECK_EQ(r.io.big[0x10000], 3); CHECK_EQ(r.io.big[0x10001], 4);
        CHECK(r.io.pages_used.size() == 2 && r.io.pages_used[0] == 0 && r.io.pages_used[1] == 1);
        CHECK_EQ(r.io.regs[reg], 0);
        CHECK_EQ(r.call("machine.read_mem", 0x1FFFF, 2, &res), OPE_ARG);
    }
#endif
#endif
#ifdef TEST_APP_ZXNEXT
    // ---- the ZX Spectrum Next: 2 MB in the DDR2 window, and the hex text of a write
    {
        Rig r(&zxnext_desc);
        CHECK_EQ(r.write(0x1FFFFE, "7788", &res), OPE_OK);
        CHECK_EQ(r.io.ddr[ZXN_W_RAM + 0x1FFFFE], 0x77);
        CHECK_EQ(r.call("machine.read_mem", 0x1FFFFE, 2, &res), OPE_OK);
        CHECK(res.data[0] == 0x77 && res.data[1] == 0x88);
        CHECK_EQ(r.call("machine.read_mem", 0x1FFFFF, 2, &res), OPE_ARG);
        CHECK_EQ(r.write(0, "", &res), OPE_ARG);
        CHECK_EQ(r.write(0, "abc", &res), OPE_ARG);                              // odd
        CHECK_EQ(r.write(0, "zz", &res), OPE_ARG);
        CHECK_EQ(r.write(0, "0g", &res), OPE_ARG);
        CHECK_EQ(r.write(0, std::string(256, 'a').c_str(), &res), OPE_ARG);      // 128 bytes
        std::string ok(254, 'B');
        CHECK_EQ(r.write(0x1000, ok.c_str(), &res), OPE_OK);                     // 127 bytes, upper case
        CHECK_EQ(r.io.ddr[ZXN_W_RAM + 0x1000], 0xBB); CHECK_EQ(r.io.ddr[ZXN_W_RAM + 0x1000 + 126], 0xBB); CHECK_EQ(r.io.ddr[ZXN_W_RAM + 0x1000 + 127], 0);
        CHECK_EQ(r.call("machine.read_mem", 0, 257, &res), OPE_ARG);             // more than OP_DATA_MAX: the registry refuses
        CHECK_EQ(r.call("machine.read_mem", 0, 256, &res), OPE_OK);
        CHECK_EQ(res.ndata, 256);
    }
#elif defined(TEST_APP_NEOGEO)
    // ---- the Neo Geo's memory is not reachable by the application (the machine's RAM is inside the core, the ROM space is the application's own staging): it says so, and why
    {
        Rig r(&neogeo_desc);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
#elif defined(TEST_APP_AMIGA)
    // ---- the Amiga 500's memory is not reachable by the application: it says so, and why
    {
        Rig r(&amiga_desc);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0 && strstr(res.msg, "Amiga") != 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
#elif defined(TEST_APP_NES)
    // ---- the NES's memory is not reachable by the application: it says so, and why
    {
        Rig r(&nes_desc);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0 && strstr(res.msg, "NES") != 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
#elif defined(TEST_APP_A1200)
    // ---- the A1200's memory is not reachable by the application: it says so, and why
    {
        Rig r(&amiga1200_desc);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0 && strstr(res.msg, "Amiga") != 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
#elif defined(TEST_APP_DDT)
    // ---- DDT's picture is computed, not kept in a window: the memory is not reachable by the application, it says so, and why
    {
        Rig r(&ddt_novic_desc);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0 && strstr(res.msg, "DDT") != 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
#else
    // ---- the hex text of a write (the 64 KB C64-family core of the app)
    {
        Rig r(&HEX_CORE);
        CHECK_EQ(r.write(0, "", &res), OPE_ARG);
        CHECK_EQ(r.write(0, "abc", &res), OPE_ARG);                              // odd
        CHECK_EQ(r.write(0, "zz", &res), OPE_ARG);
        CHECK_EQ(r.write(0, "0g", &res), OPE_ARG);
        CHECK_EQ(r.write(0, std::string(256, 'a').c_str(), &res), OPE_ARG);      // 128 bytes
        std::string ok(254, 'B');
        CHECK_EQ(r.write(0x1000, ok.c_str(), &res), OPE_OK);                     // 127 bytes, upper case
        CHECK_EQ(r.io.big[0x1000], 0xBB); CHECK_EQ(r.io.big[0x1000 + 126], 0xBB); CHECK_EQ(r.io.big[0x1000 + 127], 0);
        CHECK_EQ(r.call("machine.read_mem", 0, 257, &res), OPE_ARG);             // more than OP_DATA_MAX: the registry refuses
        CHECK_EQ(r.call("machine.read_mem", 0, 256, &res), OPE_OK);
        CHECK_EQ(res.ndata, 256);
    }
#endif
    DONE("test_memops");
}
