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
#include "trx_io.h"
#include "c64.h"
#include "filemanager.h"
#include "itu.h"
#include "iomap.h"
#include "gitinfo.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include "jsmn.h"
#include "newfile.h"                 // nf_copy: no string precision in a format (the board's printf has none)

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

    // a string as JSON text: the quote, the backslash and the control characters escaped
    void jstr(const char *s)
    {
        char esc[8];
        for (const char *p = s; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '"' || c == '\\') {
                esc[0] = '\\';
                esc[1] = char(c);
                put(esc, 2);
            } else if (c < 0x20) {
                static const char hex[] = "0123456789abcdef";
                esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                esc[4] = hex[c >> 4]; esc[5] = hex[c & 15];
                put(esc, 6);
            } else {
                put((const char *)p, 1);
            }
        }
    }

    // a file as a typed array, streamed (3072 bytes a piece is a whole number of base64 groups): a cartridge or a disk image goes into the document without
    // a copy of it in memory. A read that fails leaves the array short; failed is set.
    void file_ta(File *f, uint32_t len)
    {
        str("{\"$ta\":\"Uint8Array\",\"b64\":\"");
        uint8_t piece[3072];
        uint32_t done = 0;
        while (done < len && !failed) {
            uint32_t want = len - done;
            if (want > sizeof(piece)) {
                want = sizeof(piece);
            }
            uint32_t got = 0;
            if (f->read(piece, want, &got) != FR_OK || got != want) {
                failed = true;
                break;
            }
            b64(piece, int(want));
            done += want;
        }
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

// ---- what the file says of the machine (T228 R20) ---------------------------------
static const char *const machine_names[6] = { "c64-pal", "c64-ntsc", "c64-pal60", "c64-ntsc50", "c64-pal60l", "c64-ntsc50l" };
static TrxC64reSrc g_src;
static bool g_src_on;

void trx_c64re_set_source(const TrxC64reSrc *src)
{
    if (src) {
        g_src = *src;
    }
    g_src_on = src != NULL;
}

static const char *machine_name(void)
{
    return (g_src_on && g_src.machine >= 0 && g_src.machine < 6) ? machine_names[g_src.machine] : machine_names[0];
}

#define TRX_CART_RAM_BYTES 65536
#define W_GLUE         0
#define W_CART         26
#define GLUE_CART      0x08

// The media of the manifest: media0 the disk image, media1 (or media0 without a disk) the cartridge, each hashed here and streamed into mediaPayloads.
struct MediaFile {
    const char *path;
    const char *role;
    char name[80];
    char format[8];
    uint32_t size;
    uint8_t digest[32];
};

static bool media_probe(MediaFile *m, const char *path, const char *role, const char *name)
{
    m->path = path;
    m->role = role;
    const char *base = name ? name : path;
    const char *sl = strrchr(base, '/');
    strncpy(m->name, sl ? sl + 1 : base, sizeof(m->name) - 1);
    m->name[sizeof(m->name) - 1] = 0;
    const char *dot = strrchr(m->name, '.');
    m->format[0] = 0;
    for (int i = 0; dot && dot[1 + i] && i < int(sizeof(m->format)) - 1; i++) {
        char c = dot[1 + i];
        m->format[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        m->format[i + 1] = 0;
    }
    File *f = NULL;
    if (FileManager::getFileManager()->fopen(path, FA_READ, &f) != FR_OK) {
        return false;
    }
    m->size = uint32_t(f->get_size());
    Sha256 sh;
    sha_init(&sh);
    uint8_t piece[3072];
    uint32_t left = m->size;
    bool ok = m->size != 0;
    while (ok && left) {
        uint32_t want = left > sizeof(piece) ? uint32_t(sizeof(piece)) : left;
        uint32_t got = 0;
        if (f->read(piece, want, &got) != FR_OK || got != want) {
            ok = false;
            break;
        }
        sha_add(&sh, piece, want);
        left -= want;
    }
    sha_final(&sh, m->digest);
    FileManager::getFileManager()->fclose(f);
    return ok;
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
    g.str(",\"alarmsMaincpu\":[],");
    g.str("\"cartBytes\":null");
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

// ---- drive A's own state (TRX64-Ultimate io/c64/trx_drive.cc, T66 part 8) ---------------------------------------------------------------------------
// Way B, no gateware: the drive's RAM is in DDR2 and read there; its CPU's registers and both 6522s are not on a bus the app reaches, so a stub of 0x90
// bytes runs on the drive's own CPU in the free part of the stack page, writes them into memory the app reads, and puts the drive back where it was. The
// drive is held (CMD_DRV_PAUSE) and moved on with CMD_DRV_STEP (drv_dbg.v), so the C64 may run, be paused or frozen. Limits as there: reading a 6522
// clears interrupt flags, the timers run during the stub's cycles, the T2 latch low is not readable.
#ifndef CAPS_DBG
#define CAPS_DBG       0x08
#endif
#define P3(o)          (TRX_IO_BASE + 0x3000 + (o))
#define DRV_CMD        P3(0x000)
#define DRV_ARG        P3(0x004)
#define DRV_STAT       P3(0x0C4)
#define DRV_CYCLES     P3(0x0C8)
#define CMD_DRV_PAUSE  0x10
#define CMD_DRV_RUN    0x11
#define CMD_DRV_STEP   0x12
#define ST_HALTED      0x01
#define ST_STEPPING    0x04
#define DRIVE_IMAGE    0x0EE0000u   // drive_unit.vhd g_ram_base of drive A
#define STUB_BASE      0x0100
#define STUB_SIZE      0x90
#define STUB_NEED      (STUB_SIZE + 4)
#define TRX_DRIVE_RAM  2048

struct TrxDriveState {
    bool     regs;                  // the stub ran: cpu and via* are there
    uint8_t  a, x, y, sp, p;
    uint16_t pc;
    uint8_t  via1[16], via2[16];    // $1800-$180F and $1C00-$1C0F as read
    uint32_t cycles;
    uint8_t  ram[TRX_DRIVE_RAM];
};

static inline uint8_t dpeek(uint16_t a) { return *(volatile uint8_t *)(DRIVE_IMAGE + (a < 0x1800 ? (a & 0x07FFu) : a)); }
static inline void dpoke(uint16_t a, uint8_t v) { *(volatile uint8_t *)(DRIVE_IMAGE + (a < 0x1800 ? (a & 0x07FFu) : a)) = v; }
static void dfence(void) { __asm__ volatile("fence" ::: "memory"); }

static bool drv_wait(uint32_t mask, uint32_t want, int ms)
{
    uint16_t t0 = getMsTimer();
    while (uint16_t(getMsTimer() - t0) < uint16_t(ms)) {
        if ((trx_io_read32(DRV_STAT) & mask) == want) {
            return true;
        }
    }
    return (trx_io_read32(DRV_STAT) & mask) == want;
}
static bool drv_hold(void)
{
    trx_io_write32(DRV_CMD, CMD_DRV_PAUSE);
    return drv_wait(ST_HALTED, ST_HALTED, 50);
}
static void drv_free(void) { trx_io_write32(DRV_CMD, CMD_DRV_RUN); }
static bool drv_step(int n)
{
    trx_io_write32(DRV_ARG, uint32_t(n));
    trx_io_write32(DRV_CMD, CMD_DRV_STEP);
    return drv_wait(ST_STEPPING, 0, 200);
}
static uint16_t drv_pc(void) { return uint16_t(trx_io_read32(DRV_STAT) >> 16); }

// The stubs are assembled by hand, as there, 0x90 bytes each: code first, then data. The save stub: 68 A, 69 X, 6A Y, 6B S, 6C P, 6D MAGIC, 6E FLAG,
// 6F MAGIC2, 70..7F VIA 1, 80..8F VIA 2.
#define SV_A      0x68
#define SV_X      0x69
#define SV_Y      0x6A
#define SV_S      0x6B
#define SV_P      0x6C
#define SV_MAGIC  0x6D
#define SV_FLAG   0x6E
#define SV_MAGIC2 0x6F
#define SV_V1     0x70
#define SV_V2     0x80
#define SV_MARK   0x5A
#define SV_MARK2  0xA5
static const uint8_t save_stub[STUB_SIZE] = {
    /* 00 */ 0x08, 0x8D, SV_A, 0x01, 0x68, 0x8D, SV_P, 0x01, 0x78,
    /* 09 */ 0x8E, SV_X, 0x01, 0x8C, SV_Y, 0x01, 0xBA, 0x8E, SV_S, 0x01,
    /* 13 */ 0xAD, 0x0D, 0x18, 0x8D, SV_V1 + 13, 0x01, 0xAD, 0x0D, 0x1C, 0x8D, SV_V2 + 13, 0x01,
    /* 1F */ 0xA2, 0x00, 0xE0, 0x0D, 0xF0, 0x0C, 0xBD, 0x00, 0x18, 0x9D, SV_V1, 0x01, 0xBD, 0x00, 0x1C, 0x9D, SV_V2, 0x01,
    /* 31 */ 0xE8, 0xE0, 0x10, 0xD0, 0xEB,
    /* 36 */ 0xA9, SV_MARK, 0x8D, SV_MAGIC, 0x01,
    /* 3B */ 0xAD, SV_FLAG, 0x01, 0xF0, 0xFB,
    /* 40 */ 0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x00, 0x8D, 0x00, 0x00,
    /* 4F */ 0xA9, SV_MARK2, 0x8D, SV_MAGIC2, 0x01,
    /* 54 */ 0xAD, SV_P, 0x01, 0x48, 0xAE, SV_X, 0x01, 0xAC, SV_Y, 0x01, 0xAD, SV_A, 0x01, 0x28,
    /* 62 */ 0x4C, 0x00, 0x00
};
#define SVP_B0     0x41
#define SVP_A0     0x43
#define SVP_B1     0x46
#define SVP_A1     0x48
#define SVP_B2     0x4B
#define SVP_A2     0x4D
#define SVP_JMP    0x63
// The load stub: 6D MAGIC, 70..7F VIA 1, 80..8F VIA 2.
#define LD_MAGIC   0x6D
#define LD_V1      0x70
#define LD_V2      0x80
static const uint8_t load_stub[STUB_SIZE] = {
    /* 00 */ 0xA9, 0x7F, 0x8D, 0x0E, 0x18, 0x8D, 0x0E, 0x1C,
    /* 08 */ 0xAD, LD_V1 + 11, 0x01, 0x8D, 0x0B, 0x18, 0xAD, LD_V1 + 12, 0x01, 0x8D, 0x0C, 0x18,
    /* 14 */ 0xAD, LD_V2 + 11, 0x01, 0x8D, 0x0B, 0x1C, 0xAD, LD_V2 + 12, 0x01, 0x8D, 0x0C, 0x1C,
    /* 20 */ 0xA2, 0x00, 0xE0, 0x0D, 0xF0, 0x0C, 0xBD, LD_V1, 0x01, 0x9D, 0x00, 0x18, 0xBD, LD_V2, 0x01, 0x9D, 0x00, 0x1C,
    /* 32 */ 0xE8, 0xE0, 0x10, 0xD0, 0xEB,
    /* 37 */ 0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x00, 0x8D, 0x00, 0x00,
    /* 46 */ 0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x00, 0x8D, 0x00, 0x00, 0xA9, 0x00, 0x8D, 0x00, 0x00,
    /* 55 */ 0xA9, SV_MARK2, 0x8D, LD_MAGIC, 0x01,
    /* 5A */ 0xA2, 0x00, 0x9A, 0xA9, 0x00, 0x48, 0xA9, 0x00, 0x48, 0xA9, 0x00, 0x48, 0xA2, 0x00, 0xA0, 0x00, 0xA9, 0x00, 0x40
};
#define LDP_SLOT0  0x37
#define LDP_SLOT_N 6
#define LDP_SP     0x5B
#define LDP_PCH    0x5E
#define LDP_PCL    0x61
#define LDP_P      0x64
#define LDP_X      0x67
#define LDP_Y      0x69
#define LDP_A      0x6B

// Opcode lengths and the opcodes that have exactly one successor (FW io/c64/trx_6502.h).
static const uint8_t drv_op_len[256] = {
    1, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    3, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    1, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    1, 2, 1, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 2, 3, 3, 3, 3,  2, 2, 1, 2, 2, 2, 2, 2, 1, 3, 1, 3, 3, 3, 3, 3,
};
static bool op_falls_through(uint8_t op)
{
    switch (op) {
    case 0x00: case 0x20: case 0x40: case 0x4C: case 0x6C: case 0x60:
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0:
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52: case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
        return false;
    default:
        return true;
    }
}

static uint16_t find_patch(uint16_t *at_dpc, uint8_t *dpc_len)
{
    for (int tries = 0; tries < 400; tries++) {
        uint16_t pc = drv_pc();
        if (pc < 0x0800) {
            uint8_t op = dpeek(pc);
            uint8_t len = drv_op_len[op];
            uint16_t next = uint16_t(pc + len);
            if (op_falls_through(op) && next + 3 <= 0x0800) {
                *at_dpc = pc;
                *dpc_len = len;
                return next;
            }
        }
        if (!drv_step(1)) {
            return 0;
        }
    }
    return 0;
}

// Is there a drive whose state can be read at all (the bitstream has the debug page and drive A is powered)?
static bool drive_present(void)
{
    return (trx_io_caps() & CAPS_DBG) && g_src_on && g_src.drive_type && g_src.drive_power;
}

static const char *drive_read(TrxDriveState *st)
{
    memset(st, 0, sizeof(*st));
    if (!drive_present()) {
        return "Drive A is not powered or this bitstream has no debug page";
    }
    for (int a = 0; a < TRX_DRIVE_RAM; a++) {
        st->ram[a] = dpeek(uint16_t(a));
    }
    st->cycles = trx_io_read32(DRV_CYCLES);
    if (!drv_hold()) {
        return "Drive A did not stop";
    }
    uint16_t dpc = 0, next = 0;
    uint8_t len = 0;
    next = find_patch(&dpc, &len);
    if (!next) {
        drv_free();
        printf("[DRIVE] drive A runs in its ROM, not in its own RAM: the file carries its RAM, not its CPU and VIA registers\n");
        return NULL;                // not an error: st->regs stays false
    }
    uint8_t page[256], back[3];
    for (int i = 0; i < 256; i++) {
        page[i] = dpeek(uint16_t(STUB_BASE + i));
    }
    for (int i = 0; i < 3; i++) {
        back[i] = dpeek(uint16_t(next + i));
    }
    uint8_t stub[STUB_SIZE];
    memcpy(stub, save_stub, STUB_SIZE);
    stub[SVP_B0] = back[0];
    stub[SVP_B1] = back[1];
    stub[SVP_B2] = back[2];
    stub[SVP_A0] = uint8_t(next);
    stub[SVP_A0 + 1] = uint8_t(next >> 8);
    stub[SVP_A1] = uint8_t(next + 1);
    stub[SVP_A1 + 1] = uint8_t((next + 1) >> 8);
    stub[SVP_A2] = uint8_t(next + 2);
    stub[SVP_A2 + 1] = uint8_t((next + 2) >> 8);
    stub[SVP_JMP] = uint8_t(next);
    stub[SVP_JMP + 1] = uint8_t(next >> 8);
    for (int i = 0; i < STUB_SIZE; i++) {
        dpoke(uint16_t(STUB_BASE + i), stub[i]);
    }
    dpoke(next, 0x4C);
    dpoke(uint16_t(next + 1), uint8_t(STUB_BASE));
    dpoke(uint16_t(next + 2), uint8_t(STUB_BASE >> 8));
    dfence();
    const char *err = NULL;
    bool got = false;
    for (int i = 0; i < 40 && !got; i++) {
        if (!drv_step(200)) {
            err = "Drive A did not step";
            break;
        }
        got = dpeek(STUB_BASE + SV_MAGIC) == SV_MARK;
    }
    if (!err && !got) {
        err = "The stub on drive A did not finish";
    }
    if (!err) {
        st->a = dpeek(STUB_BASE + SV_A);
        st->x = dpeek(STUB_BASE + SV_X);
        st->y = dpeek(STUB_BASE + SV_Y);
        st->sp = dpeek(STUB_BASE + SV_S);
        st->p = dpeek(STUB_BASE + SV_P);
        st->pc = next;
        for (int i = 0; i < 16; i++) {
            st->via1[i] = dpeek(uint16_t(STUB_BASE + SV_V1 + i));
            st->via2[i] = dpeek(uint16_t(STUB_BASE + SV_V2 + i));
        }
        st->cycles = trx_io_read32(DRV_CYCLES);
        st->regs = true;
        if (st->sp < STUB_NEED) {
            printf("[DRIVE] drive A's stack pointer was $%02X, below the stub's %d bytes: its stack was written over and is put back, but the drive may have lost data\n",
                   st->sp, STUB_SIZE);
        }
    }
    dpoke(STUB_BASE + SV_FLAG, 1);
    dfence();
    bool back_out = false;
    for (int i = 0; i < 20 && !back_out; i++) {
        if (!drv_step(100)) {
            break;
        }
        back_out = dpeek(STUB_BASE + SV_MAGIC2) == SV_MARK2;
    }
    for (int i = 0; i < 3; i++) {
        dpoke(uint16_t(next + i), back[i]);
    }
    for (int i = 0; i < 256; i++) {
        dpoke(uint16_t(STUB_BASE + i), page[i]);
    }
    dfence();
    drv_free();
    if (!back_out && !err) {
        err = "Drive A did not come back out of the stub";
    }
    if (err) {
        st->regs = false;
    }
    return err;
}

static const char *drive_write(const TrxDriveState *st)
{
    if (!(trx_io_caps() & CAPS_DBG)) {
        return "This bitstream has no debug page, so drive A cannot be written";
    }
    if (!g_src_on || !g_src.drive_type || !g_src.drive_power) {
        return "Drive A is not powered";
    }
    if (!drv_hold()) {
        return "Drive A did not stop";
    }
    uint16_t dpc = 0, next = 0;
    uint8_t len = 0;
    if (st->regs) {
        next = find_patch(&dpc, &len);
        if (!next) {
            drv_free();
            return "Drive A runs in its ROM: its registers cannot be put back (its RAM was not touched)";
        }
        if (st->sp < STUB_NEED) {
            drv_free();
            return "The file's drive stack pointer is too low for the stub";
        }
    }
    uint8_t keep[3];
    for (int i = 0; i < len; i++) {
        keep[i] = dpeek(uint16_t(dpc + i));
    }
    for (int a = 0; a < TRX_DRIVE_RAM; a++) {
        dpoke(uint16_t(a), st->ram[a]);
    }
    if (!st->regs) {
        dfence();
        drv_free();
        return NULL;
    }
    for (int i = 0; i < len; i++) {
        dpoke(uint16_t(dpc + i), keep[i]);
    }
    uint8_t stub[STUB_SIZE];
    memcpy(stub, load_stub, STUB_SIZE);
    for (int i = 0; i < 16; i++) {
        stub[LD_V1 + i] = st->via1[i];
        stub[LD_V2 + i] = st->via2[i];
    }
    struct {
        uint16_t addr;
        uint8_t val;
    } fix[LDP_SLOT_N];
    int nf = 0;
    for (int i = 0; i < len && nf < LDP_SLOT_N; i++) {
        fix[nf].addr = uint16_t(dpc + i);
        fix[nf].val = st->ram[(dpc + i) & 0x07FF];
        nf++;
    }
    for (int i = 0; i < 3 && nf < LDP_SLOT_N; i++) {
        fix[nf].addr = uint16_t(next + i);
        fix[nf].val = st->ram[(next + i) & 0x07FF];
        nf++;
    }
    while (nf < LDP_SLOT_N) {
        fix[nf] = fix[0];
        nf++;
    }
    for (int i = 0; i < LDP_SLOT_N; i++) {
        int o = LDP_SLOT0 + 5 * i;
        stub[o + 1] = fix[i].val;
        stub[o + 3] = uint8_t(fix[i].addr);
        stub[o + 4] = uint8_t(fix[i].addr >> 8);
    }
    stub[LDP_SP] = st->sp;
    stub[LDP_PCH] = uint8_t(st->pc >> 8);
    stub[LDP_PCL] = uint8_t(st->pc);
    stub[LDP_P] = st->p;
    stub[LDP_X] = st->x;
    stub[LDP_Y] = st->y;
    stub[LDP_A] = st->a;
    stub[LD_MAGIC] = 0;
    for (int i = 0; i < STUB_SIZE; i++) {
        dpoke(uint16_t(STUB_BASE + i), stub[i]);
    }
    dpoke(next, 0x4C);
    dpoke(uint16_t(next + 1), uint8_t(STUB_BASE));
    dpoke(uint16_t(next + 2), uint8_t(STUB_BASE >> 8));
    dfence();
    const char *err = NULL;
    bool done = false;
    for (int i = 0; i < 40 && !done; i++) {
        if (!drv_step(200)) {
            err = "Drive A did not step";
            break;
        }
        done = dpeek(STUB_BASE + LD_MAGIC) == SV_MARK2;
    }
    if (!err && !done) {
        err = "The stub on drive A did not finish";
    }
    if (!err && !drv_step(4)) {
        err = "Drive A did not step";
    }
    for (int i = 0; i < STUB_SIZE; i++) {
        dpoke(uint16_t(STUB_BASE + i), st->ram[(STUB_BASE + i) & 0x07FF]);
    }
    dfence();
    drv_free();
    return err;
}

// ---- format 2: state and medium, binary sections, streamed (FW io/c64/trx_c64re.cc, U8) ---------------------------------------------------------------
//   41 byte head ("C64RESNP", the format byte, SHA-256 of the body) | u32 header_len | JSON header (provenance only) | padded to 4 | u16 sections | u16 0 |
//   sections x { u8 id, u8 flags, u16 0, u32 len } | the payloads, each padded to 4. An id a reader does not know is skipped by its length.
#define C64RE_FORMAT    2
#define SEC_BLOCK       1
#define SEC_RAM         2
#define SEC_COLOR       3
#define SEC_CARTRAM     4
#define SEC_CARTROM     5
#define SEC_DRIVE       6
#define SEC_EEPROM      7
#define SEC_SLOTS       9
#define SEC_ENTRY       8
#define TRX_EEPROM_BYTES 2048
#define HDR_JSON_MAX    768
#define RAW_CHUNK       16384
#define BLOCK_SEC_BYTES (TRX_BLOCK_WORDS * 4 + 8)

// Writes straight through and hashes on the way; nothing is held back.
class Raw {
    File *f;
public:
    Sha256 sha;
    bool failed;
    uint32_t written;

    Raw(File *file) : f(file), failed(false), written(0) { sha_init(&sha); }
    void put(const void *p, uint32_t len)
    {
        const uint8_t *b = (const uint8_t *)p;
        while (!failed && len) {
            uint32_t want = len > RAW_CHUNK ? RAW_CHUNK : len;
            uint32_t tr = 0;
            sha_add(&sha, b, want);
            if (f->write(b, want, &tr) != FR_OK || tr != want) {
                failed = true;
                return;
            }
            written += want;
            b += want;
            len -= want;
        }
    }
    void u16(uint16_t v) { uint8_t b[2] = { uint8_t(v), uint8_t(v >> 8) }; put(b, 2); }
    void u32(uint32_t v) { uint8_t b[4] = { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) }; put(b, 4); }
    void pad(uint32_t len)
    {
        static const uint8_t zero[4] = { 0, 0, 0, 0 };
        if (len & 3) {
            put(zero, 4 - (len & 3));
        }
    }
};

// The block section: the chip block as 32 bit words, the snap position, and the turbo word when this machine has one (T29), laid out by hand.
static uint32_t block_section(uint8_t *dst, const TrxFrame &f)
{
    uint32_t n = 0;
    for (int i = 0; i < TRX_BLOCK_WORDS; i++) {
        uint32_t w = f.w[i];
        dst[n++] = uint8_t(w);
        dst[n++] = uint8_t(w >> 8);
        dst[n++] = uint8_t(w >> 16);
        dst[n++] = uint8_t(w >> 24);
    }
    dst[n++] = uint8_t(f.x);
    dst[n++] = uint8_t(f.x >> 8);
    uint16_t flags = 0;
    uint32_t pack = 0;
    if (ioRead8(TRX_CAPS2) & TRX_CAPS2_TURBO) {
        uint32_t w1 = trx_io_read32(TRX_TURBO_W1), w2 = trx_io_read32(TRX_TURBO_W2);
        uint32_t en = (w1 >> 8) & 7, mi = w1 & 15, d031 = (w1 >> 24) & 0xFF;
        uint32_t d030 = (w1 >> 11) & 1, hold = w2 & 0x7FFFFF, stalls = (w1 & 0x80) ? 1 : 0;
        pack = ((hold >> 8) << 17) | (d031 << 9) | (d030 << 8) | (en << 5) | (stalls << 4) | mi;
        flags |= 1;
    }
    dst[n++] = uint8_t(flags);
    dst[n++] = uint8_t(flags >> 8);
    dst[n++] = uint8_t(pack);
    dst[n++] = uint8_t(pack >> 8);
    dst[n++] = uint8_t(pack >> 16);
    dst[n++] = uint8_t(pack >> 24);
    return n;
}

// The 64 bit cycle count as decimal text, by hand (the firmware's printf has no 64 bit conversion).
static int u64dec(char *dst, int cap, uint64_t v)
{
    char tmp[24];
    int k = 0;
    do {
        tmp[k++] = char('0' + int(v % 10));
        v /= 10;
    } while (v);
    int n = 0;
    while (k && n < cap - 1) {
        dst[n++] = tmp[--k];
    }
    if (n < cap) {
        dst[n] = 0;
    }
    return n;
}

// A string into the header with the characters that would break the JSON left out.
static int json_str(char *dst, int cap, const char *src)
{
    int n = 0;
    for (const char *q = src; *q && n < cap - 1; q++) {
        if (*q != '"' && *q != '\\' && uint8_t(*q) >= 0x20) {
            dst[n++] = *q;
        }
    }
    if (n < cap) {
        dst[n] = 0;
    }
    return n;
}

extern "C" uint32_t get_fattime(void);
extern "C" bool rtc_time_valid(void);
#define CMD_IF_SLOT_ENABLE     *((volatile uint8_t *)(CMD_IF_BASE + 0x1))     // whether the UCI was on (FW io/c64/trx_c64re.cc header_json, U9 D1)

static int header_json(char *dst, int cap, const TrxFrame &f, uint32_t rom_len)
{
    const bool cart = (f.w[W_GLUE] & GLUE_CART) && g_src_on && g_src.cart_src && g_src.cart_src[0];
    int n = snprintf(dst, cap,
                     "{\"kind\":\"c64re\",\"version\":%d,\"writer\":\"trx64u\",\"appVersion\":\"%s\""
                     ",\"machine\":\"%s\",\"bitstreamId\":%u,\"caps\":%u,\"caps2\":%u,\"blockWords\":%d",
                     C64RE_FORMAT, APP_VERSION_STRING, machine_name(), (unsigned)trx_io_read32(TRX_ID), (unsigned)ioRead8(TRX_CAPS),
                     (unsigned)ioRead8(TRX_CAPS2), TRX_BLOCK_WORDS);
    if (rtc_time_valid()) {                       // the FAT time: bits 31-25 years since 1980, month, day, hour, minute, seconds / 2
        uint32_t ft = get_fattime();
        int y = int(ft >> 25) + 1980;
        if (y >= 2024) {
            n += snprintf(dst + n, cap - n, ",\"createdAt\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\"", y, int((ft >> 21) & 15), int((ft >> 16) & 31),
                          int((ft >> 11) & 31), int((ft >> 5) & 63), int((ft & 31) * 2));
        }
    }
    n += snprintf(dst + n, cap - n, ",\"frame\":%u,\"cycles\":", (unsigned)f.marker);
    n += u64dec(dst + n, cap - n, f.cycles);
    n += snprintf(dst + n, cap - n, ",\"pc\":%d", int((f.w[W_CPU + 1] & 0xFFFF) - PC_ADJ) & 0xFFFF);
    if (cart) {
        n += snprintf(dst + n, cap - n, ",\"cartridgeType\":%u,\"source\":\"", (unsigned)g_src.cart_type);
        n += json_str(dst + n, cap - n, g_src.cart_src);
        n += snprintf(dst + n, cap - n, "\",\"mediumBytes\":%u", (unsigned)rom_len);
    }
    n += snprintf(dst + n, cap - n, ",\"machineState\":{");
    if (cart) {
        n += snprintf(dst + n, cap - n, "\"cartridge\":{\"type\":%u,\"bytes\":%u,\"source\":\"", (unsigned)g_src.cart_type, (unsigned)rom_len);
        n += json_str(dst + n, cap - n, g_src.cart_src);
        n += snprintf(dst + n, cap - n, "\"},");
    }
    if (g_src_on && g_src.drive_type && g_src.disk_path && g_src.disk_path[0]) {
        n += snprintf(dst + n, cap - n, "\"drive\":{\"image\":\"");
        n += json_str(dst + n, cap - n, g_src.disk_path);
        n += snprintf(dst + n, cap - n, "\",\"mode\":\"%s\"},", g_src.disk_mode == 2 ? "unlinked" : g_src.disk_mode == 1 ? "ro" : "rw");
    }
    n += snprintf(dst + n, cap - n, "\"settings\":{\"uci\":%s,\"reu\":%u,\"reuSize\":%u,\"sampler\":%s}}}",
                  (getFpgaCapabilities() & CAPAB_COMMAND_INTF) && CMD_IF_SLOT_ENABLE ? "true" : "false",
                  (unsigned)(C64_REU_ENABLE ? 1 : 0), (unsigned)C64_REU_SIZE, C64_SAMPLER_ENABLE ? "true" : "false");
    return n;
}

static const char *g_missed = "";

const char *trx_c64re_missed(void)
{
    return g_missed;
}

const char *trx_c64re_dump(const char *path, int idx, uint32_t *bytes)
{
    return trx_c64re_dump_ex(path, idx, bytes, NULL, NULL);
}

const char *trx_c64re_dump_ex(const char *path, int idx, uint32_t *bytes, trx_progress_fn progress, void *pctx)
{
    TrxFrame *f = (TrxFrame *)malloc(sizeof(TrxFrame));
    uint8_t *ram = (uint8_t *)malloc(65536);
    uint8_t *col = (uint8_t *)malloc(1024);
    uint8_t *blk = (uint8_t *)malloc(BLOCK_SEC_BYTES);
    char *hdr = (char *)malloc(HDR_JSON_MAX);
    uint8_t *chunk = (uint8_t *)malloc(RAW_CHUNK);
    const char *err = NULL;
    File *file = NULL;
    uint8_t *cart = NULL;
    TrxDriveState *drive = NULL;
    uint32_t rom_len = 0;
    *bytes = 0;

    if (!f || !ram || !col || !blk || !hdr || !chunk) {
        err = "Out of memory";
    } else if (!trx_ring_frame(idx, f)) {
        err = "No such frame";
    } else if (!f->block) {
        err = "The frame has no chip block";
    } else {
        // with a cartridge on, its RAM window comes out of the same pause
        if (f->w[W_GLUE] & GLUE_CART) {
            cart = (uint8_t *)malloc(TRX_CART_RAM_BYTES);
        }
        if (int e = trx_ring_materialize(idx, ram, col, cart)) {
            err = trx_ring_error(e);
        }
    }
    // a dump this build could not load back is not written at all: the gate is the cartridge, because that is where the state reaches outside the chip block
    // (FW trx_c64re_dump_ex, U9 D0); one list answers here and in replay_cart()
    if (!err && (f->w[W_GLUE] & GLUE_CART)) {
        const char *cname = NULL;
        if (!trx_ring_cart_state_ok(C64_CARTRIDGE_TYPE, &cname)) {
            static char refused[96];
            snprintf(refused, sizeof(refused), "A state of this cartridge (type %02x) cannot be loaded back yet", (int)C64_CARTRIDGE_TYPE);
            err = refused;
        } else if (trx_ring_cart_has_eeprom(C64_CARTRIDGE_TYPE)) {
            // U9 D3a: GMod2's saved game is in the 93C86, outside the ROM window: without it the file would hold a cartridge whose save it cannot put back
            if (!(getFpgaCapabilities() & CAPAB_EEPROM)) {
                err = "This bitstream has no EEPROM: a GMod2 state cannot be saved";
            } else if (!(g_src_on && g_src.eeprom)) {
                err = "The EEPROM could not be read: a GMod2 state cannot be saved";
            }
        }
    }
    // the medium: the cartridge image as it stands now, read straight out of DDR2 when it is written (not the .crt on the stick: the game may have
    // programmed the flash since)
    if (!err && (f->w[W_GLUE] & GLUE_CART) && g_src_on && g_src.rom_read) {
        rom_len = g_src.rom_bytes;
    }
    // drive A, through the stub on its own CPU. A drive that cannot be entered gives its RAM only, and one that is off gives nothing; neither stops the
    // file from being written.
    if (!err && drive_present()) {
        drive = (TrxDriveState *)malloc(sizeof(TrxDriveState));
        const char *derr = drive ? drive_read(drive) : "Out of memory";
        if (derr) {
            printf("[C64RE] drive A is not in the file: %s\n", derr);
            free(drive);
            drive = NULL;
        }
    }

    struct SecOut {
        uint8_t id;
        const uint8_t *p;      // NULL: the cartridge image, streamed through rom_read
        uint32_t len;
    } sec[SEC_SLOTS];
    int ns = 0;
    int hl = 0;
    if (!err) {
        hl = header_json(hdr, HDR_JSON_MAX, *f, rom_len);
        sec[ns].id = SEC_BLOCK;   sec[ns].p = blk;  sec[ns].len = block_section(blk, *f);     ns++;
        sec[ns].id = SEC_RAM;     sec[ns].p = ram;  sec[ns].len = 65536;                      ns++;
        sec[ns].id = SEC_COLOR;   sec[ns].p = col;  sec[ns].len = 1024;                       ns++;
        if (cart) {
            sec[ns].id = SEC_CARTRAM; sec[ns].p = cart; sec[ns].len = TRX_CART_RAM_BYTES;     ns++;
        }
        if (rom_len) {
            sec[ns].id = SEC_CARTROM; sec[ns].p = NULL; sec[ns].len = rom_len;                ns++;
        }
        if (drive) {
            sec[ns].id = SEC_DRIVE; sec[ns].p = (const uint8_t *)drive; sec[ns].len = sizeof(TrxDriveState); ns++;
        }
        if (g_src_on && g_src.eeprom && (f->w[W_GLUE] & GLUE_CART)) {
            sec[ns].id = SEC_EEPROM; sec[ns].p = g_src.eeprom; sec[ns].len = TRX_EEPROM_BYTES; ns++;
        }
        err = open_out(path, &file);
    }
    if (!err) {
        uint8_t head[41];
        memset(head, 0, sizeof(head));
        memcpy(head, "C64RESNP", 8);
        head[8] = C64RE_FORMAT;
        uint32_t tr = 0;
        if (file->write(head, sizeof(head), &tr) != FR_OK || tr != sizeof(head)) {
            err = "The file could not be written";
        }
        Raw w(file);
        int steps = ns, step = 0;
        for (int i = 0; i < ns; i++) {
            steps += int(sec[i].len / RAW_CHUNK);
        }
        if (progress) {
            progress(0, steps, pctx);
        }
        if (!err) {
            w.u32(uint32_t(hl));
            w.put(hdr, uint32_t(hl));
            w.pad(uint32_t(hl));
            w.u16(uint16_t(ns));
            w.u16(0);
            for (int i = 0; i < ns; i++) {
                uint8_t e[SEC_ENTRY] = { sec[i].id, 0, 0, 0, uint8_t(sec[i].len), uint8_t(sec[i].len >> 8), uint8_t(sec[i].len >> 16), uint8_t(sec[i].len >> 24) };
                w.put(e, SEC_ENTRY);
            }
            for (int i = 0; i < ns && !w.failed; i++) {
                for (uint32_t off = 0; off < sec[i].len && !w.failed; off += RAW_CHUNK) {
                    uint32_t want = sec[i].len - off > RAW_CHUNK ? RAW_CHUNK : sec[i].len - off;
                    if (sec[i].p) {
                        w.put(sec[i].p + off, want);
                    } else {
                        g_src.rom_read(g_src.rom_ctx, off, chunk, want);
                        w.put(chunk, want);
                    }
                    if (progress) {
                        progress(++step, steps, pctx);
                    }
                }
                w.pad(sec[i].len);
            }
            if (w.failed) {
                err = "The file could not be written";
            }
        }
        if (!err) {
            uint8_t digest[32];
            sha_final(&w.sha, digest);
            if (file->seek(9) != FR_OK || file->write(digest, 32, &tr) != FR_OK || tr != 32) {
                err = "The file could not be written";
            }
        }
        *bytes = 41 + w.written;
        FileManager::getFileManager()->fclose(file);
        if (err) {
            FileManager::getFileManager()->delete_file(path);        // no half file is left behind
            *bytes = 0;
        } else {
            printf("[C64RE] .c64re format %d: %u bytes, %d sections, medium %u\n", C64RE_FORMAT, (unsigned)*bytes, ns, (unsigned)rom_len);
        }
    }
    free(f);
    free(ram);
    free(col);
    free(blk);
    free(hdr);
    free(chunk);
    free(cart);
    free(drive);
    return err;
}

// ---- the dump with no ring (FW apps/c64re/src/c64re.c: "Save .C64RE Dump") ----------------------------------------------------------------------------
// The machine as it stands, read through the DMA window while the menu has it frozen: the 64 KB of RAM (the window's RAM-only mode, under the I/O too), the
// VIC registers and the colour RAM (a bus cycle, only while the machine is held), the CPU port from RAM $00/$01; format 1, the media that are in the machine
// (the cartridge's .crt and drive A's image) inside it. What the ring's chip block carries (the 6510's registers, the CIA and VIC internals) is not on any
// bus here and is left out; every node is optional on the reading side, so the dump opens as a machine to look at.
#define DMA_MEMONLY    (IOBASE + 0x180003)
#define DMA_WINDOW     (IOBASE + 0x50000)
#define DRV_STATUS_REG (TRX_IO_BASE + 0x3008)

const char *trx_c64re_dump_machine(const char *path, uint32_t *bytes)
{
    uint8_t *ram = (uint8_t *)malloc(65536);
    uint8_t *col = (uint8_t *)malloc(1024);
    const char *err = NULL;
    File *file = NULL;
    *bytes = 0;
    if (!ram || !col) {
        err = "Out of memory";
    }
    uint8_t vic[0x30];
    memset(vic, 0, sizeof(vic));
    if (!err) {
        volatile uint8_t *memonly = (volatile uint8_t *)DMA_MEMONLY;
        const volatile uint8_t *win = (const volatile uint8_t *)DMA_WINDOW;
        uint8_t old = *memonly;
        *memonly = 1;
        for (int a = 0; a < 65536; a++) {
            ram[a] = win[a];
        }
        uint32_t st = trx_io_read32(DRV_STATUS_REG);
        if ((st & 1) && (st & (0x02 | 0x20))) {                  // the gate is shut and a stop is in force: the window's D page is a bus cycle
            *memonly = 0;
            for (int r = 0; r < 0x30; r++) {
                vic[r] = win[0xD000 + r];
            }
            for (int a = 0; a < 1024; a++) {
                col[a] = win[0xD800 + a] & 0x0F;
            }
        } else {
            printf("[C64RE] the machine is not held: the VIC registers and the colour RAM are not in the dump\n");
            memset(col, 0, 1024);
        }
        *memonly = old;
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
        MediaFile mf[2];
        int nm = 0;
        if (g_src_on && g_src.cart_src && g_src.cart_src[0] && media_probe(&mf[nm], g_src.cart_src, "cartridge", NULL)) {
            nm++;
        }
        if (g_src_on && g_src.disk_path && g_src.disk_path[0] && media_probe(&mf[nm], g_src.disk_path, "drive8", NULL)) {
            nm++;
        }
        g.str("{\"mediaPayloads\":{");
        for (int i = 0; i < nm && !g.failed; i++) {
            File *mfile = NULL;
            g.num(i ? ",\"media" : "\"media", i);
            g.str("\":");
            if (FileManager::getFileManager()->fopen(mf[i].path, FA_READ, &mfile) == FR_OK) {
                g.file_ta(mfile, mf[i].size);
                FileManager::getFileManager()->fclose(mfile);
            } else {
                g.failed = true;
            }
        }
        g.str("},\"manifest\":{\"kind\":\"c64re-runtime-snapshot\",\"version\":1,\"createdAt\":\"1970-01-01T00:00:00Z\"");
        g.str(",\"machine\":{\"model\":\"");
        g.str(machine_name());
        g.str("\",\"runtimeVersion\":\"trx64u/c64re-dump\"}");
        g.str(",\"checkpoint\":{\"encoding\":\"ta-json-gz/1\",\"payloadRef\":\"checkpoint\",\"cycle\":0,\"pc\":0},\"media\":[");
        for (int i = 0; i < nm; i++) {
            static const char hex[] = "0123456789abcdef";
            char sha[65];
            for (int k = 0; k < 32; k++) {
                sha[2 * k] = hex[mf[i].digest[k] >> 4];
                sha[2 * k + 1] = hex[mf[i].digest[k] & 15];
            }
            sha[64] = 0;
            g.str(i ? ",{\"role\":\"" : "{\"role\":\"");
            g.str(mf[i].role);
            g.str("\",\"format\":\"");
            g.jstr(mf[i].format);
            g.str("\",\"sha256\":\"");
            g.str(sha);
            g.str("\",\"sourceName\":\"");
            g.jstr(mf[i].name);
            g.num("\",\"embeddedPayloadRef\":\"media", i);
            g.str("\"}");
        }
        g.str("]},\"checkpoint\":{\"schemaVersion\":1,\"ram\":");
        g.ta(ram, 65536);
        g.num(",\"cpuPortDirection\":", ram[0]);
        g.num(",\"cpuPortValue\":", ram[1]);
        g.str(",");
        uint32_t w[TRX_BLOCK_WORDS];                              // write_vic reads the registers out of the block's VIC words
        memset(w, 0, sizeof(w));
        for (int r = 0; r < 0x30; r++) {
            w[W_VIC + r / 4] |= uint32_t(vic[r]) << (8 * (r & 3));
        }
        write_vic(g, w, col, 0, 0);
        g.str("}}");
        uint8_t digest[32];
        if (!g.finish(digest)) {
            err = "The file could not be written";
        } else if (file->seek(9) != FR_OK || file->write(digest, 32, &tr) != FR_OK || tr != 32) {
            err = "The file could not be written";
        }
        *bytes = 41 + g.written;
        FileManager::getFileManager()->fclose(file);
        if (err) {
            FileManager::getFileManager()->delete_file(path);
            *bytes = 0;
        }
    }
    free(ram);
    free(col);
    return err;
}

// The loader of format 2: the whole file hashed once, then the sections it can put back are read at their offsets - the file is never held whole (a
// cartridge image is megabytes).
static inline uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

// The cartridge image goes into DDR2 from the file while the machine stands (TrxLoadMem::rom_put), piece by piece through the caller's rom_write.
struct RomJob {
    File *file;
    uint32_t off, len;
    uint8_t *chunk;
};
static RomJob g_rom;

static void rom_put_cb(void *)
{
    for (uint32_t k = 0; k < g_rom.len; k += RAW_CHUNK) {
        uint32_t want = g_rom.len - k > RAW_CHUNK ? RAW_CHUNK : g_rom.len - k, got = 0;
        if (g_rom.file->seek(g_rom.off + k) != FR_OK || g_rom.file->read(g_rom.chunk, want, &got) != FR_OK || got != want) {
            printf("[C64RE] the cartridge image could not be read back from the file\n");
            return;
        }
        g_src.rom_write(g_src.rom_ctx, k, g_rom.chunk, want);
    }
}

// What a load could not put back, named once each (FW note())
static void note_missed(char *missed, int cap, const char *what)
{
    int n = int(strlen(missed));
    if (n + int(strlen(what)) + 3 >= cap) {
        return;
    }
    if (n) {
        missed[n++] = ',';
        missed[n++] = ' ';
    }
    strcpy(missed + n, what);
}

static bool is_there(const char *path)
{
    File *f = NULL;
    if (FileManager::getFileManager()->fopen(path, FA_READ, &f) != FR_OK) {
        return false;
    }
    FileManager::getFileManager()->fclose(f);
    return true;
}

// the string value after pat (a `"key":"` prefix) in the header, up to its closing quote; false when the key is not there
static bool hdr_str(const char *hdr, const char *pat, char *out, int cap)
{
    const char *p = strstr(hdr, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    int k = 0;
    while (*p && *p != '"' && k < cap - 1) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[k++] = *p++;
    }
    out[k] = 0;
    return true;
}

#define CMD_IF_SLOT_BASE       *((volatile uint8_t *)(CMD_IF_BASE + 0x0))

// FW build_settings (D4): the registers, directly, for as long as this state runs; the setting values are not written, so nothing reaches the card
static void build_settings(const char *ms, char *missed, int cap)
{
    const char *se = strstr(ms, "\"settings\":{");
    if (!se) {
        return;
    }
    const bool uci = strstr(se, "\"uci\":true") != NULL;
    const bool sampler = strstr(se, "\"sampler\":true") != NULL;
    const char *rp = strstr(se, "\"reu\":");
    const char *sp = strstr(se, "\"reuSize\":");
    const int reu = rp ? atoi(rp + 6) : 0;
    const int reu_size = sp ? atoi(sp + 10) : 0;
    if (getFpgaCapabilities() & CAPAB_COMMAND_INTF) {
        CMD_IF_SLOT_ENABLE = uci ? 1 : 0;
        if (uci) {
            CMD_IF_SLOT_BASE = 0x47;                        // $DF1C
        }
    } else if (uci) {
        note_missed(missed, cap, "UCI");
    }
    if (getFpgaCapabilities() & CAPAB_SAMPLER) {
        C64_SAMPLER_ENABLE = sampler ? 1 : 0;
    } else if (sampler) {
        note_missed(missed, cap, "the sampler");
    }
    // the REU is off in every state a ring can produce (A08); this only puts back an "off" that the machine may currently contradict
    C64_REU_ENABLE = reu ? 1 : 0;
    C64_REU_SIZE = uint8_t(reu_size);
}

// FW build_drive: the image drive A had, mounted before the drive's own state goes in (a mount resets the drive and would overwrite it); the state decides,
// not what happens to be in the drive. A path that is gone is looked for under the same name beside the file being loaded (FW resolve_medium).
static char g_load_path[192];                      // the file being loaded: a medium that is gone is looked for beside it (FW resolve_medium)
static void build_drive(const char *ms, char *missed, int cap)
{
    char img[144], mode[12];
    if (!g_src_on || !g_src.drive_mount || !hdr_str(ms, "\"drive\":{\"image\":\"", img, sizeof(img)) || !img[0]) {
        return;
    }
    if (!hdr_str(ms, "\"mode\":\"", mode, sizeof(mode))) {
        mode[0] = 0;
    }
    char spec[200];
    if (!is_there(img)) {
        const char *base = strrchr(img, '/');
        const char *cut = strrchr(g_load_path, '/');
        char tmp[200];
        if (base && cut && int(cut - g_load_path) + int(strlen(base)) < int(sizeof(tmp))) {
            memcpy(tmp, g_load_path, cut - g_load_path);
            strcpy(tmp + (cut - g_load_path), base);
            if (is_there(tmp)) {
                printf("[C64RE] '%s' is not there; '%s' is used instead\n", img, tmp);
                snprintf(img, sizeof(img), "%s", tmp);
            }
        }
    }
    snprintf(spec, sizeof(spec), "%s%s", !strcmp(mode, "unlinked") ? "unlinked " : !strcmp(mode, "ro") ? "ro " : "", img);
    if (g_src.drive_mount(g_src.rom_ctx, spec) != 0) {
        printf("[C64RE] drive A: '%s' was not mounted\n", img);
        note_missed(missed, cap, "the disk in drive A");
    } else {
        printf("[C64RE] drive A: '%s' mounted\n", img);
    }
}

static const char *load_v2(File *file, uint32_t size)
{
    const char *err = NULL;
    uint8_t *chunk = (uint8_t *)malloc(RAW_CHUNK);
    uint8_t *ram = (uint8_t *)malloc(65536);
    uint8_t *col = (uint8_t *)malloc(1024);
    uint8_t *blk = (uint8_t *)malloc(BLOCK_SEC_BYTES);
    TrxDriveState *drive = NULL;
    static char missed[96];
    missed[0] = 0;
    g_missed = missed;
    if (!chunk || !ram || !col || !blk) {
        err = "Out of memory";
    }
    uint32_t tr = 0;
    uint8_t head[41];
    if (!err && (size < 41 + 8 || file->seek(0) != FR_OK || file->read(head, 41, &tr) != FR_OK || tr != 41)) {
        err = "The file could not be read";
    }
    if (!err) {
        Sha256 s;
        sha_init(&s);
        for (uint32_t left = size - 41; left && !err;) {
            uint32_t want = left > RAW_CHUNK ? RAW_CHUNK : left;
            if (file->read(chunk, want, &tr) != FR_OK || tr != want) {
                err = "The file could not be read";
                break;
            }
            sha_add(&s, chunk, want);
            left -= want;
        }
        uint8_t d[32];
        sha_final(&s, d);
        if (!err && memcmp(d, head + 9, 32)) {
            err = "Hash mismatch";
        }
    }
    uint32_t pos = 41;
    uint8_t w4[4];
    uint32_t hl = 0;
    if (!err && (file->seek(pos) != FR_OK || file->read(w4, 4, &tr) != FR_OK || tr != 4)) {
        err = "The file could not be read";
    }
    if (!err) {
        hl = le32(w4);
        pos += 4 + ((hl + 3) & ~3u);
        if (pos + 4 > size) {
            err = "Bad header length";
        }
    }
    // the header names the machine the state needs (FW build_cartridge reads machineState.cartridge): the type and the .crt it came from
    static char hdr[HDR_JSON_MAX + 1];
    hdr[0] = 0;
    if (!err && hl <= HDR_JSON_MAX && (file->seek(41 + 4) != FR_OK || file->read(hdr, hl, &tr) != FR_OK || tr != hl)) {
        err = "The file could not be read";
    }
    hdr[hl <= HDR_JSON_MAX ? hl : 0] = 0;
    const char *ms_at = strstr(hdr, "\"machineState\"");
    bool has_ms = ms_at != NULL;
    int machine = -1;                                   // the video standard the file is of (FW stage_v2: a name this build does not run refuses before anything moves)
    char mname[40];
    if (!err && hdr_str(hdr, "\"machine\":\"", mname, sizeof(mname))) {
        for (int i = 0; i < 6; i++) {
            if (!strcmp(mname, machine_names[i])) {
                machine = i;
            }
        }
        if (machine < 0) {
            static char msg[96];
            char mn[41];
            nf_copy(mn, sizeof(mn), mname, 40);
            snprintf(msg, sizeof(msg), "The file is for machine \"%s\", which this build does not run", mn);
            err = msg;
        }
    }
    int cart_type = 0;
    static char cart_src[160];
    cart_src[0] = 0;
    if (const char *cp = strstr(hdr, "\"cartridge\":{\"type\":")) {
        cart_type = atoi(cp + 20);
        if (const char *sp = strstr(cp, "\"source\":\"")) {
            sp += 10;
            int k = 0;
            while (*sp && *sp != '"' && k < int(sizeof(cart_src)) - 1) {
                cart_src[k++] = *sp++;
            }
            cart_src[k] = 0;
        }
    }
    uint8_t *cartram = NULL, *eeprom = NULL;
    uint32_t rom_off = 0, rom_len = 0;
    int ns = 0;
    uint8_t tab[SEC_SLOTS * 4 * SEC_ENTRY];
    if (!err && (file->seek(pos) != FR_OK || file->read(w4, 4, &tr) != FR_OK || tr != 4)) {
        err = "The file could not be read";
    }
    if (!err) {
        ns = w4[0] | w4[1] << 8;
        if (ns < 1 || ns > SEC_SLOTS * 4 || pos + 4 + uint32_t(ns) * SEC_ENTRY > size) {
            err = "Bad section table";
        } else if (file->read(tab, uint32_t(ns) * SEC_ENTRY, &tr) != FR_OK || tr != uint32_t(ns) * SEC_ENTRY) {
            err = "The file could not be read";
        }
    }
    bool have_block = false, have_ram = false, have_col = false;
    uint32_t off = pos + 4 + uint32_t(ns) * SEC_ENTRY;
    for (int i = 0; i < ns && !err; i++) {
        uint8_t id = tab[i * SEC_ENTRY];
        uint32_t len = le32(tab + i * SEC_ENTRY + 4);
        uint32_t padded = (len + 3) & ~3u;
        if (off + len > size) {
            err = "A section is longer than the file";
            break;
        }
        uint8_t *dst = NULL;
        switch (id) {
        case SEC_BLOCK:  dst = blk; if (len != BLOCK_SEC_BYTES) err = "Bad block section"; have_block = true; break;
        case SEC_RAM:    dst = ram; if (len != 65536) err = "Bad RAM section"; have_ram = true; break;
        case SEC_COLOR:  dst = col; if (len != 1024) err = "Bad colour RAM section"; have_col = true; break;
        case SEC_DRIVE:
            if (len != sizeof(TrxDriveState)) {
                err = "Bad drive section";
            } else if (!(drive = (TrxDriveState *)malloc(sizeof(TrxDriveState)))) {
                err = "Out of memory";
            }
            dst = (uint8_t *)drive;
            break;
        case SEC_CARTRAM:
            if (len != TRX_CART_RAM_BYTES) {
                err = "Bad cartridge RAM section";
            } else if (!(cartram = (uint8_t *)malloc(len))) {
                err = "Out of memory";
            }
            dst = cartram;
            break;
        case SEC_CARTROM: rom_off = off; rom_len = len; break;     // streamed into DDR2 while the machine stands
        case SEC_EEPROM:
            if (len != TRX_EEPROM_BYTES) {
                err = "Bad EEPROM section";
            } else if (!(eeprom = (uint8_t *)malloc(len))) {
                err = "Out of memory";
            }
            dst = eeprom;
            break;
        default: break;
        }
        if (!err && dst && (file->seek(off) != FR_OK || file->read(dst, len, &tr) != FR_OK || tr != len)) {
            err = "The file could not be read";
        }
        off += padded;
    }
    if (!err && !(have_block && have_ram && have_col)) {
        err = "The file has no chip block, RAM or colour RAM";
    }
    uint32_t w[TRX_BLOCK_WORDS];
    int x = 0;
    if (!err) {
        for (int i = 0; i < TRX_BLOCK_WORDS; i++) {
            w[i] = le32(blk + 4 * i);
        }
        x = blk[4 * TRX_BLOCK_WORDS] | blk[4 * TRX_BLOCK_WORDS + 1] << 8;
        if (x < 96 || x > 504) {
            err = "Bad snap position";
        } else if (blk[4 * TRX_BLOCK_WORDS + 2] & 1) {
            trx_ring_set_turbo(le32(blk + 4 * TRX_BLOCK_WORDS + 4));      // one shot, taken by the load below (a no-op on a machine without the turbo engine)
        }
    }
    // the cartridge the state needs goes into the port first, without a reset (FW trx_c64re_apply: build_machine before the state; U9 D2): the .crt the file
    // names, or an empty port when the state had none. What the file holds of the cartridge goes in with the state.
    // FW build_machine_mode: the video standard first, it drives the PLL and the colour timing the chip block that follows was running at
    if (!err && machine >= 0 && g_src_on && g_src.machine_set && g_src.machine_set(g_src.rom_ctx, machine) != 0) {
        note_missed(missed, sizeof(missed), "the video standard");
    }
    bool cart_ok = false;
    if (!err && has_ms) {
        if (g_src_on && g_src.cart_for_state) {
            cart_ok = g_src.cart_for_state(g_src.rom_ctx, cart_type > 0 ? cart_src : NULL) == 0;
        }
        if (!cart_ok && cart_type > 0) {
            note_missed(missed, sizeof(missed), "the cartridge");
        }
    } else if (!err && (cartram || rom_len || eeprom)) {
        note_missed(missed, sizeof(missed), "the cartridge");           // a file that does not name its machine
    }
    if (!err && has_ms) {                                                   // FW build_machine: cartridge, then drive A's image, then the registers; all before the state
        build_drive(ms_at, missed, sizeof(missed));
        build_settings(ms_at, missed, sizeof(missed));
    }
    if (!err) {
        bool cart_in = cart_ok && cart_type > 0;
        g_rom.file = file;
        g_rom.off = rom_off;
        g_rom.len = cart_in && g_src.rom_write ? rom_len : 0;
        g_rom.chunk = chunk;
        TrxLoadMem mem = { ram, col, cart_in ? cartram : NULL, g_rom.len ? rom_put_cb : NULL, NULL };
        int e = trx_ring_load_state(&mem, w, uint16_t(x));
        if (e) {
            err = trx_ring_error(e);
        } else if (cart_in && eeprom && g_src.eeprom_put) {
            g_src.eeprom_put(g_src.rom_ctx, eeprom);                 // after the block: replay_cart's $DE00 write drives the 93C86's lines on its way past
        }
    }
    if (!err && drive) {
        // drive A after the machine: the stub runs on the drive's own CPU and is stepped, so the C64's state does not matter (FW trx_c64re_apply)
        const char *derr = drive_write(drive);
        if (derr) {
            printf("[C64RE] drive A was not put back: %s\n", derr);
            note_missed(missed, sizeof(missed), "drive A");
        }
    }
    free(chunk);
    free(ram);
    free(col);
    free(blk);
    free(drive);
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
    g_missed = "";
    snprintf(g_load_path, sizeof(g_load_path), "%s", path);
    if (trx_trxring_is_one(path)) {
        // one way in for every file the ring can load (FW trx_c64re_load_entry): a .trxring brings the ring back as well
        return trx_trxring_load(path);
    }
    if (fm->fopen(path, FA_READ, &file) != FR_OK) {
        return "The file could not be opened";
    }
    uint32_t size = file->get_size();
    uint8_t magic[9];
    uint32_t mr = 0;
    if (size >= 41 && file->read(magic, 9, &mr) == FR_OK && mr == 9 && !memcmp(magic, "C64RESNP", 8) && magic[8] == C64RE_FORMAT) {
        const char *e2 = load_v2(file, size);               // format 2 is read in sections, never held whole
        fm->fclose(file);
        return e2;
    }
    if (file->seek(0) != FR_OK) {
        fm->fclose(file);
        return "The file could not be read";
    }
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
        err = "Not a .c64re (format 1 or 2) or .c64rering";
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
        TrxLoadMem mem = { ram, col, NULL, NULL, NULL };
        int e = trx_ring_load_state(&mem, w, uint16_t(x));
        if (e) {
            err = trx_ring_error(e);
        }
    }
    free(ram);
    free(col);
    return err;
}

// ---- the .trxring: the whole frame ring in a file (FW io/c64/trx_trxring.cc, load side) ---------------------------
//   0..7 "TRXRING1", 8..11 u32 header length, then the JSON header, then in this order the parts its "lengths" give: the timeline (TrxFrame entries,
//   oldest first, the newest entry's counts rebased to 0), C64 RAM, colour RAM, cartridge RAM, the records of streams 0 and 1. sha256State covers the
//   first four parts and is checked before anything is put back; sha256Log covers the records, a mismatch is a message, not a refusal.
#define TRXRING_HDR_MAX    2048
#define TRXRING_VERSION    1
#define TRXRING_CHUNK      0x8000

struct TrxRingBuf {
    uint8_t *raw, *p;
};

static bool trb_alloc(TrxRingBuf *b, uint32_t n)
{
    b->raw = (uint8_t *)malloc(n + 15);
    b->p = b->raw ? (uint8_t *)(((uintptr_t)b->raw + 15) & ~(uintptr_t)15) : NULL;
    return b->p != NULL;
}

static void trb_free(TrxRingBuf *b)
{
    free(b->raw);
    b->raw = NULL;
    b->p = NULL;
}

static bool tr_hexeq(const char *hex, const uint8_t *d, int n)
{
    static const char h[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        if (hex[2 * i] != h[d[i] >> 4] || hex[2 * i + 1] != h[d[i] & 15]) {
            return false;
        }
    }
    return true;
}

struct TrxRingLoad {
    File *f;
    char *hdr;
    jsmntok_t *tok;
    int ntok;
    TrxRingBuf buf, stage;
    uint32_t s0_bytes, s1_bytes, s0_first, s1_first;
    uint8_t digest_log[32];
};

static int64_t tr_hget(const TrxRingLoad *j, const char *a, const char *b, int64_t dflt)
{
    int o = key(j->hdr, j->tok, j->ntok, 0, a);
    if (b) {
        o = key(j->hdr, j->tok, j->ntok, o, b);
    }
    return o < 0 ? dflt : tok_int(j->hdr, j->tok, o);
}

static void tr_area_rd(void *ctx, uint32_t off, uint8_t *p, uint32_t len)
{
    trx_ring_area_read(*(int *)ctx, off, p, len);
}

static void tr_area_wr(void *ctx, uint32_t off, const uint8_t *p, uint32_t len)
{
    trx_ring_area_write(*(int *)ctx, off, p, len);
}

// Moves a run of records back into one of the engine's areas (FW trx_ring_recs.h trx_recs_out): an area is a ring, a run starts at
// (count * record size) & (size - 1) and wraps at its end; the mover takes 16-byte aligned addresses and whole 16-byte groups, so the group at each
// end is read first and written back with the new bytes in it, and the records beside it keep what they have.
static void tr_recs_out(int s, uint32_t from, const uint8_t *src, uint32_t n, uint8_t *stage)
{
    uint32_t size = trx_ring_area_size(s);
    uint32_t off = (from * trx_ring_rec_size(s)) & (size - 1);
    uint32_t done = 0;
    uint8_t edge[16];
    while (done < n) {
        uint32_t want = n - done;
        if (off + want > size) {
            want = size - off;              // the rest goes on at the area's start
        }
        uint32_t pre = off & 15u;
        if (pre) {
            uint32_t take = 16 - pre;
            if (take > want) {
                take = want;
            }
            tr_area_rd(&s, off - pre, edge, 16);
            memcpy(edge + pre, src + done, take);
            tr_area_wr(&s, off - pre, edge, 16);
            done += take;
            off += take;
        } else if (want >= 16) {
            uint32_t whole = want & ~15u;
            if (whole > TRXRING_CHUNK) {
                whole = TRXRING_CHUNK;
            }
            memcpy(stage, src + done, whole);
            tr_area_wr(&s, off, stage, whole);
            done += whole;
            off += whole;
        } else {
            tr_area_rd(&s, off, edge, 16);
            memcpy(edge, src + done, want);
            tr_area_wr(&s, off, edge, 16);
            done += want;
            off += want;
        }
        if (off >= size) {
            off = 0;
        }
    }
}

// the install's callback: the records out of the file and into the areas, at the offsets the rebased counts give
static int tr_areas_back(void *ctx)
{
    TrxRingLoad *j = (TrxRingLoad *)ctx;
    Sha256 sh;
    sha_init(&sh);
    for (int s = 0; s < 2; s++) {
        uint32_t left = s ? j->s1_bytes : j->s0_bytes;
        uint32_t rec = trx_ring_rec_size(s);
        uint32_t cnt = s ? j->s1_first : j->s0_first;
        while (left) {
            uint32_t want = left > TRXRING_CHUNK ? uint32_t(TRXRING_CHUNK) : left;
            want = (want / rec) * rec;
            uint32_t got = 0;
            if (!want || j->f->read(j->buf.p, want, &got) != FR_OK || got != want) {
                return -1;
            }
            sha_add(&sh, j->buf.p, want);
            tr_recs_out(s, cnt, j->buf.p, want, j->stage.p);
            cnt = (cnt + want / rec) & 0x0FFFFFFFu;
            left -= want;
        }
    }
    sha_final(&sh, j->digest_log);
    return 0;
}

// the header into hdr/tok (FW's first half of trx_trxring_load and of trx_trxring_check): NULL, or what is wrong
static const char *tr_open(TrxRingLoad *j)
{
    uint8_t head[12];
    uint32_t got = 0;
    if (j->f->read(head, 12, &got) != FR_OK || got != 12 || memcmp(head, "TRXRING1", 8)) {
        return "Not a .trxring";
    }
    uint32_t hl = uint32_t(head[8]) | (uint32_t(head[9]) << 8) | (uint32_t(head[10]) << 16) | (uint32_t(head[11]) << 24);
    if (hl < 2 || hl > TRXRING_HDR_MAX) {
        return "The header's length is not right";
    }
    j->hdr = (char *)malloc(hl + 1);
    if (!j->hdr || j->f->read(j->hdr, hl, &got) != FR_OK || got != hl) {
        return "The header could not be read";
    }
    j->hdr[hl] = 0;
    jsmn_parser p;
    jsmn_init(&p);
    int need = jsmn_parse(&p, j->hdr, hl, NULL, 0);
    if (need < 1) {
        return "The header is not JSON";
    }
    j->tok = (jsmntok_t *)malloc(sizeof(jsmntok_t) * need);
    jsmn_init(&p);
    j->ntok = j->tok ? jsmn_parse(&p, j->hdr, hl, j->tok, need) : -1;
    if (j->ntok < 1) {
        return "The header is not JSON";
    }
    if (tr_hget(j, "version", NULL, -1) != TRXRING_VERSION) {
        return "A .trxring of another version";
    }
    if (tr_hget(j, "blockWords", NULL, -1) != TRX_BLOCK_WORDS || tr_hget(j, "frameBytes", NULL, -1) != int64_t(sizeof(TrxFrame))) {
        return "A .trxring of another build (the timeline's shape differs)";
    }
    if (uint32_t(tr_hget(j, "bitstreamId", NULL, 0)) != trx_io_read32(TRX_ID)) {
        return "A .trxring of another bitstream";
    }
    return NULL;
}

bool trx_trxring_is_one(const char *path)
{
    File *f = NULL;
    if (FileManager::getFileManager()->fopen(path, FA_READ, &f) != FR_OK) {
        return false;
    }
    char magic[8];
    uint32_t got = 0;
    bool ok = f->read(magic, 8, &got) == FR_OK && got == 8 && !memcmp(magic, "TRXRING1", 8);
    FileManager::getFileManager()->fclose(f);
    return ok;
}

// Is this a .trxring this build can load (the header only, nothing is kept)? NULL, or what is wrong with it.
const char *trx_trxring_check(const char *path)
{
    TrxRingLoad j;
    memset(&j, 0, sizeof(j));
    if (FileManager::getFileManager()->fopen(path, FA_READ, &j.f) != FR_OK) {
        return "The file could not be opened";
    }
    const char *err = tr_open(&j);
    if (!err && tr_hget(&j, "frames", NULL, 0) < 1) {
        err = "The .trxring holds no frame";
    }
    if (!err) {
        err = trx_ring_reu_refusal();
    }
    free(j.hdr);
    free(j.tok);
    FileManager::getFileManager()->fclose(j.f);
    return err;
}

// Reads it and puts the ring and the machine back.
const char *trx_trxring_load(const char *path)
{
    TrxRingLoad j;
    memset(&j, 0, sizeof(j));
    FileManager *fm = FileManager::getFileManager();
    if (fm->fopen(path, FA_READ, &j.f) != FR_OK) {
        return "The file could not be opened";
    }
    uint32_t got = 0;
    int frames = 0;
    uint32_t tl_bytes = 0, ram_bytes = 0, col_bytes = 0, cart_bytes = 0;
    TrxFrame *ents = NULL;
    uint8_t *ram = NULL, *col = NULL, *cart = NULL;
    const char *err = tr_open(&j);
    if (!err) {
        int rb = key(j.hdr, j.tok, j.ntok, 0, "recBytes");
        bool recs_ok = rb >= 0 && j.tok[rb].type == JSMN_ARRAY && j.tok[rb].size == 2 &&
                       tok_int(j.hdr, j.tok, rb + 1) == int64_t(trx_ring_rec_size(0)) &&
                       tok_int(j.hdr, j.tok, rb + 2) == int64_t(trx_ring_rec_size(1));
        if (!recs_ok) {
            err = "A .trxring whose records are not this engine's size";
        }
    }
    if (!err) {
        frames = int(tr_hget(&j, "frames", NULL, 0));
        tl_bytes = uint32_t(tr_hget(&j, "lengths", "timeline", 0));
        ram_bytes = uint32_t(tr_hget(&j, "lengths", "ram", 0));
        col_bytes = uint32_t(tr_hget(&j, "lengths", "color", 0));
        cart_bytes = uint32_t(tr_hget(&j, "lengths", "cart", 0));
        j.s0_bytes = uint32_t(tr_hget(&j, "lengths", "s0", 0));
        j.s1_bytes = uint32_t(tr_hget(&j, "lengths", "s1", 0));
        j.s0_first = uint32_t(tr_hget(&j, "s0First", NULL, 0));
        j.s1_first = uint32_t(tr_hget(&j, "s1First", NULL, 0));
        if (frames < 1 || tl_bytes != uint32_t(frames) * sizeof(TrxFrame) || ram_bytes != 65536 || col_bytes != 1024 ||
            (cart_bytes && cart_bytes != TRX_CART_RAM_BYTES)) {
            err = "The header's lengths do not fit";
        }
    }
    if (!err) {
        ents = (TrxFrame *)malloc(tl_bytes);
        ram = (uint8_t *)malloc(ram_bytes);
        col = (uint8_t *)malloc(col_bytes);
        cart = cart_bytes ? (uint8_t *)malloc(cart_bytes) : NULL;
        trb_alloc(&j.buf, TRXRING_CHUNK + 32);
        trb_alloc(&j.stage, TRXRING_CHUNK + 32);
        if (!ents || !ram || !col || !j.buf.p || !j.stage.p || (cart_bytes && !cart)) {
            err = "Out of memory";
        }
    }
    if (!err) {
        Sha256 sh;
        sha_init(&sh);
        struct {
            uint8_t *p;
            uint32_t len;
        } state[4] = { { (uint8_t *)ents, tl_bytes }, { ram, ram_bytes }, { col, col_bytes }, { cart, cart_bytes } };
        for (int i = 0; !err && i < 4; i++) {
            if (!state[i].len) {
                continue;
            }
            if (j.f->read(state[i].p, state[i].len, &got) != FR_OK || got != state[i].len) {
                err = "The file ends inside the state";
            } else {
                sha_add(&sh, state[i].p, state[i].len);
            }
        }
        uint8_t d[32];
        sha_final(&sh, d);
        int sd = key(j.hdr, j.tok, j.ntok, 0, "sha256State");
        if (!err && (sd < 0 || !tr_hexeq(j.hdr + j.tok[sd].start, d, 32))) {
            err = "The ring's state does not match its hash";
        }
    }
    if (!err) {
        int ld = key(j.hdr, j.tok, j.ntok, 0, "sha256Log");
        const char *log_hex = ld >= 0 ? j.hdr + j.tok[ld].start : NULL;
        TrxLoadMem mem = { ram, col, cart, NULL, NULL };
        int e = trx_ring_install(ents, frames, &mem, tr_areas_back, &j);
        if (e == -1) {
            err = "The file ends inside the log";
        } else if (e) {
            err = trx_ring_error(e);
        } else {
            if (log_hex && !tr_hexeq(log_hex, j.digest_log, 32)) {
                printf("[RING] the .trxring's log does not match its hash: the machine is where the file says, but a rewind may not be\n");
            }
            printf("[RING] .trxring loaded: %d frames, %u + %u record bytes\n", frames, (unsigned)j.s0_bytes, (unsigned)j.s1_bytes);   // T251
        }
    }
    fm->fclose(j.f);
    free(j.hdr);
    free(j.tok);
    free(ents);
    free(ram);
    free(col);
    free(cart);
    trb_free(&j.buf);
    trb_free(&j.stage);
    return err;
}
