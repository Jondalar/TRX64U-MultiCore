#include "fmt.h"
#include "mbr.h"
#include "mediasniff.h"
#include "partition.h"
#include "chanfat_manager.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

extern "C" uint32_t get_fattime(void);

// ---- geometry: the arithmetic of f_mkfs (ff.c, exFAT branch) for a volume that starts on the alignment ----------------------------------
// The up-case table f_mkfs writes is compressed; its size is found by the same loop (without the writes).
static uint32_t upcase_bytes(void)
{
    static uint32_t cached;
    if (cached) {
        return cached;
    }
    uint32_t szb = 0;
    UINT st = 0, j = 0;
    WCHAR si = 0, ch;
    do {
        switch (st) {
        case 0:
            ch = (WCHAR)ff_wtoupper(si);
            if (ch != si) {
                si++;
                break;
            }
            for (j = 1; (WCHAR)(si + j) && (WCHAR)(si + j) == ff_wtoupper((WCHAR)(si + j)); j++) ;
            if (j >= 128) {
                ch = 0xFFFF;
                st = 2;
                break;
            }
            st = 1;
            // fall through
        case 1:
            ch = si++;
            if (--j == 0) {
                st = 0;
            }
            break;
        default:
            ch = (WCHAR)j;
            si += (WCHAR)j;
            st = 0;
        }
        szb += 2;
    } while (si);
    cached = szb;
    return szb;
}

void fmt_geometry(uint32_t sectors, FmtGeom *g)
{
    memset(g, 0, sizeof(*g));
    g->sectors = sectors;
    g->part_start = MBR_PART_START;
    g->part_len = sectors > MBR_PART_START ? sectors - MBR_PART_START : 0;
    uint32_t v = g->part_len;
    uint32_t au = 8;
    if (v >= 0x80000) au = 64;
    if (v >= 0x4000000) au = 256;
    g->au = au;
    g->cluster_bytes = au * 512;
    uint32_t sz_fat = (uint32_t)(((uint64_t)(v / au + 2) * 4 + 511) / 512);
    uint32_t b_data = (32 + sz_fat + MBR_PART_START - 1) & ~(MBR_PART_START - 1);
    g->clusters = v > b_data ? (v - b_data) / au : 0;
    uint32_t szb_bit = (g->clusters + 7) / 8;
    uint32_t t_bit = (szb_bit + au * 512 - 1) / (au * 512);
    uint32_t szb_case = upcase_bytes();
    uint32_t t_case = (szb_case + au * 512 - 1) / (au * 512);
    g->sys_clusters = t_bit + t_case + 1;
    uint32_t sec = 1 /* sector 0 cleared */ + (szb_case + 511) / 512 + (szb_bit + 511) / 512 + sz_fat + au + 24 /* two boot regions */ + 1 /* the table */;
    g->est_bytes = sec * 512;
}

uint32_t fmt_work_size(uint32_t free_heap)
{
    uint32_t w = free_heap / 4;
    if (w > 65536) {
        w = 65536;
    }
    w &= ~511u;
    return w < 4096 ? 0 : w;
}

const char *fmt_check(BlockDevice *dev, uint32_t work, FmtGeom *g)
{
    if (!dev || dev->get_state() != e_device_ready) {
        return "no medium";
    }
    DSTATUS st = dev->status();
    if (st & STA_NODISK) {
        return "no medium";
    }
    if (st & STA_PROTECT) {
        return "write protected";
    }
    if (st & STA_NOINIT) {
        return "not ready";
    }
    uint32_t ss = 0, n = 0;
    if (dev->ioctl(GET_SECTOR_SIZE, &ss) != RES_OK) {
        return "cannot read the sector size";
    }
    if (ss != 512) {
        return "sectors are not 512 bytes";
    }
    if (dev->ioctl(GET_SECTOR_COUNT, &n) != RES_OK) {
        return "cannot read the size";
    }
    if (n == 0) {
        return "no medium";
    }
    if (n < 131072) {
        return "smaller than 64 MiB";
    }
    if (n > 0xFFFFFFFFu - MBR_PART_START) {
        return "larger than 2 TiB";
    }
    if (!work) {
        return "not enough memory";
    }
    fmt_geometry(n, g);
    if (g->clusters < 16) {
        return "too small for exFAT";
    }
    return 0;
}

