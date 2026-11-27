// Copy of coreapp/cores/tape_pulses.cc at commit 2fd95fc63 (T239.5 D16: the original stays for the cores that have not moved; this copy is the CPC's own).
#include "tape_pulses.h"
#include <stdio.h>
#include <string.h>

namespace {

// The ROM loader's timings, in T-states (hardware fact: the Spectrum tape format; the TZX specification names the same numbers for block 0x10).
const uint32_t PILOT_T = 2168, SYNC1_T = 667, SYNC2_T = 735, BIT0_T = 855, BIT1_T = 1710;
const uint32_t PILOT_HEADER = 8063, PILOT_DATA = 3223;
const uint32_t MS_T = 3500;                // ticks in a millisecond

struct Conv {
    CoreFile *f;
    uint64_t size;
    uint64_t pos;                          // read cursor
    uint8_t rb[512];
    uint64_t rb_off;
    uint32_t rb_n;
    TapeOut *out;
    TapeIndex *idx;
    uint16_t wb[512];
    uint32_t nw;
    uint32_t total;                        // words made so far
    bool rd_fail, put_fail, too_long, short_pulse;

    // ---- reading ---------------------------------------------------------------------------------------------------------
    bool byte_at(uint64_t off, uint8_t *v)
    {
        if (off >= size) {
            return false;
        }
        if (rb_n == 0 || off < rb_off || off >= rb_off + rb_n) {
            uint32_t n = size - off < sizeof(rb) ? (uint32_t)(size - off) : (uint32_t)sizeof(rb);
            if (!f->read(off, rb, n)) {
                rd_fail = true;
                rb_n = 0;
                return false;
            }
            rb_off = off;
            rb_n = n;
        }
        *v = rb[off - rb_off];
        return true;
    }
    bool u8(uint32_t *v)
    {
        uint8_t b;
        if (!byte_at(pos, &b)) {
            return false;
        }
        pos++;
        *v = b;
        return true;
    }
    bool un(int bytes, uint32_t *v)
    {
        uint32_t r = 0;
        for (int i = 0; i < bytes; i++) {
            uint32_t b;
            if (!u8(&b)) {
                return false;
            }
            r |= b << (8 * i);
        }
        *v = r;
        return true;
    }
    bool skip(uint32_t n)
    {
        if (n > size - pos || pos > size) {
            return false;
        }
        pos += n;
        return true;
    }

    // ---- the stream ------------------------------------------------------------------------------------------------------
    void flush()
    {
        if (nw && !put_fail && !out->put(wb, nw)) {
            put_fail = true;
        }
        nw = 0;
    }
    void word(uint32_t w)
    {
        if (total >= TAPE_MAX_WORDS) {
            too_long = true;
            return;
        }
        wb[nw++] = (uint16_t)w;
        total++;
        if (nw == sizeof(wb) / sizeof(wb[0])) {
            flush();
        }
    }
    void gap(uint32_t ticks)
    {
        if (ticks) {
            word(TAPE_GAP);
            word(ticks & 0xFFFF);
            word(ticks >> 16);
        }
    }
    void pulse(uint32_t ticks)
    {
        if (ticks < TAPE_MIN_PULSE) {
            short_pulse = true;
            return;
        }
        if (ticks > 0xFFFD) {              // longer than a word: hold, then a pulse of the minimum that toggles
            gap(ticks - TAPE_MIN_PULSE);
            ticks = TAPE_MIN_PULSE;
        }
        word(ticks);
    }
    void pulse64(uint64_t ticks) { pulse(ticks > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)ticks); }
    void block_start()
    {
        if (idx->nblocks < TAPE_BLOCKS_MAX) {
            idx->start[idx->nblocks++] = total;
        } else {
            idx->truncated = true;
        }
    }
    // After a data block: one millisecond that ends its last pulse, then silence (a pause of 0 makes nothing).
    void pause_after(uint32_t ms)
    {
        if (ms) {
            pulse(MS_T);
            if (ms > 1) {
                gap((ms - 1) * MS_T);
            }
        }
    }
    bool failed() const { return rd_fail || put_fail || too_long || short_pulse; }

