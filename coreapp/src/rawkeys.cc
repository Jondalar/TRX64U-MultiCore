#include "rawkeys.h"
#include "hotkeys.h"
#include <string.h>

namespace {

const RawKeySpec *g_spec;

const int RING = 64;                       // reports; the USB task writes, the poll reads
uint8_t ring[RING][8];
volatile uint8_t ring_head, ring_tail;
volatile uint8_t last_rep[8];              // the newest report, whether or not the ring had room for it (the menu's baseline)
volatile bool have_last;

const int EVQ = 128;                       // events not yet written; the indices count up and are masked
uint8_t evq[EVQ];
uint8_t ev_head, ev_tail;

uint8_t down[16];                          // the machine codes the machine has as pressed (bit c)
uint8_t ignore[16];                        // held across the menu: not pressed until released
bool caps_down, caps_on;

bool bit(const uint8_t *s, int i) { return s[i >> 3] & (1 << (i & 7)); }
void setbit(uint8_t *s, int i) { s[i >> 3] |= (uint8_t)(1 << (i & 7)); }
void clrbit(uint8_t *s, int i) { s[i >> 3] &= (uint8_t)~(1 << (i & 7)); }

int evq_count() { return (ev_head - ev_tail) & (EVQ - 1); }

void push(uint8_t ev)
{
    if (evq_count() < EVQ - 1) {
        evq[ev_head & (EVQ - 1)] = ev;
        ev_head++;
    }
}

// the key codes a report holds: modifiers first, then the slots in order; at most 8 + 6
int report_codes(const uint8_t *rep, uint8_t *codes, bool *caps)
{
    int n = 0;
    *caps = false;
    for (int b = 0; b < 8; b++) {
        if ((rep[0] & (1 << b)) && g_spec->mod[b] != 0xFF) {
            codes[n++] = g_spec->mod[b];
        }
    }
    for (int i = 2; i < 8; i++) {
        uint8_t u = rep[i];
        if (!u || rawkeys_is_menu_key(u)) {
            continue;
        }
        if (u == g_spec->caps_usage) {
            *caps = true;
        } else if (u < g_spec->nhid && g_spec->hid[u] != 0xFF) {
            codes[n++] = g_spec->hid[u];
        }
    }
    return n;
}

void take(const uint8_t *rep)
{
    if (rep[2] == 0x01) {
        return;                                      // ErrorRollOver: too many keys, the report says nothing
    }
    uint8_t codes[16];
    bool caps;
    int n = report_codes(rep, codes, &caps);
    uint8_t cur[16] = { 0 };
    for (int i = 0; i < n; i++) {
        setbit(cur, codes[i]);
    }
    uint8_t want[16];                                // what should be down: the held keys without the ignored ones
    for (int i = 0; i < 16; i++) {
        ignore[i] &= cur[i];                         // a key held across the menu leaves the ignore set when it comes up
        want[i] = (uint8_t)(cur[i] & ~ignore[i]);
    }
    for (int c = 0; c < 128; c++) {                  // releases, the keys before the modifiers (ascending codes)
        if (bit(down, c) && !bit(want, c)) {
            push((uint8_t)(c | 0x80));
            clrbit(down, c);
        }
    }
    for (int i = 0; i < n; i++) {                    // presses in report order, modifiers first
        if (bit(want, codes[i]) && !bit(down, codes[i])) {
            push(codes[i]);
            setbit(down, codes[i]);
        }
    }
    if (caps && !caps_down) {
        caps_on = !caps_on;
        push(caps_on ? g_spec->caps_code : (uint8_t)(g_spec->caps_code | 0x80));
    }
    caps_down = caps;
}

// writes what the register has room for; true when it wrote
bool flush(CoreCtx *c)
{
    bool wrote = false;
    while (ev_head != ev_tail) {
        if (!(c->io->rd(g_spec->reg_key) & 1)) {
            break;                                   // the core's queue holds more than 8: wait
        }
        for (int k = 0; k < 8 && ev_head != ev_tail; k++) {
            c->io->wr(g_spec->reg_key, evq[ev_tail & (EVQ - 1)]);
            ev_tail++;
            wrote = true;
        }
    }
    return wrote;
}


// ---- held-keys mode (RawKeySpec::held): the usages down, as a set of 256 bits
uint8_t h_cur[32];                         // what the core was told is down
uint8_t h_ignore[32];                      // held across the menu: not given to the core until released
bool h_known;                              // h_cur is what the core has (false: tell it at the next pace)

// the usages a report holds: the modifier bits as 0xE0 + bit, then the slots; the menu key never
void held_set(const uint8_t *rep, uint8_t set[32])
{
    memset(set, 0, 32);
    for (int b = 0; b < 8; b++) {
        if (rep[0] & (1 << b)) {
            setbit(set, 0xE0 + b);
        }
    }
    for (int i = 2; i < 8; i++) {
        if (rep[i] > 3 && !rawkeys_is_menu_key(rep[i])) {
            setbit(set, rep[i]);
        }
    }
}

void held_tell(CoreCtx *c, const uint8_t *set)
{
    uint8_t list[48];
    int n = 0;
    for (int u = 0; u < 256 && n < (int)sizeof(list); u++) {
        if (bit(set, u)) {
            list[n++] = (uint8_t)u;
        }
    }
    g_spec->held(c, list, n);
    memcpy(h_cur, set, 32);
    h_known = true;
}

void held_take(CoreCtx *c, const uint8_t *rep)
{
    if (rep[2] == 0x01) {
        return;                                      // ErrorRollOver: the report says nothing
    }
    uint8_t set[32];
    held_set(rep, set);
    for (int i = 0; i < 32; i++) {
        h_ignore[i] &= set[i];                       // a key held across the menu leaves the ignore set when it comes up
        set[i] &= (uint8_t)~h_ignore[i];
    }
    if (!h_known || memcmp(set, h_cur, 32)) {
        held_tell(c, set);
    }
}

}