// ---- the shim every write of the format goes through ------------------------------------------------------------------------------------------
class FmtDevice : public BlockDevice
{
    BlockDevice *inner;
    FmtProgressCb cb;
    void *cbctx;
    uint32_t total;
public:
    uint32_t written;                 // bytes written through the shim
    uint32_t since;                   // bytes since the last report
    bool cancelled;
    bool cancelable;                  // false once the partition table is written
    int step;

    FmtDevice(BlockDevice *d, FmtProgressCb c, void *x, uint32_t tot) : inner(d), cb(c), cbctx(x), total(tot), written(0), since(0), cancelled(false), cancelable(true), step(FST_CLEAR) { }
    void report(void)
    {
        since = 0;
        if (cb && cb(cbctx, step, written, total) && cancelable) {
            cancelled = true;
        }
    }
    DSTATUS init(void) { return inner->status(); }
    DSTATUS status(void) { return inner->status(); }
    DRESULT read(uint8_t *b, uint32_t s, int n) { return inner->read(b, s, n); }
    DRESULT write(const uint8_t *b, uint32_t s, int n)
    {
        if (cancelled) {
            return RES_ERROR;
        }
        DRESULT r = inner->write(b, s, n);
        if (r != RES_OK) {
            return r;
        }
        written += (uint32_t)n * 512;
        since += (uint32_t)n * 512;
        if (since >= FMT_REPORT_BYTES) {
            report();
        }
        return RES_OK;
    }
    DRESULT ioctl(uint8_t c, void *d) { return inner->ioctl(c, d); }
};

struct SniffCtx { BlockDevice *dev; };
static bool sniff_read(void *ctx, uint32_t lba, uint8_t *buf)
{
    return ((SniffCtx *)ctx)->dev->read(buf, lba, 1) == RES_OK;
}

const char *fmt_verify_layout(BlockDevice *dev, const FmtGeom *g)
{
    SniffCtx sc = { dev };
    MediaSniff s;
    media_sniff(sniff_read, &sc, g->sectors, 512, &s);
    if (s.type == MT_ERROR) {
        return "the medium cannot be read back";
    }
    if (s.type != MT_EXFAT) {
        return "what was written is not read back as exFAT";
    }
    if (s.parts != 1 || !s.mbr || s.part_start != g->part_start || s.part_len != g->part_len) {
        return "the partition table read back is not the one that was written";
    }
    if (s.cluster_bytes != g->cluster_bytes) {
        return "the cluster size read back is not the expected one";
    }
    return 0;
}

static const char *fr_text(FRESULT r)
{
    switch (r) {
    case FR_DISK_ERR: return "write error";
    case FR_NOT_READY: return "not ready";
    case FR_WRITE_PROTECTED: return "write protected";
    case FR_MKFS_ABORTED: return "FatFs refused the geometry";
    case FR_NOT_ENOUGH_CORE: return "not enough memory";
    case FR_INVALID_PARAMETER: return "invalid parameter";
    default: return "FatFs error";
    }
}

static void say(char *why, int n, const char *a, const char *b = "")
{
    if (why && n > 0) {
        snprintf(why, n, "%s%s", a, b);
    }
}

