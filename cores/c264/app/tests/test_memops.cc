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
extern const CoreDesc c264_desc;                // the Commodore 264 is its own app (cores/c264/app/lib): no shared header declares it

const CoreDesc *const core_table[] = { &c264_desc, 0 };
#define HEX_CORE c264_desc

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
    struct W { const CoreDesc *d; int last; } widths[] = { { &c264_desc, 0xFFFF } };
    for (auto &w : widths) {
        Rig r(w.d);
        const OpDef *rd = ops_of(&r.c)->find_op("machine.read_mem");
        CHECK(rd != 0);
        if (rd) CHECK_EQ(rd->args[0].hi, w.last);
    }

    OpResult res;
    // ---- the C64 family: the 64 KB window, RAM only (C64_DMA_MEMONLY on for the access and off again)
    for (const CoreDesc *d : { &c264_desc }) {
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
#if   defined(HEX_CORE)                  // the shared app has no 64 KB C64-family core left: its apps run this case (vc20_memops, c264_memops, ...)
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