bool rawkeys_is_menu_key(uint8_t usage)
{
    return usage == 0x47;                            // SCROLL LOCK; F10 goes to the machine (T141 D5, run.cc rawkeys_active)
}

bool rawkeys_active()
{
    return g_spec != nullptr && !g_spec->held;
}

void rawkeys_configure(const RawKeySpec *spec)
{
    g_spec = spec;
    rawkeys_reset();
}

void rawkeys_reset()
{
    memset(down, 0, sizeof(down));
    memset(ignore, 0, sizeof(ignore));
    ev_head = ev_tail = 0;
    caps_down = false;
    caps_on = false;
    memset(h_cur, 0, sizeof(h_cur));
    memset(h_ignore, 0, sizeof(h_ignore));
    h_known = false;
}

void rawkeys_usb_report(const uint8_t report_in[8])
{
    if (!g_spec) {
        return;
    }
    uint8_t report[8];
    hk_usb_report_filter(report_in, report);          // T181: the hotkey combination is the application's, the machine does not see it
    for (int i = 0; i < 8; i++) {
        last_rep[i] = report[i];
    }
    have_last = true;
    uint8_t h = ring_head;
    uint8_t n = (uint8_t)((h + 1) % RING);
    if (n == ring_tail) {
        return;                                      // full: the poll is not running (menu); the baseline is the last report taken after it
    }
    memcpy(ring[h], report, 8);
    ring_head = n;
}

void rawkeys_menu(CoreCtx *c, bool open)
{
    if (!g_spec) {
        return;
    }
    if (g_spec->held) {
        if (open) {
            ring_tail = ring_head;
            uint8_t none[32] = { 0 };
            held_tell(c, none);                      // the machine has no key down while the menu is open
            return;
        }
        ring_tail = ring_head;
        if (have_last) {
            uint8_t last[8];
            for (int tries = 0; tries < 4; tries++) {
                for (int i = 0; i < 8; i++) last[i] = last_rep[i];
                uint8_t again[8];
                for (int i = 0; i < 8; i++) again[i] = last_rep[i];
                if (!memcmp(last, again, 8)) break;
            }
            held_set(last, h_ignore);                // the keys held now are ignored until they have been up
        }
        return;
    }
    if (open) {
        // the reports not yet taken are dropped with the baseline at the close (below): what comes while the menu is open is not the machine's
        ev_head = ev_tail = 0;
        for (int code = 0; code < 128; code++) {
            if (bit(down, code)) {
                push((uint8_t)(code | 0x80));
                clrbit(down, code);
            }
        }
        flush(c);
        return;
    }
    // closed: the newest report seen is the baseline (read twice: the USB task may be writing it)
    ring_tail = ring_head;
    uint8_t last[8], again[8];
    for (int tries = 0; tries < 4; tries++) {
        for (int i = 0; i < 8; i++) last[i] = last_rep[i];
        for (int i = 0; i < 8; i++) again[i] = last_rep[i];
        if (!memcmp(last, again, 8)) {
            break;
        }
    }
    if (have_last) {
        uint8_t codes[16];
        bool caps;
        int n = report_codes(last, codes, &caps);
        memset(ignore, 0, sizeof(ignore));
        for (int i = 0; i < n; i++) {
            setbit(ignore, codes[i]);
        }
        caps_down = caps;
    }
}

bool rawkeys_pace(CoreCtx *c)
{
    if (!g_spec) {
        return false;
    }
    while (ring_tail != ring_head && evq_count() < EVQ - 32) {
        uint8_t rep[8];
        memcpy(rep, ring[ring_tail], 8);
        ring_tail = (uint8_t)((ring_tail + 1) % RING);
        if (g_spec->held) {
            held_take(c, rep);
        } else {
            take(rep);
        }
    }
    return g_spec->held ? false : flush(c);
}