    // ---- block bodies ----------------------------------------------------------------------------------------------------
    // nbytes bytes from the cursor as bit pairs; the last byte holds `used` bits (the high ones).
    bool data_bytes(uint32_t b0, uint32_t b1, uint32_t nbytes, uint32_t used)
    {
        if (used == 0 || used > 8) {
            used = 8;
        }
        for (uint32_t i = 0; i < nbytes && !failed(); i++) {
            uint32_t c;
            if (!u8(&c)) {
                return false;
            }
            uint32_t nb = i == nbytes - 1 ? used : 8;
            for (uint32_t k = 0; k < nb; k++) {
                uint32_t d = (c >> (7 - k)) & 1 ? b1 : b0;
                pulse(d);
                pulse(d);
            }
        }
        return !failed();
    }
    void leader(uint32_t pilot_t, uint32_t n, uint32_t s1, uint32_t s2)
    {
        for (uint32_t i = 0; i < n && !failed(); i++) {
            pulse(pilot_t);
        }
        if (s1) {
            pulse(s1);
        }
        if (s2) {
            pulse(s2);
        }
    }
};

void reason(char *msg, int msglen, const char *fmt, unsigned a = 0, unsigned b = 0)
{
    snprintf(msg, msglen, fmt, a, b);
}

int finish_error(const Conv &c, bool truncated_file, char *msg, int msglen)
{
    if (c.put_fail) {
        reason(msg, msglen, "the tape could not be written into the core's memory");
    } else if (c.too_long) {
        reason(msg, msglen, "the tape is longer than 4 MB of pulses (%u words)", TAPE_MAX_WORDS);
    } else if (c.short_pulse) {
        reason(msg, msglen, "a pulse is shorter than %u T-states (a turbo loader): the tape plays at real speed only", TAPE_MIN_PULSE);
    } else if (c.rd_fail) {
        reason(msg, msglen, "cannot read the tape (the medium is gone?)");
    } else if (truncated_file) {
        reason(msg, msglen, "the file ends inside a block");
    }
    return 1;
}

int convert_tap(Conv &c, char *msg, int msglen)
{
    while (c.pos < c.size && !c.failed()) {
        uint32_t len;
        if (!c.un(2, &len)) {
            return finish_error(c, true, msg, msglen);
        }
        if (len == 0) {
            continue;
        }
        if (len > c.size - c.pos) {
            return finish_error(c, true, msg, msglen);
        }
        uint8_t flag = 0;
        c.byte_at(c.pos, &flag);
        c.block_start();
        c.leader(PILOT_T, flag < 128 ? PILOT_HEADER : PILOT_DATA, SYNC1_T, SYNC2_T);
        if (!c.data_bytes(BIT0_T, BIT1_T, len, 8)) {
            return finish_error(c, true, msg, msglen);
        }
        c.pause_after(1000);
    }
    return c.failed() ? finish_error(c, false, msg, msglen) : 0;
}

#define C64TAP_LOW 100                                   // us the line is low in every pulse (the falling edges are the tape's information)

int convert_c64tap(Conv &c, char *msg, int msglen)
{
    uint8_t h[20];
    for (int i = 0; i < 20; i++) {
        if (!c.byte_at(i, &h[i])) {
            reason(msg, msglen, "not a C64 .tap (too short)");
            return 1;
        }
    }
    if (memcmp(h, "C64-TAPE-RAW", 12) != 0) {
        reason(msg, msglen, "not a Commodore .tap (no C64-TAPE-RAW signature)");
        return 1;
    }
    if (h[12] > 1) {
        reason(msg, msglen, "this .tap version is not read (0 and 1 only)");
        return 1;
    }
    uint32_t len = (uint32_t)h[16] | ((uint32_t)h[17] << 8) | ((uint32_t)h[18] << 16) | ((uint32_t)h[19] << 24);
    c.pos = 20;
    uint64_t end = c.size < 20u + (uint64_t)len ? c.size : 20u + (uint64_t)len;
    c.block_start();
    while (c.pos < end && !c.failed()) {
        uint32_t v;
        c.u8(&v);
        uint32_t ticks = v * 8;
        if (v == 0) {
            if (h[12] == 0) {
                ticks = 256 * 8;
            } else {
                uint32_t cyc;
                if (end - c.pos < 3 || !c.un(3, &cyc)) {
                    return finish_error(c, true, msg, msglen);
                }
                ticks = cyc;
            }
        }
        if (ticks < C64TAP_LOW + TAPE_MIN_PULSE) {
            c.short_pulse = true;
            break;
        }
        c.pulse(C64TAP_LOW);                              // low, then the toggle to high
        c.pulse(ticks - C64TAP_LOW);                      // high until the falling edge
    }
    return c.failed() ? finish_error(c, false, msg, msglen) : 0;
}

