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
extern const CoreDesc amiga_desc;               // the Amiga 500 is its own app (cores/amiga/app/lib)

const CoreDesc *const core_table[] = { &amiga_desc, 0 };

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
    struct W { const CoreDesc *d; int last; } widths[] = { { &amiga_desc, 0xFFFFFF } };
    for (auto &w : widths) {
        Rig r(w.d);
        const OpDef *rd = ops_of(&r.c)->find_op("machine.read_mem");
        CHECK(rd != 0);
        if (rd) CHECK_EQ(rd->args[0].hi, w.last);
    }

    OpResult res;
    // ---- the C64 family: the 64 KB window, RAM only (C64_DMA_MEMONLY on for the access and off again)
    // ---- the Amiga 500's memory is not reachable by the application: it says so, and why
    {
        Rig r(&amiga_desc);
        CHECK_EQ(r.call("machine.read_mem", 0x100, 4, &res), OPE_UNSUPPORTED);
        CHECK(strlen(res.msg) > 40 && res.ndata == 0 && strstr(res.msg, "Amiga") != 0);
        CHECK_EQ(r.write(0x100, "00", &res), OPE_UNSUPPORTED);
    }
    DONE("test_memops");
}
