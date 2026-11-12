// PROVENANCE: copied unchanged from TRX64-Ultimate software/ultimate-app/software/io/c64/trx_c64re.cc (.c64re and .c64rering from the frame ring; GPL-3.0), T104.2 UI.
// .c64re / .c64rering writer and .c64re loader for the frame ring. See
// trx_c64re.h. The gzip, SHA-256 and JSON pieces follow apps/c64re/src/c64re.c
// (the same container, written from a .u2a there).
//
// Node mapping (T20 §9.3), from a timeline entry's chip block (c64_core.v map)
// and the RAM / colour RAM the ring materializes for it:
//   cpu        A X Y S P PC from chip 1 w0/w1; cycles = the ring's phi2 count
//   ram        64 KB, $D800-$DBFF the colour nibbles (TRX64's own captures do
//              this: its VIC reads colour from there); the RAM under the I/O
//              rides in trx64Hw.ramUnderIo
//   cpuPort*   $00 and the $01 latch (chip 1 w2)
//   cia1/cia2  v2 nodes: registers as read, latches, mask and alarm from the
//              hidden words, timer states from CRA/CRB (partial)
//   vic        registers as read, raster position of the snap, colour RAM;
//              the internal counters and draw pipeline cold (partial)
//   cartBytes  null (TRX64 detaches a cartridge), no sid, drive or REU node
//   trx64Hw    {layout, x, line, block (58 words LE), ramUnderIo, colorRam}:
//              everything trx_c64re_load() needs to put the board back exactly

#include "trx_c64re.h"
#include "trx_ring.h"
#include "filemanager.h"
#include "itu.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include "jsmn.h"

#define LAYOUT         1
#define PC_ADJ         0     // chip 1 w1's PC at the snap edge minus the next opcode's address
#define W_CPU          1
#define W_VIC          4
#define W_CIA1         16
#define W_CIA2         21
#define W_CIA1H        34
#define W_CIA2H        46

// ---- SHA-256 ---------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8];
    uint8_t block[64];
    uint32_t len_lo, len_hi;
    int fill;
};

