#include "update.h"
#include "ops.h"
#include "ctx.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {

struct Rec {
    uint32_t load, len, start;
    uint64_t at;                  // file offset of the data
};

const uint32_t CHUNK = 32768;

bool le32(CoreFile *f, uint64_t off, uint32_t *v)
{
    uint8_t b[4];
    if (!f->read(off, b, 4)) {
        return false;
    }
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

bool is_cfw(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && !strcasecmp(path + n - 4, ".cfw");
}

// The four words of trx_time_handover.h: the updater has no clock of its own on our bitstream. No valid time: all zero, so that nothing
// left over from an earlier run can pass for one.
void hand_time(CoreIo *io)
{
    uint32_t w[4] = { 0, 0, 0, 0 };
    uint8_t t[6];
    if (io->wall_time(t)) {
        uint32_t d = ((uint32_t)(2000 + t[0]) << 16) | ((uint32_t)t[1] << 8) | t[2];
        uint32_t tm = ((uint32_t)t[3] << 16) | ((uint32_t)t[4] << 8) | t[5];
        w[0] = 0x54585254u;                       // "TRXT"
        w[1] = d;
        w[2] = tm;
        w[3] = w[0] ^ d ^ tm ^ 0x5A17C0DEu;
    }
    io->ddr_write(UPD_TIME_ADDR - UPD_DDR_WINDOW, w, sizeof(w));
}

uint32_t sum32(const uint8_t *p, uint32_t n, uint32_t s)
{
    for (uint32_t i = 0; i < n; i++) {
        s = (s << 5) + (s >> 27) + p[i];
    }
    return s;
}

}  // namespace

int update_run_record(CoreIo *io, CoreFs *fs, bool monolith, const char *path, char *msg, int msglen)
{
    if (!monolith) {
        snprintf(msg, msglen, "Updates run from the host's menu on this board.");
        return 1;
    }
    if (!is_cfw(path)) {
        snprintf(msg, msglen, "%s is not a .cfw update file.", path);
        return 1;
    }
    CoreFile *f = fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "Cannot open %s.", path);
        return 1;
    }
    int rc = 1;
    uint8_t *buf = 0, *back = 0;
    uint8_t pre[36];
    Rec rec[UPD_RECORDS_MAX];
    int nrec = 0;
    uint32_t start = 0;
    uint64_t size = f->size(), off = 36;
    uint32_t fp = 0;

    if (size < 36 + 12 || !f->read(0, pre, 36) || !le32(f, 0, &fp) || fp != UPD_FINGERPRINT) {
        snprintf(msg, msglen, "Invalid Update Image");
        goto done;
    }
    // His format 1: a family or a product that is named has to be this board's
    if (pre[4] && strncmp((const char *)pre + 4, io->product_family(), 16) != 0) {
        snprintf(msg, msglen, "Wrong Product Family");
        goto done;
    }
    if (pre[20] && strncmp((const char *)pre + 20, io->product_name(), 16) != 0) {
        snprintf(msg, msglen, "Update for Wrong Product");
        goto done;
    }
    // First pass, nothing is written: every record inside the file and inside the updaters' window, a start address inside its record
    while (off < size) {
        if (nrec >= UPD_RECORDS_MAX) {
            snprintf(msg, msglen, "Update image: more than %d records.", UPD_RECORDS_MAX);
            goto done;
        }
        Rec &r = rec[nrec];
        if (size - off < 12 || !le32(f, off, &r.load) || !le32(f, off + 4, &r.len) || !le32(f, off + 8, &r.start)) {
            snprintf(msg, msglen, "Update image: a record header is cut off.");
            goto done;
        }
        r.at = off + 12;
        if (r.len > size - r.at) {
            snprintf(msg, msglen, "Update image: record %d is cut off (the file ends %u bytes early).", nrec, (unsigned)(r.len - (size - r.at)));
            goto done;
        }
        if (r.load < UPD_WIN_LO || r.load >= UPD_WIN_HI || r.len > UPD_WIN_HI - r.load) {
            snprintf(msg, msglen, "Update image: record %d lies at $%08X, outside $%08X-$%08X.", nrec, (unsigned)r.load, (unsigned)UPD_WIN_LO, (unsigned)UPD_WIN_HI);
            goto done;
        }
        if (r.start) {
            if (r.start < r.load || r.start >= r.load + r.len) {
                snprintf(msg, msglen, "Update image: the start $%08X is not inside its record.", (unsigned)r.start);
                goto done;
            }
            start = r.start;
        }
        off = r.at + r.len;
        nrec++;
    }
    if (!nrec || !start) {
        snprintf(msg, msglen, "Update image: no start address.");
        goto done;
    }
    buf = new uint8_t[CHUNK];
    back = new uint8_t[CHUNK];
    if (!buf || !back) {
        snprintf(msg, msglen, "Out of memory for the update (2 x %u bytes).", (unsigned)CHUNK);
        goto done;
    }
    // Second pass: into DDR2 in chunks, then read back and compare, so that a stage that does not hold never gets the jump
    for (int k = 0; k < nrec; k++) {
        uint32_t sent = 0, got = 0;
        for (uint32_t done_b = 0; done_b < rec[k].len;) {
            uint32_t n = rec[k].len - done_b < CHUNK ? rec[k].len - done_b : CHUNK;
            if (!f->read(rec[k].at + done_b, buf, n)) {
                snprintf(msg, msglen, "Cannot read %s at %u (the medium).", path, (unsigned)(rec[k].at + done_b));
                goto done;
            }
            io->ddr_write(rec[k].load + done_b - UPD_DDR_WINDOW, buf, n);
            sent = sum32(buf, n, sent);
            done_b += n;
        }
        for (uint32_t done_b = 0; done_b < rec[k].len;) {
            uint32_t n = rec[k].len - done_b < CHUNK ? rec[k].len - done_b : CHUNK;
            io->ddr_read(rec[k].load + done_b - UPD_DDR_WINDOW, back, n);
            got = sum32(back, n, got);
            done_b += n;
        }
        if (sent != got) {
            snprintf(msg, msglen, "The update did not stay in memory (record %d reads back different); not started.", k);
            goto done;
        }
    }
    hand_time(io);
    snprintf(msg, msglen, "Update started at $%08X.", (unsigned)start);
    fs->close(f);
    f = 0;
    delete[] buf;
    delete[] back;
    buf = back = 0;
    io->run_at(start);                    // the board does not come back from here
    return 0;
done:
    delete[] buf;
    delete[] back;
    if (f) {
        fs->close(f);
    }
    return rc;
}

static const ArgSpec path_arg[] = { ARG_PATH("path") };

static int op_run_record(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    bool mono = c->board && c->board->kind == BOARD_MONOLITH;
    return update_run_record(c->io, c->fs, mono, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

void update_register(OpRegistry &r)
{
    r.add_op("update.run_record", path_arg, 1, op_run_record);
}