int convert_tzx(Conv &c, bool stop48, char *msg, int msglen)
{
    static const char sig[] = "ZXTape!\x1a";
    uint8_t h[10];
    for (int i = 0; i < 10; i++) {
        if (!c.byte_at(i, &h[i])) {
            reason(msg, msglen, "not a TZX file (too short)");
            return 1;
        }
    }
    if (memcmp(h, sig, 8) != 0) {
        reason(msg, msglen, "not a TZX file (no ZXTape! signature)");
        return 1;
    }
    c.pos = 10;
    uint64_t loop_at = 0;
    uint32_t loop_left = 0;
    bool in_loop = false;
    while (c.pos < c.size && !c.failed()) {
        uint32_t id;
        c.u8(&id);
        uint32_t a = 0, n = 0, used = 0, pause = 0, len = 0, p0 = 0, p1 = 0, s1 = 0, s2 = 0;
        bool ok = true;
        switch (id) {
        case 0x10:                                                   // standard speed data
            ok = c.un(2, &pause) && c.un(2, &len);
            if (ok && len <= c.size - c.pos) {
                uint8_t flag = 0;
                c.byte_at(c.pos, &flag);
                c.block_start();
                c.leader(PILOT_T, flag < 128 ? PILOT_HEADER : PILOT_DATA, SYNC1_T, SYNC2_T);
                ok = c.data_bytes(BIT0_T, BIT1_T, len, 8);
                c.pause_after(pause);
            } else {
                ok = false;
            }
            break;
        case 0x11:                                                   // turbo speed data
            ok = c.un(2, &a) && c.un(2, &s1) && c.un(2, &s2) && c.un(2, &p0) && c.un(2, &p1) && c.un(2, &n) && c.un(1, &used) &&
                 c.un(2, &pause) && c.un(3, &len);
            if (ok && len <= c.size - c.pos) {
                c.block_start();
                c.leader(a, n, s1, s2);
                ok = c.data_bytes(p0, p1, len, used);
                c.pause_after(pause);
            } else {
                ok = false;
            }
            break;
        case 0x12:                                                   // pure tone
            ok = c.un(2, &a) && c.un(2, &n);
            if (ok) {
                c.block_start();
                for (uint32_t i = 0; i < n && !c.failed(); i++) {
                    c.pulse(a);
                }
            }
            break;
        case 0x13:                                                   // a sequence of pulses
            ok = c.un(1, &n);
            if (ok) {
                c.block_start();
                for (uint32_t i = 0; i < n && ok && !c.failed(); i++) {
                    ok = c.un(2, &a);
                    c.pulse(a);
                }
            }
            break;
        case 0x14:                                                   // pure data
            ok = c.un(2, &p0) && c.un(2, &p1) && c.un(1, &used) && c.un(2, &pause) && c.un(3, &len);
            if (ok && len <= c.size - c.pos) {
                c.block_start();
                ok = c.data_bytes(p0, p1, len, used);
                c.pause_after(pause);
            } else {
                ok = false;
            }
            break;
        case 0x15:                                                   // direct recording: runs of equal samples are the pulses
            ok = c.un(2, &a) && c.un(2, &pause) && c.un(1, &used) && c.un(3, &len);
            if (ok && len <= c.size - c.pos) {
                if (used == 0 || used > 8) {
                    used = 8;
                }
                c.block_start();
                uint32_t run = 0, cur = 2;                           // cur 2: no sample yet
                for (uint32_t i = 0; i < len && ok && !c.failed(); i++) {
                    uint32_t byte;
                    ok = c.u8(&byte);
                    uint32_t nb = i == len - 1 ? used : 8;
                    for (uint32_t k = 0; k < nb && ok; k++) {
                        uint32_t bit = (byte >> (7 - k)) & 1;
                        if (bit == cur) {
                            run++;
                        } else {
                            if (run) {
                                c.pulse64((uint64_t)run * a);
                            }
                            cur = bit;
                            run = 1;
                        }
                    }
                }
                if (run && ok) {
                    c.pulse64((uint64_t)run * a);
                }
                c.pause_after(pause);
            } else {
                ok = false;
            }
            break;
        case 0x20:                                                   // pause, or stop the tape
            ok = c.un(2, &pause);
            if (ok) {
                if (pause == 0) {
                    c.word(TAPE_STOP);
                } else {
                    c.gap(pause * MS_T);
                }
            }
            break;
        case 0x21: ok = c.un(1, &n) && c.skip(n); break;            // group start: a name
        case 0x22: break;                                            // group end
        case 0x24:                                                   // loop start
            if (in_loop) {
                reason(msg, msglen, "the TZX loops inside a loop (block 24 twice)");
                return 1;
            }
            ok = c.un(2, &n);
            if (ok && n == 0) {
                ok = false;
            }
            loop_left = n;
            loop_at = c.pos;
            in_loop = true;
            break;
        case 0x25:                                                   // loop end
            if (!in_loop) {
                reason(msg, msglen, "the TZX has a loop end (block 25) without a loop start");
                return 1;
            }
            if (--loop_left > 0) {
                c.pos = loop_at;
            } else {
                in_loop = false;
            }
            break;
        case 0x2A:                                                   // stop the tape if in 48K mode
            ok = c.un(4, &len) && c.skip(len);
            if (ok && stop48) {
                c.word(TAPE_STOP);
            }
            break;
        case 0x2B: ok = c.un(4, &len) && c.skip(len); break;       // the signal level: the engine's polarity is not read by the ROM loaders
        case 0x30: ok = c.un(1, &n) && c.skip(n); break;            // text description
        case 0x31: ok = c.un(1, &a) && c.un(1, &n) && c.skip(n); break;   // message
        case 0x32: ok = c.un(2, &n) && c.skip(n); break;            // archive info
        case 0x33: ok = c.un(1, &n) && c.skip(3 * n); break;        // hardware type
        case 0x34: ok = c.skip(8); break;                            // emulation info
        case 0x35: ok = c.skip(10) && c.un(4, &n) && c.skip(n); break;    // custom info
        case 0x5A: ok = c.skip(9); break;                            // glue
        case 0x16: case 0x17: reason(msg, msglen, "TZX block %02X is for the C64: not on this machine", id); return 1;
        case 0x18: reason(msg, msglen, "TZX block 18 (CSW recording) is not supported"); return 1;
        case 0x19: reason(msg, msglen, "TZX block 19 (generalized data) is not supported"); return 1;
        case 0x23: case 0x26: case 0x27: case 0x28:
            reason(msg, msglen, "TZX block %02X (jump, call, return or selection) is not supported", id);
            return 1;
        default:
            reason(msg, msglen, "TZX block %02X is not known", id);
            return 1;
        }
        if (!ok || c.failed()) {
            return finish_error(c, !c.rd_fail, msg, msglen);
        }
    }
    if (in_loop) {
        reason(msg, msglen, "the TZX ends inside a loop");
        return 1;
    }
    return c.failed() ? finish_error(c, false, msg, msglen) : 0;
}

}  // namespace