static const uint32_t sha_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static inline uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha_compress(Sha256 *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + sha_k[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(Sha256 *s)
{
    static const uint32_t h0[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    memcpy(s->h, h0, sizeof(h0));
    s->len_lo = s->len_hi = 0;
    s->fill = 0;
}

static void sha_add(Sha256 *s, const uint8_t *p, uint32_t len)
{
    uint32_t lo = s->len_lo + len;
    if (lo < s->len_lo) {
        s->len_hi++;
    }
    s->len_lo = lo;
    while (len) {
        int n = 64 - s->fill;
        if (uint32_t(n) > len) {
            n = len;
        }
        memcpy(s->block + s->fill, p, n);
        s->fill += n;
        p += n;
        len -= n;
        if (s->fill == 64) {
            sha_compress(s, s->block);
            s->fill = 0;
        }
    }
}

static void sha_final(Sha256 *s, uint8_t *out)
{
    uint64_t bits = (uint64_t(s->len_hi) << 32 | s->len_lo) * 8;
    uint8_t pad = 0x80;
    sha_add(s, &pad, 1);
    pad = 0;
    while (s->fill != 56) {
        sha_add(s, &pad, 1);
    }
    uint8_t tail[8];
    for (int i = 0; i < 8; i++) {
        tail[i] = uint8_t(bits >> (56 - 8 * i));
    }
    sha_add(s, tail, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = uint8_t(s->h[i] >> 24);
        out[4 * i + 1] = uint8_t(s->h[i] >> 16);
        out[4 * i + 2] = uint8_t(s->h[i] >> 8);
        out[4 * i + 3] = uint8_t(s->h[i]);
    }
}

// ---- gzip with stored blocks, written as the document is produced ----------------
#define GZ_BLOCK 16384

// The rvlite core reads DDR2 without a cache (about a microsecond per load),
// so the hot paths keep their state in locals: one load per input byte, the
// CRC bitwise in registers, stores (posted) into the block.
static inline uint32_t crc8(uint32_t c)
{
    for (int k = 0; k < 8; k++) {
        c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c;
}

static inline char b64c(uint32_t v)
{
    v &= 63;
    return v < 26 ? char('A' + v) : v < 52 ? char('a' + v - 26) : v < 62 ? char('0' + v - 52) : v == 62 ? '+' : '/';
}

class Gz {
    File *f;
    uint8_t *buf;
    int fill;
    uint32_t crc, isize;
    bool hash;
public:
    Sha256 sha;
    bool failed;
    uint32_t written;
    uint32_t write_ms = 0;

    Gz(File *file, bool with_hash) : f(file), fill(0), crc(0xFFFFFFFFu), isize(0), hash(with_hash), failed(false), written(0)
    {
        sha_init(&sha);
        buf = (uint8_t *)malloc(GZ_BLOCK);
        failed = !buf;
        static const uint8_t header[10] = { 0x1F, 0x8B, 0x08, 0, 0, 0, 0, 0, 0, 0x03 };
        raw(header, sizeof(header));
    }
    ~Gz() { free(buf); }

    void raw(const uint8_t *p, uint32_t len)
    {
        if (failed) {
            return;
        }
        if (hash) {
            sha_add(&sha, p, len);
        }
        uint32_t tr = 0;
        uint16_t t = getMsTimer();
        if (f->write(p, len, &tr) != FR_OK || tr != len) {
            failed = true;
        }
        write_ms += uint16_t(getMsTimer() - t);
        written += len;
    }

    void flush(bool final)
    {
        uint8_t head[5];
        head[0] = final ? 1 : 0;
        head[1] = uint8_t(fill);
        head[2] = uint8_t(fill >> 8);
        head[3] = uint8_t(~fill);
        head[4] = uint8_t((~fill) >> 8);
        raw(head, 5);
        if (fill) {
            raw(buf, fill);
        }
        fill = 0;
    }

    void put(const char *s, int len)
    {
        uint32_t c = crc;
        int fl = fill;
        uint8_t *const b = buf;
        for (int i = 0; i < len; i++) {
            uint8_t ch = uint8_t(s[i]);
            c = crc8(c ^ ch);
            b[fl++] = ch;
            if (fl == GZ_BLOCK) {
                fill = fl;
                flush(false);
                fl = 0;
            }
        }
        crc = c;
        fill = fl;
        isize += len;
    }

    void str(const char *s) { put(s, strlen(s)); }

    void num(const char *prefix, int64_t v)
    {
        char tmp[24];
        int n = 0;
        if (prefix) {
            str(prefix);
        }
        uint64_t u = v < 0 ? uint64_t(-v) : uint64_t(v);
        do {                            // the printf here has no 64-bit conversions
            tmp[n++] = char('0' + u % 10);
            u /= 10;
        } while (u);
        if (v < 0) {
            tmp[n++] = '-';
        }
        char out[24];
        for (int i = 0; i < n; i++) {
            out[i] = tmp[n - 1 - i];
        }
        put(out, n);
    }

    // a whole array as base64, '=' padded
    void b64(const uint8_t *data, int len)
    {
        uint32_t c = crc;
        int fl = fill;
        uint8_t *const b = buf;
        int outn = 0;
        for (int i = 0; i < len; i += 3) {
            int rem = len - i;
            uint32_t v = uint32_t(data[i]) << 16;
            if (rem > 1) {
                v |= uint32_t(data[i + 1]) << 8;
            }
            if (rem > 2) {
                v |= data[i + 2];
            }
            char o[4] = { b64c(v >> 18), b64c(v >> 12), rem > 1 ? b64c(v >> 6) : '=', rem > 2 ? b64c(v) : '=' };
            for (int k = 0; k < 4; k++) {
                c = crc8(c ^ uint8_t(o[k]));
                b[fl++] = uint8_t(o[k]);
                if (fl == GZ_BLOCK) {
                    fill = fl;
                    flush(false);
                    fl = 0;
                }
            }
            outn += 4;
        }
        crc = c;
        fill = fl;
        isize += outn;
    }

    void ta(const uint8_t *data, int len)
    {
        str("{\"$ta\":\"Uint8Array\",\"b64\":\"");
        b64(data, len);
        str("\"}");
    }

    void array(const uint8_t *data, int count, int mask)
    {
        char out[4 * 64 + 4];
        int on = 0;
        put("[", 1);
        for (int i = 0; i < count; i++) {
            int v = data[i] & mask;
            if (i) {
                out[on++] = ',';
            }
            if (v >= 100) {
                out[on++] = char('0' + v / 100);
            }
            if (v >= 10) {
                out[on++] = char('0' + (v / 10) % 10);
            }
            out[on++] = char('0' + v % 10);
            if (on > 4 * 64) {
                put(out, on);
                on = 0;
            }
        }
        out[on++] = ']';
        put(out, on);
    }

    bool finish(uint8_t *digest)
    {
        flush(true);
        crc ^= 0xFFFFFFFFu;
        uint8_t tail[8];
        for (int i = 0; i < 4; i++) {
            tail[i] = uint8_t(crc >> (8 * i));
            tail[4 + i] = uint8_t(isize >> (8 * i));
        }
        raw(tail, 8);
        if (digest) {
            sha_final(&sha, digest);
        }
        return !failed;
    }
};

// ---- the checkpoint --------------------------------------------------------------
static uint32_t bits(const uint32_t *w, int lo, int n)
{
    uint32_t v = 0;
    for (int b = 0; b < n; b++) {
        v |= ((w[(lo + b) / 32] >> ((lo + b) % 32)) & 1u) << b;
    }
    return v;
}

static uint8_t wbyte(const uint32_t *w, int word0, int reg)
{
    return uint8_t(w[word0 + reg / 4] >> (8 * (reg & 3)));
}

static int timer_state(uint8_t cr)
{
    if (!(cr & 1)) {
        return (cr & 8) ? 0x008 : 0;
    }
    if (cr & 0x60) {            // counts CNT or timer A: no phi2 input
        return 0x001 | (cr & 8);
    }
    return 0x863 | ((cr & 8) ? 0x1108 : 0);
}

static void write_cia(Gz &g, const char *name, const uint32_t *w, int wr, int wh, uint64_t cycles)
{
    const uint32_t *h = w + wh;
    uint8_t rd[16], c[16];
    for (int i = 0; i < 16; i++) {
        rd[i] = wbyte(w, wr, i);
        c[i] = rd[i];
    }
    uint32_t ta = bits(h, 43, 16), tb = bits(h, 83, 16);
    c[0] = bits(h, 32, 8);
    c[1] = bits(h, 24, 8);
    c[2] = bits(h, 16, 8);
    c[3] = bits(h, 8, 8);
    c[4] = ta & 0xFF; c[5] = ta >> 8;
    c[6] = tb & 0xFF; c[7] = tb >> 8;
    c[13] = bits(h, 311, 5);
    g.str("\"");
    g.str(name);
    g.str("\":{\"v\":2,\"c_cia\":");
    g.array(c, 16, 0xFF);
    g.num(",\"irqflags\":", rd[13] & 0x1F);
    g.str(",\"ack_irqflags\":0,\"new_irqflags\":0");
    g.num(",\"irq_enabled\":", (rd[13] & c[13] & 0x1F) ? 1 : 0);
    g.str(",\"rdi\":0,\"ifr_clock\":0,\"ifr_delay\":0,\"tat\":0,\"tbt\":0");
    g.num(",\"ta_state\":", timer_state(rd[14]));
    g.num(",\"ta_latch\":", ta);
    g.num(",\"ta_cnt\":", rd[4] | rd[5] << 8);
    g.num(",\"ta_clk\":", cycles);
    g.num(",\"tb_state\":", timer_state(rd[15]));
    g.num(",\"tb_latch\":", tb);
    g.num(",\"tb_cnt\":", rd[6] | rd[7] << 8);
    g.num(",\"tb_clk\":", cycles);
    g.str(",\"sr_bits\":0,\"sdr_valid\":0,\"sdr_force_finish\":0,\"shifter\":0,\"sdr_delay\":0");
    g.str(",\"sp_in_state\":0,\"cnt_in_state\":0,\"cnt_out_state\":0");
    uint8_t alarm[4] = { uint8_t(bits(h, 147, 8)), uint8_t(bits(h, 139, 8)), uint8_t(bits(h, 131, 8)),
                         uint8_t(bits(h, 123, 8)) };
    g.str(",\"todalarm\":");
    g.array(alarm, 4, 0xFF);
    g.str(",\"todlatch\":");
    g.array(rd + 8, 4, 0xFF);
    g.str(",\"todlatched\":0,\"todstopped\":0,\"todticks\":0,\"todclk\":0,\"todtickcounter\":0");
    g.str(",\"power_tickcounter\":0,\"power_ticks\":0,\"old_pa\":255,\"old_pb\":255");
    g.num(",\"read_clk\":", cycles);
    g.str(",\"read_offset\":0,\"last_read\":0,\"model\":0}");
}

static void write_vic(Gz &g, const uint32_t *w, const uint8_t *color, int line, int cycle)
{
    uint8_t regs[0x40];
    for (int r = 0; r < 0x40; r++) {
        regs[r] = r < 0x30 ? wbyte(w, W_VIC, r) : 0xFF;
    }
    g.str("\"vic\":{\"model\":0,\"regs\":");
    g.array(regs, 0x40, 0xFF);
    g.num(",\"raster_cycle\":", cycle);
    g.num(",\"cycle_flags\":0,\"raster_line\":", line);
    g.num(",\"start_of_frame\":0,\"irq_status\":", regs[0x19] & 0x0F);
    g.str(",\"raster_irq_line\":0,\"raster_irq_triggered\":0");
    g.str(",\"vbuf\":[],\"cbuf\":[],\"gbuf\":0,\"dbuf_offset\":0,\"dbuf\":[]");
    g.num(",\"ysmooth\":", regs[0x11] & 7);
    g.str(",\"allow_bad_lines\":0,\"sprite_sprite_collisions\":0");
    g.str(",\"sprite_background_collisions\":0,\"clear_collisions\":0,\"idle_state\":1");
    g.str(",\"vcbase\":0,\"vc\":0,\"rc\":0,\"vmli\":0,\"bad_line\":0");
    g.str(",\"light_pen\":{\"state\":0,\"triggered\":0,\"x\":0,\"y\":0,\"x_extra_bits\":0,\"trigger_cycle\":0}");
    g.str(",\"reg11_delay\":0,\"prefetch_cycles\":0,\"sprite_display_bits\":0,\"sprite_dma\":0");
    g.str(",\"last_color_reg\":0,\"last_color_value\":0,\"last_read_phi1\":0,\"last_bus_phi2\":0");
    g.str(",\"vborder\":1,\"set_vborder\":1,\"main_border\":1,\"refresh_counter\":0");
    g.str(",\"color_ram\":");
    g.array(color, 0x400, 0x0F);
    g.str(",\"sprite\":[]");
    g.str(",\"drawCycle\":{\"gbuf_pipe0_reg\":0,\"cbuf_pipe0_reg\":0,\"vbuf_pipe0_reg\":0");
    g.str(",\"gbuf_pipe1_reg\":0,\"cbuf_pipe1_reg\":0,\"vbuf_pipe1_reg\":0");
    g.num(",\"xscroll_pipe\":", regs[0x16] & 7);
    g.str(",\"vmode11_pipe\":0,\"vmode16_pipe\":0,\"vmode16_pipe2\":0");
    g.str(",\"gbuf_reg\":0,\"gbuf_mc_flop\":0,\"gbuf_pixel_reg\":0,\"cbuf_reg\":0,\"vbuf_reg\":0,\"dmli\":0");
    g.str(",\"sprite_x_pipe\":[],\"sprite_pri_bits\":0,\"sprite_mc_bits\":0,\"sprite_expx_bits\":0");
    g.str(",\"sprite_pending_bits\":0,\"sprite_active_bits\":0,\"sprite_halt_bits\":0");
    g.str(",\"sbuf_reg\":null,\"sbuf_pixel_reg\":null,\"sbuf_expx_flops\":0,\"sbuf_mc_flops\":0,\"border_state\":0");
    g.str(",\"render_buffer\":null,\"pri_buffer\":null,\"pixel_buffer\":null,\"cregs\":");
    g.ta(regs, 0x2F);
    g.str(",\"last_color_reg\":0,\"last_color_value\":0,\"cycle_flags_pipe\":0}}");
}

// The checkpoint tree of one entry (an object, without a key in front).
// TRX64 0.12.2's turbo node (T20 §10.1, §17.5), the one place that builds it. The core has no
// turbo yet (T29): the machine always runs at 1 MHz with badline stalls, which is the registers-
// mode reset state, written out. T29 puts its state (regs_en, prefer, d030_b0, d031, hold) on
// the state bus; this takes it from the frame's block words then.
static void write_turbo(Gz &g, const TrxFrame &)
{
    g.str("\"turbo\":{\"enable\":1,\"menuIndex\":0,\"menuStalls\":true,\"d031\":0,\"d030\":0,\"resetHold\":0}");
}

static void write_checkpoint(Gz &g, const TrxFrame &f, uint8_t *ram, const uint8_t *col)
{
    const uint32_t *w = f.w;
    static uint8_t under_io[1024];  // one writer at a time (the REST task or the menu)
    memcpy(under_io, ram + 0xD800, 1024);
    for (int i = 0; i < 1024; i++) {
        ram[0xD800 + i] = col[i] & 0x0F;
    }
    uint32_t r0 = w[W_CPU], r1 = w[W_CPU + 1], r2 = w[W_CPU + 2];
    int pc = int((r1 & 0xFFFF) - PC_ADJ) & 0xFFFF;
    int cycle = f.x >> 3;
    g.str("{\"schemaVersion\":1,\"atInstructionBoundary\":true,\"cpu\":{");
    g.num("\"pc\":", pc);
    g.num(",\"a\":", r0 & 0xFF);
    g.num(",\"x\":", (r0 >> 8) & 0xFF);
    g.num(",\"y\":", (r0 >> 16) & 0xFF);
    g.num(",\"sp\":", (r0 >> 24) & 0xFF);
    g.num(",\"flags\":", (r1 >> 16) & 0xFF);
    g.num(",\"cycles\":", int64_t(f.cycles));
    g.str("},\"ram\":");
    g.ta(ram, 65536);
    g.num(",\"cpuPortDirection\":", r2 & 0xFF);
    g.num(",\"cpuPortValue\":", (r2 >> 8) & 0xFF);
    g.str(",");
    write_cia(g, "cia1", w, W_CIA1, W_CIA1H, f.cycles);
    g.str(",");
    write_cia(g, "cia2", w, W_CIA2, W_CIA2H, f.cycles);
    g.str(",");
    write_vic(g, w, col, f.line, cycle);
    g.str(",\"alarmsMaincpu\":[],\"cartBytes\":null");
    g.str(",");
    write_turbo(g, f);
    g.num(",\"trx64Hw\":{\"layout\":", LAYOUT);
    g.num(",\"x\":", f.x);
    g.num(",\"line\":", f.line);
    g.num(",\"marker\":", f.marker);
    g.num(",\"flags\":", f.flags);
    g.str(",\"block\":");
    uint8_t blk[TRX_BLOCK_WORDS * 4];
    for (int i = 0; i < TRX_BLOCK_WORDS; i++) {
        for (int b = 0; b < 4; b++) {
            blk[4 * i + b] = uint8_t(w[i] >> (8 * b));
        }
    }
    g.ta(blk, sizeof(blk));
    g.str(",\"ramUnderIo\":");
    g.ta(under_io, 1024);
    g.str(",\"colorRam\":");
    g.ta(col, 1024);
    g.str("}}");
    memcpy(ram + 0xD800, under_io, 1024);
}

static const char *open_out(const char *path, File **f)
{
    FileManager *fm = FileManager::getFileManager();
    if (fm->fopen(path, FA_WRITE | FA_CREATE_ALWAYS, f) != FR_OK) {
        return "The file could not be created";
    }
    return NULL;
}

const char *trx_c64re_dump(const char *path, int idx, uint32_t *bytes)
{
    TrxFrame *f = (TrxFrame *)malloc(sizeof(TrxFrame));
    uint8_t *ram = (uint8_t *)malloc(65536);
    uint8_t *col = (uint8_t *)malloc(1024);
    const char *err = NULL;
    File *file = NULL;
    if (!f || !ram || !col) {
        err = "Out of memory";
    } else if (!trx_ring_frame(idx, f)) {
        err = "No such frame";
    } else if (!f->block) {
        err = "The frame has no chip block";
    } else if (int e = trx_ring_materialize(idx, ram, col)) {
        err = trx_ring_error(e);
    } else {
        err = open_out(path, &file);
    }
    if (!err) {
        uint8_t header[41];
        memset(header, 0, sizeof(header));
        memcpy(header, "C64RESNP", 8);
        header[8] = 1;
        uint32_t tr;
        file->write(header, sizeof(header), &tr);
        Gz g(file, true);
        g.str("{\"manifest\":{\"kind\":\"c64re-runtime-snapshot\",\"version\":1,\"createdAt\":\"1970-01-01T00:00:00Z\"");
        g.str(",\"machine\":{\"model\":\"c64-pal\",\"runtimeVersion\":\"trx64-ultimate/t20-app\"}");
        g.num(",\"checkpoint\":{\"encoding\":\"ta-json-gz/1\",\"payloadRef\":\"checkpoint\",\"cycle\":", int64_t(f->cycles));
        g.num(",\"pc\":", int((f->w[W_CPU + 1] & 0xFFFF) - PC_ADJ) & 0xFFFF);
        g.str("},\"media\":[]},\"checkpoint\":");
        write_checkpoint(g, *f, ram, col);
        g.str(",\"mediaPayloads\":{}}");
        uint8_t digest[32];
        if (!g.finish(digest)) {
            err = "The file could not be written";
        } else {
            file->seek(9);
            if (file->write(digest, 32, &tr) != FR_OK || tr != 32) {
                err = "The file could not be written";
            }
        }
        *bytes = 41 + g.written;
        FileManager::getFileManager()->fclose(file);
    }
    free(f);
    free(ram);
    free(col);
    return err;
}

#define MAX_RING_ENTRIES 64

const char *trx_c64rering_dump_ex(const char *path, int seconds, int stride, uint32_t *bytes, int *entries,
                                  trx_progress_fn progress, void *ctx)
{
    if (stride < 1) {
        stride = 1;
    }
    int n_tl = trx_ring_count();
    int span = (seconds * (int)trx_ring_fps_x100() + 50) / 100;
    int idx[MAX_RING_ENTRIES];
    int n = 0;
    // from the newest whole entry back, every stride frames
    int i = trx_ring_target(0);
    int lo = n_tl - 1 - span;
    while (i >= 0 && i >= lo && n < MAX_RING_ENTRIES) {
        idx[n++] = i;
        int next = i - stride;
        if (next < 0) {
            break;
        }
        i = trx_ring_target(n_tl - 1 - next);
    }
    if (!n) {
        return "The ring holds no frame";
    }
    // oldest first
    for (int k = 0; k < n / 2; k++) {
        int t = idx[k];
        idx[k] = idx[n - 1 - k];
        idx[n - 1 - k] = t;
    }
    uint8_t **rams = (uint8_t **)calloc(n, sizeof(uint8_t *));
    uint8_t **cols = (uint8_t **)calloc(n, sizeof(uint8_t *));
    TrxFrame *fr = (TrxFrame *)malloc(n * sizeof(TrxFrame));
    const char *err = NULL;
    if (!rams || !cols || !fr) {
        err = "Out of memory";
    }
    // the entries as they are now (materialize may move the indices; it
    // finds its targets by their seq)
    for (int k = 0; !err && k < n; k++) {
        if (!trx_ring_frame(idx[k], &fr[k])) {
            err = "No such frame";
        }
    }
    for (int k = 0; !err && k < n; k++) {
        rams[k] = (uint8_t *)malloc(65536);
        cols[k] = (uint8_t *)malloc(1024);
        if (!rams[k] || !cols[k]) {
            err = "Out of memory";
        }
    }
    uint16_t t0 = getMsTimer();
    if (!err) {
        if (int e = trx_ring_materialize_many(idx, n, rams, cols)) {
            err = trx_ring_error(e);
        }
    }
    uint16_t t1 = getMsTimer();
    if (!err && progress) {
        progress(0, n, ctx);        // the RAM of every entry is in memory; the JSON and the file follow
    }
    File *file = NULL;
    if (!err) {
        err = open_out(path, &file);
    }
    if (!err) {
        static const uint8_t header[9] = { 'C', '6', '4', 'R', 'E', 'R', 'N', 'G', 1 };
        uint32_t tr;
        file->write(header, sizeof(header), &tr);
        Gz g(file, false);
        char id[32];
        const TrxFrame &last = fr[n - 1];
        g.num("{\"checkpoint_ring\":{\"budget_bytes\":", int64_t(n) * 256 * 1024);
        g.num(",\"max_entries\":", n);
        g.num(",\"seq\":", last.seq);
        sprintf(id, "cp_%u_%u", (unsigned)(last.cycles / trx_ring_frame_cycles()), (unsigned)last.seq);
        g.str(",\"current_id\":\"");
        g.str(id);
        g.str("\",\"entries\":[");
        for (int k = 0; k < n; k++) {
            const TrxFrame *f = &fr[k];
            // frame: the ring's cycle count in frames of the running mode (seq skips the frames a rewind cut)
            uint32_t frame = uint32_t(f->cycles / trx_ring_frame_cycles());
            sprintf(id, "cp_%u_%u", (unsigned)frame, (unsigned)f->seq);
            g.str(k ? ",{\"id\":\"" : "{\"id\":\"");
            g.str(id);
            g.num("\",\"frame\":", frame);
            g.num(",\"cycles\":", int64_t(f->cycles));
            g.str(",\"pinned\":false,\"byte_size\":96000,\"created_at_ms\":0,\"payload\":");
            write_checkpoint(g, *f, rams[k], cols[k]);
            g.str("}");
            if (progress) {
                progress(k + 1, n, ctx);
            }
        }
        g.str("]},\"delta_ring\":{\"entry_cap\":1,\"writes_cap\":1,\"enabled\":false,\"entries\":\"\",\"writes\":\"\"}");
        g.str(",\"cpu_history\":{\"capacity\":1,\"enabled\":false,\"entries\":\"\"}}");
        if (!g.finish(NULL)) {
            err = "The file could not be written";
        }
        printf("[RING] .c64rering: %d entries, materialized in %u ms, written in %u ms (%u in file writes)\n", n,
               (unsigned)uint16_t(t1 - t0), (unsigned)uint16_t(getMsTimer() - t1), (unsigned)g.write_ms);
        *bytes = 9 + g.written;
        *entries = n;
        FileManager::getFileManager()->fclose(file);
    }
    for (int k = 0; k < n && rams && cols; k++) {
        free(rams[k]);
        free(cols[k]);
    }
    free(rams);
    free(cols);
    free(fr);
    return err;
}

const char *trx_c64rering_dump(const char *path, int seconds, int stride, uint32_t *bytes, int *entries)
{
    return trx_c64rering_dump_ex(path, seconds, stride, bytes, entries, NULL, NULL);
}

// ---- load ------------------------------------------------------------------------
// The body of a gzip member made of stored blocks (what trx_c64re_dump writes).
static char *gunzip_stored(const uint8_t *p, uint32_t len, uint32_t *out_len, const char **err)
{
    if (len < 18 || p[0] != 0x1F || p[1] != 0x8B || p[2] != 8) {
        *err = "Not a gzip body";
        return NULL;
    }
    uint32_t i = 10;
    uint8_t flg = p[3];
    if (flg & 4) {
        i += 2 + (p[i] | p[i + 1] << 8);
    }
    if (flg & 8) {
        while (i < len && p[i]) i++;
        i++;
    }
    if (flg & 16) {
        while (i < len && p[i]) i++;
        i++;
    }
    if (flg & 2) {
        i += 2;
    }
    uint32_t isize = p[len - 4] | p[len - 3] << 8 | p[len - 2] << 16 | uint32_t(p[len - 1]) << 24;
    char *out = (char *)malloc(isize + 1);
    if (!out) {
        *err = "Out of memory";
        return NULL;
    }
    uint32_t o = 0;
    for (;;) {
        if (i + 5 > len) {
            *err = "Truncated gzip body";
            free(out);
            return NULL;
        }
        uint8_t h = p[i];
        if (h & 6) {
            *err = "Compressed .c64re (only the board's own uncompressed files load)";
            free(out);
            return NULL;
        }
        uint32_t n = p[i + 1] | p[i + 2] << 8;
        i += 5;
        if (i + n > len || o + n > isize) {
            *err = "Bad stored block";
            free(out);
            return NULL;
        }
        memcpy(out + o, p + i, n);
        o += n;
        i += n;
        if (h & 1) {
            break;
        }
    }
    out[o] = 0;
    *out_len = o;
    return out;
}

static bool tok_eq(const char *js, const jsmntok_t &t, const char *s)
{
    int n = t.end - t.start;
    return t.type == JSMN_STRING && int(strlen(s)) == n && !strncmp(js + t.start, s, n);
}

// the value token of key in object o, or -1
static int key(const char *js, const jsmntok_t *t, int n, int o, const char *k)
{
    if (o < 0 || t[o].type != JSMN_OBJECT) {
        return -1;
    }
    for (int i = o + 1; i < n && t[i].start < t[o].end; i++) {
        if (t[i].parent == o && tok_eq(js, t[i], k)) {
            return i + 1;
        }
    }
    return -1;
}

static int64_t tok_int(const char *js, const jsmntok_t *t, int i)
{
    if (i < 0) {
        return -1;
    }
    return strtoll(js + t[i].start, NULL, 10);
}

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// a {"$ta":..,"b64":..} node into out (exactly len bytes), false otherwise
static bool tok_ta(const char *js, const jsmntok_t *t, int n, int i, uint8_t *out, int len)
{
    int b = key(js, t, n, i, "b64");
    if (b < 0) {
        return false;
    }
    const char *s = js + t[b].start;
    int sl = t[b].end - t[b].start;
    int o = 0;
    uint32_t acc = 0;
    int nb = 0;
    for (int k = 0; k < sl; k++) {
        int v = b64val(s[k]);
        if (v < 0) {
            continue;
        }
        acc = (acc << 6) | v;
        nb += 6;
        if (nb >= 8) {
            nb -= 8;
            if (o < len) {
                out[o] = uint8_t(acc >> nb);
            }
            o++;
        }
    }
    return o == len;
}

// A .c64re, or the current entry of a .c64rering (checkpoint_ring.current_id;
// the entry's payload is the same checkpoint node), written by this board.
const char *trx_c64re_load(const char *path)
{
    FileManager *fm = FileManager::getFileManager();
    File *file;
    if (fm->fopen(path, FA_READ, &file) != FR_OK) {
        return "The file could not be opened";
    }
    uint32_t size = file->get_size();
    uint8_t *raw = (uint8_t *)malloc(size);
    const char *err = NULL;
    uint32_t tr = 0;
    if (!raw) {
        err = "Out of memory";
    } else if (file->read(raw, size, &tr) != FR_OK || tr != size) {
        err = "The file could not be read";
    }
    fm->fclose(file);
    bool ring = !err && size >= 9 + 18 && !memcmp(raw, "C64RERNG", 8) && raw[8] == 1;
    if (!err && !ring && (size < 41 + 18 || memcmp(raw, "C64RESNP", 8) || raw[8] != 1)) {
        err = "Not a .c64re or .c64rering (format 1)";
    }
    if (!err && !ring) {
        Sha256 s;
        uint8_t d[32];
        sha_init(&s);
        sha_add(&s, raw + 41, size - 41);
        sha_final(&s, d);
        if (memcmp(d, raw + 9, 32)) {
            err = "Hash mismatch";
        }
    }
    char *js = NULL;
    uint32_t jl = 0;
    if (!err) {
        uint32_t hdr = ring ? 9 : 41;
        js = gunzip_stored(raw + hdr, size - hdr, &jl, &err);
    }
    free(raw);
    jsmntok_t *t = NULL;
    int n = 0;
    if (!err) {
        jsmn_parser p;
        jsmn_init(&p);
        int need = jsmn_parse(&p, js, jl, NULL, 0);
        if (need < 1) {
            err = "Bad JSON";
        } else {
            t = (jsmntok_t *)malloc(sizeof(jsmntok_t) * need);
            if (!t) {
                err = "Out of memory";
            } else {
                jsmn_init(&p);
                n = jsmn_parse(&p, js, jl, t, need);
                if (n < 1) {
                    err = "Bad JSON";
                }
            }
        }
    }
    int cp = -1;
    if (!err && !ring) {
        cp = key(js, t, n, 0, "checkpoint");
    } else if (!err) {
        // checkpoint_ring.entries[k].payload where entries[k].id == current_id
        int cr = key(js, t, n, 0, "checkpoint_ring");
        int cur = key(js, t, n, cr, "current_id");
        int ents = key(js, t, n, cr, "entries");
        if (cur < 0 || ents < 0 || t[ents].type != JSMN_ARRAY) {
            err = "No checkpoint_ring entries";
        } else {
            for (int i = ents + 1; i < n && t[i].start < t[ents].end; i++) {
                if (t[i].parent != ents) {
                    continue;
                }
                int id = key(js, t, n, i, "id");
                if (id >= 0 && t[id].end - t[id].start == t[cur].end - t[cur].start &&
                    !strncmp(js + t[id].start, js + t[cur].start, t[cur].end - t[cur].start)) {
                    cp = key(js, t, n, i, "payload");
                    break;
                }
            }
            if (cp < 0) {
                err = "The current entry is not in the file";
            }
        }
    }
    uint8_t *ram = NULL, *col = NULL;
    uint32_t w[TRX_BLOCK_WORDS];
    int x = 0;
    if (!err) {
        // a file from a turbo-capable machine (TRX64 or a later board): this core runs at 1 MHz
        int tb = key(js, t, n, cp, "turbo");
        if (tb >= 0 && (tok_int(js, t, key(js, t, n, tb, "menuIndex")) > 0 ||
                        (tok_int(js, t, key(js, t, n, tb, "d031")) & 0x0F) > 0)) {
            printf("[C64RE] the file's turbo state (speed index %d) is not applied: this bitstream has no turbo\n",
                   (int)tok_int(js, t, key(js, t, n, tb, "menuIndex")));
        }
        int hw = key(js, t, n, cp, "trx64Hw");
        ram = (uint8_t *)malloc(65536);
        col = (uint8_t *)malloc(1024);
        static uint8_t blk[TRX_BLOCK_WORDS * 4];
        static uint8_t under[1024];
        if (hw < 0) {
            err = "No trx64Hw node (not written by this board)";
        } else if (tok_int(js, t, key(js, t, n, hw, "layout")) != LAYOUT) {
            err = "Unknown trx64Hw layout";
        } else if (!ram || !col) {
            err = "Out of memory";
        } else if (!tok_ta(js, t, n, key(js, t, n, cp, "ram"), ram, 65536) ||
                   !tok_ta(js, t, n, key(js, t, n, hw, "block"), blk, sizeof(blk)) ||
                   !tok_ta(js, t, n, key(js, t, n, hw, "ramUnderIo"), under, 1024) ||
                   !tok_ta(js, t, n, key(js, t, n, hw, "colorRam"), col, 1024)) {
            err = "Missing or short arrays";
        } else {
            memcpy(ram + 0xD800, under, 1024);
            for (int i = 0; i < TRX_BLOCK_WORDS; i++) {
                w[i] = blk[4 * i] | blk[4 * i + 1] << 8 | blk[4 * i + 2] << 16 | uint32_t(blk[4 * i + 3]) << 24;
            }
            x = int(tok_int(js, t, key(js, t, n, hw, "x")));
            if (x < 96 || x > 504) {
                err = "Bad snap position";
            }
        }
    }
    free(t);
    free(js);
    if (!err) {
        int e = trx_ring_load_state(ram, col, w, uint16_t(x));
        if (e) {
            err = trx_ring_error(e);
        }
    }
    free(ram);
    free(col);
    return err;
}
