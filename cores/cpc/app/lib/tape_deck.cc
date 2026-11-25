// Copy of coreapp/cores/tape_deck.cc at commit 2fd95fc63 (T239.5 D16: the original stays for the cores that have not moved; this copy is the CPC's own).
#include "tape_deck.h"
#include <stdio.h>
#include <string.h>

namespace {

class DdrTape : public TapeOut {
public:
    DdrTape(CoreIo *io, uint32_t off) : io_(io), off_(off) { }
    bool put(const uint16_t *w, uint32_t n)
    {
        io_->ddr_write(off_, w, n * 2);
        off_ += n * 2;
        return true;
    }
private:
    CoreIo *io_;
    uint32_t off_;
};

void seek(CoreCtx *c, const TapeRegs &r, uint32_t word)
{
    for (int k = 0; k < 4; k++) {
        c->io->wr(r.pos + k, (uint8_t)(word >> (8 * k)));
    }
    c->io->wr(r.seek, 1);
}

uint32_t position(CoreCtx *c, const TapeRegs &r)
{
    return c->io->rd(r.pos) | (uint32_t)c->io->rd(r.pos + 1) << 8 | (uint32_t)c->io->rd(r.pos + 2) << 16 |
           (uint32_t)(c->io->rd(r.pos + 3) & 1) << 24;
}

}  // namespace

void deck_stop(CoreCtx *c, TapeDeck *d, const TapeRegs &r)
{
    c->io->wr(r.ctrl, 0);
    d->playing = false;
}

int deck_play(CoreCtx *c, TapeDeck *d, const TapeRegs &r, char *msg, int msglen)
{
    if (!d->loaded) {
        snprintf(msg, msglen, "no tape is inserted");
        return 1;
    }
    c->io->wr(r.ctrl, 0);                         // the engine starts on a rising edge
    c->io->wr(r.ctrl, 1);
    d->playing = true;
    return 0;
}

int deck_insert(CoreCtx *c, TapeDeck *d, const TapeRegs &r, const char *path, bool play, bool stop48, char *msg, int msglen)
{
    TapeFormat fmt;
    if (!tape_format_of(path, &fmt)) {
        snprintf(msg, msglen, "%s is not a tape (.tap, .tzx, .cdt)", path);
        return 1;
    }
    if (r.c64tap) {
        if (fmt != TAPE_TAP) {
            snprintf(msg, msglen, "%s is not a tape (.tap)", path);
            return 1;
        }
        fmt = TAPE_C64;
    }
    CoreFile *f = c->fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    deck_stop(c, d, r);
    bool had = d->loaded;
    d->loaded = false;
    d->name[0] = 0;
    d->path[0] = 0;
    uint32_t base = 0x01000000u + r.win_off;
    for (int k = 0; k < 4; k++) {
        c->io->wr(r.base + k, (uint8_t)(base >> (8 * k)));
    }
    DdrTape out(c->io, r.win_off);
    int rc = tape_convert(f, fmt, stop48, &out, &d->idx, msg, msglen);
    c->fs->close(f);
    if (rc) {
        if (had) {
            mount_notify(c, r.slot);              // the old tape is out and the deck is empty
        }
        return 1;
    }
    seek(c, r, 0);
    d->loaded = true;
    const char *slash = strrchr(path, '/');
    fs_short_name(d->name, sizeof(d->name), slash ? slash + 1 : path);
    snprintf(d->path, sizeof(d->path), "%s", path);
    mount_notify(c, r.slot);                      // T180 D2
    printf("[tape] %s: %u words, %u blocks%s\n", d->name, (unsigned)d->idx.words, (unsigned)d->idx.nblocks, d->idx.truncated ? " (index cut)" : "");
    return play ? deck_play(c, d, r, msg, msglen) : 0;
}

void deck_rewind(CoreCtx *c, TapeDeck *d, const TapeRegs &r)
{
    deck_stop(c, d, r);
    seek(c, r, 0);
}

int deck_block_step(CoreCtx *c, TapeDeck *d, const TapeRegs &r, int dir, char *msg, int msglen)
{
    if (!d->loaded || !d->idx.nblocks) {
        snprintf(msg, msglen, "no tape is inserted");
        return 1;
    }
    uint32_t pos = position(c, r);
    uint32_t cur = tape_block_at(&d->idx, pos);
    int target = (int)cur;
    if (dir > 0) {
        target = (int)cur + 1;
    } else if (pos == d->idx.start[cur] && cur > 0) {
        target = (int)cur - 1;
    }
    if (target >= (int)d->idx.nblocks) {
        snprintf(msg, msglen, "this is the last block");
        return 1;
    }
    deck_stop(c, d, r);
    seek(c, r, d->idx.start[target]);
    return 0;
}

void deck_eject(CoreCtx *c, TapeDeck *d, const TapeRegs &r)
{
    deck_stop(c, d, r);
    d->loaded = false; d->name[0] = 0; d->path[0] = 0;
    mount_notify(c, r.slot);                      // T180 D2
}

void deck_service(CoreCtx *c, TapeDeck *d, const TapeRegs &r)
{
    if (d->playing) {
        uint8_t ts = c->io->rd(r.status);
        if (ts & (TAPE_ST_END | TAPE_ST_STOP)) {
            deck_stop(c, d, r);
        }
    }
}

const char *deck_image(const TapeDeck *d)
{
    return d->loaded ? d->name : 0;
}