FmtResult fmt_exfat(BlockDevice *dev, const FmtOps *ops, uint8_t *work, uint32_t worklen, FmtProgressCb cb, void *cbctx, char *why, int whylen)
{
    say(why, whylen, "");
    FmtGeom g;
    const char *refuse = fmt_check(dev, work ? worklen : 0, &g);
    if (refuse) {
        say(why, whylen, refuse);
        return FMT_REFUSED;
    }
    FmtDevice sh(dev, cb, cbctx, g.est_bytes);
    FmtResult res = FMT_FAILED;
    char vwhy[96] = "";

    sh.step = FST_RELEASE;
    sh.report();
    if (sh.cancelled) {
        say(why, whylen, "cancelled before the first write; the medium is as it was");
        ops->attach(ops->ctx, &g, false, vwhy, sizeof(vwhy));
        return FMT_CANCELLED;
    }
    if (!ops->detach(ops->ctx)) {
        say(why, whylen, "the volume could not be released; nothing was written");
        ops->attach(ops->ctx, &g, false, vwhy, sizeof(vwhy));
        return FMT_FAILED;
    }

    do {
        // (3) the old partition table is dead from here on
        sh.step = FST_CLEAR;
        static uint8_t zero[512];
        memset(zero, 0, sizeof(zero));
#ifdef FMT_MBR_FIRST
        // bench control only (-DFMT_MBR_FIRST): the table is written before the file system, over the old one
        static uint8_t mbr0[512];
        mbr_build(mbr0, g.sectors, get_fattime());
        if (sh.write(mbr0, 0, 1) != RES_OK) {
            say(why, whylen, sh.cancelled ? "cancelled" : "write error at the partition table");
            res = sh.cancelled ? FMT_CANCELLED : FMT_FAILED;
            break;
        }
#else
        if (sh.write(zero, 0, 1) != RES_OK) {
            say(why, whylen, sh.cancelled ? "cancelled" : "write error at sector 0");
            res = sh.cancelled ? FMT_CANCELLED : FMT_FAILED;
            break;
        }
#endif
        // (4) the file system, on a partition of ours that starts where the table will say
        sh.step = FST_WRITE;
        sh.report();
        if (sh.cancelled) {
            say(why, whylen, "cancelled; the medium is blank");
            res = FMT_CANCELLED;
            break;
        }
        Partition part(&sh, g.part_start, g.part_len, MBR_TYPE_EXFAT);
        uint8_t drv = ChanFATManager::getManager()->addDrive(&part);
        if (drv == 0xFF) {
            say(why, whylen, "no free drive slot for the format");
            break;
        }
        char path[4];
        snprintf(path, sizeof(path), "%d:", drv);
        MKFS_PARM parm;
        memset(&parm, 0, sizeof(parm));
        parm.fmt = FM_EXFAT | FM_SFD;
        parm.n_fat = 1;
        parm.align = MBR_PART_START;
        parm.n_root = 0;
        parm.au_size = 0;
        FRESULT fr = f_mkfs(path, &parm, work, worklen);
        ChanFATManager::getManager()->removeDrive(drv);
        if (fr != FR_OK) {
            if (sh.cancelled) {
                say(why, whylen, "cancelled; the medium is blank");
                res = FMT_CANCELLED;
            } else {
                say(why, whylen, "format failed: ", fr_text(fr));
            }
            break;
        }
        // (5) the partition table, last
        sh.step = FST_TABLE;
#ifndef FMT_MBR_FIRST
        {
            static uint8_t mbr[512];
            mbr_build(mbr, g.sectors, get_fattime());
            if (sh.write(mbr, 0, 1) != RES_OK) {
                say(why, whylen, sh.cancelled ? "cancelled; the medium is blank" : "write error at the partition table");
                res = sh.cancelled ? FMT_CANCELLED : FMT_FAILED;
                break;
            }
        }
#endif
        sh.cancelable = false;                       // the table is written: the volume is complete
        sh.step = FST_SYNC;
        sh.report();
        if (dev->ioctl(CTRL_SYNC, 0) != RES_OK) {
            say(why, whylen, "the medium did not accept the flush");
            break;
        }
        // (7) the host takes the medium up again and must see exactly this
        sh.step = FST_VERIFY;
        sh.report();
        const char *lay = fmt_verify_layout(dev, &g);
        if (lay) {
            say(why, whylen, lay);
            break;
        }
        if (!ops->attach(ops->ctx, &g, true, vwhy, sizeof(vwhy))) {
            say(why, whylen, vwhy[0] ? vwhy : "the host could not mount what was written");
            break;
        }
        res = FMT_OK;
    } while (0);
    if (res != FMT_OK) {
        ops->attach(ops->ctx, &g, false, vwhy, sizeof(vwhy));      // look again at whatever is on the medium now
    }
    return res;
}
