#include "startfile.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t (*g_clock)(void);
static uint32_t cur_ms(void) { return g_clock ? g_clock() : 0; }

struct KeyStats {
    uint32_t samples, fire_n, cbm_n;
    int32_t first_fire, first_cbm;           // sample number (1-based), -1 = never
    uint32_t t_fire, t_cbm;
    uint8_t raw_first, raw_last;
};

static KeyStats g_k;
static bool g_cfg_seen, g_decided;
static uint32_t g_t_cfg, g_t_win0, g_t_win1, g_win_ms;
static int g_cfg_n, g_cfg_def, g_cfg_direct, g_cfg_cd;
static char g_body[STARTREC_BODY];
static int g_blen;
static bool g_stored;                        // this start's block is in the file already
static unsigned g_seq;

void startrec_init(uint32_t (*clock)(void))
{
    g_clock = clock;
    memset(&g_k, 0, sizeof(g_k));
    g_k.first_fire = g_k.first_cbm = -1;
    g_cfg_seen = g_decided = g_stored = false;
    g_t_cfg = g_t_win0 = g_t_win1 = g_win_ms = 0;
    g_blen = 0;
    g_body[0] = 0;
    g_seq = 0;
}

void startrec_key(uint8_t joy, uint8_t row)
{
    g_k.samples++;
    if (g_k.samples == 1) {
        g_k.raw_first = joy;
    }
    g_k.raw_last = joy;
    if (!(joy & 0x10)) {
        g_k.fire_n++;
        if (g_k.first_fire < 0) {
            g_k.first_fire = (int32_t)g_k.samples;
            g_k.t_fire = cur_ms();
        }
    }
    if (!(row & 0x20)) {
        g_k.cbm_n++;
        if (g_k.first_cbm < 0) {
            g_k.first_cbm = (int32_t)g_k.samples;
            g_k.t_cbm = cur_ms();
        }
    }
}

#define STARTREC_RESERVE 160       // the last bytes of the block belong to the "started by" line; other lines stop before them
static int g_reserve = STARTREC_RESERVE;
static void put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void put(const char *fmt, ...)
{
    int cap = STARTREC_BODY - g_reserve;
    if (g_blen >= cap - 1) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_body + g_blen, cap - g_blen, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (g_blen + n >= cap - 1) {                // does not fit: the line is dropped whole, never half
        g_body[g_blen] = 0;
        return;
    }
    g_blen += n;
}

bool startrec_cfg(int n, int def, int direct, int countdown)
{
    uint32_t t = cur_ms();
    if (g_cfg_seen) {
        return false;
    }
    g_cfg_seen = true;
    g_t_cfg = t;
    g_cfg_n = n;
    g_cfg_def = def;
    g_cfg_direct = direct;
    g_cfg_cd = countdown;
    if (g_decided) {
        put("t=%6u ms boot.cfg read AFTER the decision: cores %d, default %d, direct %d, countdown %d\n", (unsigned)t, n, def, direct, countdown);
        return true;
    }
    return false;
}

void startrec_window_open(uint32_t ms)
{
    g_t_win0 = cur_ms();
    g_win_ms = ms;
}

void startrec_window_close(void)
{
    g_t_win1 = cur_ms();
}

void startrec_decide(const DirectIn &in, DbReason why)
{
    g_decided = true;
    g_blen = 0;
    g_body[0] = 0;
    uint32_t t = cur_ms();
    if (g_cfg_seen) {
        put("t=%6u ms boot.cfg read: cores %d, default %d, direct %d, countdown %d%s\n", (unsigned)g_t_cfg, g_cfg_n, g_cfg_def, g_cfg_direct, g_cfg_cd,
            g_t_cfg > g_t_win0 + g_win_ms ? " (LATE: after the key window ended)" : "");
    } else {
        put("boot.cfg: not read before the decision\n");
    }
    put("key window: %u ms from t=%u ms, closed at t=%u ms\n", (unsigned)g_win_ms, (unsigned)g_t_win0, (unsigned)g_t_win1);
    put("keys: %u samples, raw port 1 first 0x%02X last 0x%02X\n", (unsigned)g_k.samples, g_k.raw_first, g_k.raw_last);
    if (g_k.first_fire >= 0) {
        put("  fire: low in %u samples, first at sample %d (t=%u ms)\n", (unsigned)g_k.fire_n, (int)g_k.first_fire, (unsigned)g_k.t_fire);
    } else {
        put("  fire: never seen\n");
    }
    if (g_k.first_cbm >= 0) {
        put("  C=: down in %u samples, first at sample %d (t=%u ms)\n", (unsigned)g_k.cbm_n, (int)g_k.first_cbm, (unsigned)g_k.t_cbm);
    } else {
        put("  C=: never seen\n");
    }
    put("input: cfg %d, direct %d, cores %d, default %d, marker %d, C= %d, fire %d, default file %d\n", in.cfg_loaded, in.direct, in.n, in.def,
        in.marker, in.key_cbm, in.key_fire, (int)in.dstate);
    put("t=%6u ms decision: %s%s\n", (unsigned)t, why == DB_START ? "START the default core" : directboot_text(why),
        why == DB_START ? "" : directboot_stops_countdown(why) ? " (countdown stopped)" : " (countdown runs)");
    if (why == DB_START) {
        g_reserve = 0;
        put("t=%6u ms started by: direct start\n", (unsigned)t);
        g_reserve = STARTREC_RESERVE;
    }
}

