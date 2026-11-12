#include "memops.h"
#include <stdio.h>
#include <string.h>

static const MemRegion *region_of(const MemMap &m, uint32_t addr)
{
    for (int i = 0; i < m.nregions; i++) {
        if (addr >= m.regions[i].first && addr - m.regions[i].first < m.regions[i].size) {
            return &m.regions[i];
        }
    }
    return 0;
}

// Checks that [addr, addr+n) lies in one region; the sentence otherwise.
static int locate(const MemMap &m, uint32_t addr, uint32_t n, const MemRegion **rg, OpOut &o)
{
    if (!m.nregions) {
        snprintf(o.msg, o.msglen, "%s", m.none ? m.none : "This core's memory is not reachable by the application");
        return OPE_UNSUPPORTED;
    }
    const MemRegion *r = region_of(m, addr);
    if (!r) {
        snprintf(o.msg, o.msglen, "$%X is outside this core's memory (%s: $%X .. $%X)", (unsigned)addr, m.regions[0].name, (unsigned)m.regions[0].first,
                 (unsigned)(m.regions[0].first + m.regions[0].size - 1));
        return OPE_ARG;
    }
    if (n > r->size - (addr - r->first)) {
        snprintf(o.msg, o.msglen, "%u bytes at $%X cross the end of %s ($%X)", (unsigned)n, (unsigned)addr, r->name, (unsigned)(r->first + r->size - 1));
        return OPE_ARG;
    }
    *rg = r;
    return OPE_OK;
}

// One access in the region: reads (rd) or writes (!rd) n bytes at `addr`.
static void access(const MemMap &m, CoreCtx *c, const MemRegion &r, uint32_t addr, uint8_t *buf, uint32_t n, bool rd)
{
    uint32_t off = addr - r.first;
    CoreIo *io = c->io;
    if (r.kind == MK_DDR) {
        if (rd) {
            io->ddr_read(r.base + off, buf, n);
        } else {
            io->ddr_write(r.base + off, buf, n);
        }
        return;
    }
    if (m.ram_only) {
        io->ram_memonly(true);
    }
    if (r.kind == MK_RAMPAGE) {
        // the window shows 64 KB at a time; a transfer that would run across the page boundary is split
        while (n) {
            uint32_t page = off >> 16, in = off & 0xFFFF, take = 0x10000 - in < n ? 0x10000 - in : n;
            io->wr(r.reg, (uint8_t)page);
            if (rd) {
                io->ram_read(in, buf, take);
            } else {
                io->ram_write(in, buf, take);
            }
            off += take;
            buf += take;
            n -= take;
        }
        io->wr(r.reg, 0);
    } else if (rd) {
        io->ram_read(off, buf, n);
    } else {
        io->ram_write(off, buf, n);
    }
    if (m.ram_only) {
        io->ram_memonly(false);
    }
}

int mem_read(const MemMap &m, CoreCtx *c, const OpArgs &a, OpOut &o)
{
    uint32_t addr = (uint32_t)a.i(0), n = (uint32_t)a.i(1);
    const MemRegion *r = 0;
    int rc = locate(m, addr, n, &r, o);
    if (rc) {
        return rc;
    }
    if (n > (uint32_t)o.datacap) {
        snprintf(o.msg, o.msglen, "at most %d bytes at a time", o.datacap);
        return OPE_ARG;
    }
    access(m, c, *r, addr, o.data, n, true);
    o.ndata = (int)n;
    snprintf(o.msg, o.msglen, "%u bytes at $%X (%s)", (unsigned)n, (unsigned)addr, r->name);
    return OPE_OK;
}

static int hexv(char ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
}

int mem_write(const MemMap &m, CoreCtx *c, const OpArgs &a, OpOut &o)
{
    uint32_t addr = (uint32_t)a.i(0);
    const char *hex = a.s(1);
    size_t len = strlen(hex);
    if (len == 0 || (len & 1) || len / 2 > 127) {
        snprintf(o.msg, o.msglen, "data is hex text: two digits a byte, 1 to 127 bytes");
        return OPE_ARG;
    }
    uint8_t buf[127];
    for (size_t i = 0; i < len / 2; i++) {
        int h = hexv(hex[2 * i]), l = hexv(hex[2 * i + 1]);
        if (h < 0 || l < 0) {
            snprintf(o.msg, o.msglen, "data has a character that is not a hex digit");
            return OPE_ARG;
        }
        buf[i] = (uint8_t)(h * 16 + l);
    }
    const MemRegion *r = 0;
    int rc = locate(m, addr, (uint32_t)(len / 2), &r, o);
    if (rc) {
        return rc;
    }
    access(m, c, *r, addr, buf, (uint32_t)(len / 2), false);
    snprintf(o.msg, o.msglen, "%u bytes written at $%X (%s)", (unsigned)(len / 2), (unsigned)addr, r->name);
    return OPE_OK;
}