int tape_convert(CoreFile *f, TapeFormat fmt, bool stop48, TapeOut *out, TapeIndex *idx, char *msg, int msglen)
{
    static Conv c;                                // 1.5 KB: not on the stack of a task
    memset(&c, 0, sizeof(c));
    memset(idx, 0, sizeof(*idx));
    c.f = f;
    c.size = f->size();
    c.out = out;
    c.idx = idx;
    int rc = fmt == TAPE_C64 ? convert_c64tap(c, msg, msglen) : fmt == TAPE_TAP ? convert_tap(c, msg, msglen) : convert_tzx(c, stop48, msg, msglen);
    if (rc) {
        return rc;
    }
    idx->words = c.total;
    c.word(0);                                    // the end of the tape; then zeros to a whole 16 byte line (the engine reads lines)
    while (c.nw % 8 != 0 && !c.too_long) {
        c.wb[c.nw++] = 0;
    }
    c.flush();
    if (c.put_fail) {
        reason(msg, msglen, "the tape could not be written into the core's memory");
        return 1;
    }
    if (idx->words == 0) {
        reason(msg, msglen, "the tape has no data");
        return 1;
    }
    return 0;
}

bool tape_format_of(const char *path, TapeFormat *fmt)
{
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return false;
    }
    if (!strcasecmp(dot, ".tap")) {
        *fmt = TAPE_TAP;
        return true;
    }
    if (!strcasecmp(dot, ".tzx") || !strcasecmp(dot, ".cdt")) {
        *fmt = TAPE_TZX;
        return true;
    }
    return false;
}

uint32_t tape_block_at(const TapeIndex *idx, uint32_t pos_words)
{
    uint32_t b = 0;
    for (uint32_t i = 0; i < idx->nblocks; i++) {
        if (idx->start[i] <= pos_words) {
            b = i;
        }
    }
    return b;
}