void startrec_event(const char *fmt, ...)
{
    char l[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l, sizeof(l), fmt, ap);
    va_end(ap);
    g_reserve = strncmp(l, "started by", 10) ? STARTREC_RESERVE : 0;
    put("t=%6u ms %s\n", (unsigned)cur_ms(), l);
    g_reserve = STARTREC_RESERVE;
}

bool startrec_decided(void) { return g_decided; }
const char *startrec_body(void) { return g_body; }

// ---- the file ---------------------------------------------------------------------------------------------------------------------
#define HDR "=== start #"

static bool at_header(const char *b, int i) { return (i == 0 || b[i - 1] == '\n') && !strncmp(b + i, HDR, sizeof(HDR) - 1); }

static int last_header(const char *b, int len)
{
    for (int i = len - 1; i >= 0; i--) {
        if (at_header(b, i)) {
            return i;
        }
    }
    return -1;
}

// Drops the oldest blocks until at most `keep` remain and `extra` more bytes fit under STARTFILE_MAX.
static int trim(char *b, int len, int keep, int extra)
{
    for (;;) {
        int n = 0, second = -1;
        for (int i = 0; i < len; i++) {
            if (at_header(b, i)) {
                if (++n == 2) {
                    second = i;
                }
            }
        }
        if (n == 0 || (n <= keep && len + extra <= STARTFILE_MAX) || second < 0) {
            return len;
        }
        memmove(b, b + second, len - second);
        len -= second;
    }
}

bool startfile_store(const StartLogFs *fs, const char *app, const char *built, const char *clock)
{
    static char buf[STARTFILE_MAX + STARTREC_BODY + 200];
    char path[64];
    if (!fs->make_dir(fs->ctx, STARTFILE_DIR)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/%s", STARTFILE_DIR, STARTFILE_NAME);
    int len = fs->load(fs->ctx, path, buf, STARTFILE_MAX);
    if (len < 0 || len > STARTFILE_MAX) {
        len = 0;
    }
    buf[len] = 0;
    int first = 0;                                       // a file not starting with a block header (foreign, damaged): what precedes the first header goes
    while (first < len && !at_header(buf, first)) {
        first++;
    }
    if (first > 0) {
        memmove(buf, buf + first, len - first);
        len -= first;
        buf[len] = 0;
    }
    int h = last_header(buf, len);
    unsigned last = 0;
    if (h >= 0) {
        last = (unsigned)strtoul(buf + h + sizeof(HDR) - 1, 0, 10);
    }
    if (g_stored && h >= 0 && last == g_seq) {            // this start wrote its block before: it is replaced, not repeated
        len = h;
        buf[len] = 0;
    } else {
        g_seq = last + 1;
    }
    int w = snprintf(buf + len, sizeof(buf) - len, HDR "%u | host app %s built %s | clock %s ===\n%s", g_seq, app, built, clock, g_body);
    if (w < 0 || w >= (int)(sizeof(buf) - len)) {      // cut short: nothing is written
        return false;
    }
    len += w;
    len = trim(buf, len, STARTFILE_KEEP, 0);
    if (len > STARTFILE_MAX) {
        return false;
    }
    g_stored = fs->save(fs->ctx, STARTFILE_DIR, STARTFILE_NAME, buf, len);
    return g_stored;
}
