#include "pad.h"
#include "hidpad.h"
#include <stdio.h>
#include <string.h>

#define PAD_DESC_MAX 256
#define PAD_REDUMP_MS 15000      // the descriptor is printed again this long after the plug

namespace {

struct Slot {
    bool used;
    const void *key;
    HidPadLayout lay;
    PadRaw raw;
    char name[40];
    uint16_t vid, pid;
    uint8_t desc[PAD_DESC_MAX];
    int desc_len;
    int dumps;                   // times the descriptor went to the console
    uint32_t seen_ms;            // the first pace that saw the pad
    bool seen;
};

Slot slots[PAD_PLAYERS];
MouseCrit *crit;
const PadSpec *spec;
int mode;
uint8_t custom[PAD_PLAYERS][PAD_BUTTONS_MAX];
uint8_t resolved[PAD_PLAYERS][PAD_BUTTONS_MAX];
bool custom_ok[PAD_PLAYERS];
uint16_t sent[PAD_PLAYERS];
bool sent_valid[PAD_PLAYERS];
int log_budget = 96;
int g_nact;                      // T225: the buttons of the target in force (0 = all of the spec's)
int g_type;                      // T225: its type number
bool id_seen;                    // T225 by_id: the pad whose stored map is in force ...
uint16_t id_vid, id_pid;         // ... (0:0 = none)

struct Capture {
    bool active;
    int player;
    bool armed;
    uint64_t prev;
    bool got;
    uint8_t src;
} cap;

struct Guard {
    Guard() { if (crit) crit->enter(); }
    ~Guard() { if (crit) crit->leave(); }
};

uint64_t pressed_set(const PadRaw &r)
{
    return (uint64_t)r.buttons | ((uint64_t)(r.dpad & 0x0F) << 32);
}

int slot_of(const void *key)
{
    for (int i = 0; i < PAD_PLAYERS; i++) {
        if (slots[i].used && slots[i].key == key) return i;
    }
    return -1;
}

}

namespace {

const PadNumbering numberings[] = {
    // right bottom left top L R select start (source id = Button number - 1)
    { 0x2DC8, 0x6001, "8BitDo SN30 Pro", { 0, 1, 4, 3, 6, 7, 10, 11 } },       // read on the board 2026-10-29 (Pad 1 map=01040A0B...)
};
const PadNumbering generic = { 0, 0, "generic", { 1, 0, 2, 3, 4, 5, 6, 7 } };

void dump_desc(int player, const char *name, const uint8_t *d, int len)
{
    printf("[pad] pad %d %s: report descriptor %d bytes\n", player + 1, name, len);
    for (int o = 0; o < len; o += 16) {
        printf("[pad] desc %03d:", o);
        for (int k = o; k < len && k < o + 16; k++) printf(" %02X", d[k]);
        printf("\n");
    }
}

}

const PadNumbering *pad_numbering_for(uint16_t vid, uint16_t pid)
{
    for (unsigned i = 0; i < sizeof(numberings) / sizeof(numberings[0]); i++) {
        const PadNumbering *n = &numberings[i];
        if (n->vid == vid && n->pid == pid) return n;
    }
    return &generic;
}

void pad_default_map(const PadSpec *sp, uint16_t vid, uint16_t pid, uint8_t *out)
{
    const PadNumbering *n = pad_numbering_for(vid, pid);
    for (int i = 0; i < sp->nbuttons && i < PAD_BUTTONS_MAX; i++) {
        uint8_t e = sp->def_map[i];
        out[i] = (e >= PAD_ROLE_RIGHT && e <= PAD_ROLE_START) ? n->src[e - PAD_ROLE_RIGHT] : e;
    }
}

void pad_set_crit(MouseCrit *c)
{
    crit = c;
}

void pad_configure(const PadSpec *s)
{
    Guard g;
    spec = s;
    for (int i = 0; i < PAD_PLAYERS; i++) {
        sent_valid[i] = false;
        custom_ok[i] = false;
    }
    mode = PAD_MODE_DEFAULT;
    cap.active = false;
    g_nact = 0;
    g_type = 0;
    id_seen = false;
}

void pad_reset()
{
    Guard g;
    for (int i = 0; i < PAD_PLAYERS; i++) {
        sent_valid[i] = false;
    }
    cap.active = false;
}

bool pad_attach(const void *key, const char *name, uint16_t vid, uint16_t pid, const uint8_t *desc, int len)
{
    HidPadLayout l;
    bool ok = hidpad_parse(desc, len, &l);
    printf("[pad] %s %04X:%04X: report descriptor %d bytes, %s\n", name, vid, pid, len, ok ? "a gamepad" : "no gamepad in it");
    for (int o = 0; o < len; o += 16) {
        printf("[pad] desc %03d:", o);
        for (int k = o; k < len && k < o + 16; k++) printf(" %02X", desc[k]);
        printf("\n");
    }
    if (!ok) {
        return false;
    }
    int i;
    {
        Guard g;
        i = slot_of(key);
        if (i < 0) {
            for (int k = 0; k < PAD_PLAYERS; k++) {
                if (!slots[k].used) { i = k; break; }
            }
        }
        if (i >= 0) {
            Slot &s = slots[i];
            memset(&s, 0, sizeof(s));
            s.used = true;
            s.key = key;
            s.lay = l;
            snprintf(s.name, sizeof(s.name), "%s", name);
            s.vid = vid;
            s.pid = pid;
            s.desc_len = len > PAD_DESC_MAX ? PAD_DESC_MAX : len;
            memcpy(s.desc, desc, s.desc_len);
            s.dumps = 1;                                           // the dump above
            sent_valid[i] = false;
        }
    }
    if (i < 0) { printf("[pad] %s: two pads are in use, this one is left alone\n", name); return false; }
    int nb = 0;
    for (int k = 0; k < l.nruns; k++) nb += l.runs[k].count;
    printf("[pad] pad %d: %s (%d buttons%s%s%s, report %d bits%s)\n", i + 1, name, nb, l.hat.ok ? ", hat" : "", (l.x.ok && l.y.ok) ? ", X/Y" : "",
           l.dp[0].ok ? ", D-pad usages" : "", (int)l.bits, l.has_ids ? ", with report ID" : "");
    printf("[pad] pad %d: default numbering: %s\n", i + 1, pad_numbering_for(vid, pid)->name);
    return true;
}

int pad_player_of(const void *key)
{
    Guard g;
    return slot_of(key);
}

uint16_t pad_vid(int player) { return (player >= 0 && player < PAD_PLAYERS && slots[player].used) ? slots[player].vid : 0; }
uint16_t pad_pid(int player) { return (player >= 0 && player < PAD_PLAYERS && slots[player].used) ? slots[player].pid : 0; }

const char *pad_name(int player)
{
    return (player >= 0 && player < PAD_PLAYERS && slots[player].used) ? slots[player].name : "";
}

void pad_report(const void *key, const uint8_t *data, int len)
{
    int i;
    PadRaw r;
    bool log_it = false;
    {
        Guard g;
        i = slot_of(key);
        if (i < 0) {
            return;
        }
        if (!hidpad_decode(slots[i].lay, data, len, &r)) {
            return;
        }
        if (r != slots[i].raw && log_budget > 0) {
            log_budget--;
            log_it = true;
        }
        slots[i].raw = r;
        if (cap.active && cap.player == i) {
            uint64_t now = pressed_set(r);
            if (!cap.armed) {
                if (now == 0) cap.armed = true;                       // the wizard waits for the previous press to be let go
            } else if (now & ~cap.prev) {
                uint64_t fresh = now & ~cap.prev;
                for (int b = 0; b < 36; b++) {
                    if (fresh & ((uint64_t)1 << b)) { cap.src = (uint8_t)b; break; }
                }
                cap.got = true;
                cap.active = false;
            }
            cap.prev = now;
        }
    }
    if (log_it) {                                                  // the console is slow: never inside the critical section
        printf("[pad] pad %d report:", i + 1);
        for (int k = 0; k < len && k < 16; k++) printf(" %02X", data[k]);
        printf(" -> buttons %08X dpad %X\n", (unsigned)r.buttons, r.dpad);
    }
}

void pad_detach(const void *key)
{
    char name[sizeof(slots[0].name)];
    int i;
    {
        Guard g;
        i = slot_of(key);
        if (i < 0) {
            return;
        }
        snprintf(name, sizeof(name), "%s", slots[i].name);
        memset(&slots[i], 0, sizeof(slots[i]));               // zero state: the next pace writes 0 (S5)
        if (cap.active && cap.player == i) {
            cap.active = false;
        }
    }
    printf("[pad] pad %d: %s is gone\n", i + 1, name);
}

uint16_t pad_word(const PadSpec *sp, const uint8_t *map, uint32_t buttons, uint8_t dpad)
{
    uint16_t w = 0;
    int n = (g_nact > 0 && g_nact < sp->nbuttons) ? g_nact : sp->nbuttons;
    for (int i = 0; i < n && i < PAD_BUTTONS_MAX; i++) {
        uint8_t s = map[i];
        bool on = false;
        if (s < 32) on = (buttons >> s) & 1;
        else if (s < 36) on = (dpad >> (s - 32)) & 1;
        if (on) w |= (uint16_t)(1u << i);
    }
    return w;
}

int pad_slot(int player)
{
    if (!spec || !spec->by_id) return player;
    for (int k = 0; k < PAD_PLAYERS; k++) {
        if (slots[k].used) return k;
    }
    return 0;
}

void pad_set_target(int type, int nbuttons)
{
    Guard g;
    g_type = type;
    g_nact = nbuttons;
    for (int i = 0; i < PAD_PLAYERS; i++) sent_valid[i] = false;
}

int pad_nbuttons() { return (spec && g_nact > 0 && g_nact < spec->nbuttons) ? g_nact : (spec ? spec->nbuttons : 0); }
int pad_target_type() { return g_type; }

const uint8_t *pad_active_map(int player)
{
    if (!spec) return 0;
    if (player < 0 || player >= PAD_PLAYERS) return spec->def_map;
    int s = pad_slot(player);
    if ((mode == PAD_MODE_CUSTOM || spec->by_id) && custom_ok[player]) return custom[player];
    pad_default_map(spec, slots[s].used ? slots[s].vid : 0, slots[s].used ? slots[s].pid : 0, resolved[player]);
    return resolved[player];
}

// by_id: the stored map of the pad that is plugged now (its VID:PID, any socket), looked up when that pad changes.
static void id_refresh(CoreCtx *c)
{
    uint16_t v, pi;
    {
        Guard g;
        int s = pad_slot(0);
        v = slots[s].used ? slots[s].vid : 0;
        pi = slots[s].used ? slots[s].pid : 0;
        if (id_seen && v == id_vid && pi == id_pid) return;
        id_seen = true;
        id_vid = v;
        id_pid = pi;
        custom_ok[0] = false;
    }
    if (!slots[pad_slot(0)].used || !c->settings) return;
    char key[40], val[80];
    uint8_t m[PAD_BUTTONS_MAX];
    snprintf(key, sizeof(key), "Pad map %04X:%04X", v, pi);
    if (c->settings->extra_get(key, val, sizeof(val)) && val[0] >= '0' && val[0] <= '9' && pad_map_from_text(val + 1, spec->nbuttons, m)) {
        pad_set_custom(0, m);
        printf("[pad] %04X:%04X: stored map for target type %c\n", v, pi, val[0]);
        if (spec->id_loaded) spec->id_loaded(c, val[0] - '0');
    }
}

bool pad_save_id_map(Settings *s, int player)
{
    if (!spec || !spec->by_id || !s || player != 0 || !custom_ok[0]) return false;
    int sl = pad_slot(0);
    if (!slots[sl].used) return false;
    char key[40], v[80];
    snprintf(key, sizeof(key), "Pad map %04X:%04X", slots[sl].vid, slots[sl].pid);
    v[0] = (char)('0' + g_type);
    if (!pad_map_to_text(custom[0], spec->nbuttons, v + 1, sizeof(v) - 1)) return false;
    return s->extra_set(key, v);
}

void pad_ensure(CoreCtx *c)
{
    if (spec != c->desc->pad) {                           // a core of another descriptor: its spec, its maps from its .cfg
        pad_configure(c->desc->pad);
        pad_load_maps(c->settings);
    }
}

bool pad_pace(CoreCtx *c)
{
    pad_ensure(c);
    if (!spec) {
        return false;
    }
    bool wrote = false;
    if (spec->by_id) id_refresh(c);
    for (int p = 0; p < (spec->by_id ? 1 : PAD_PLAYERS); p++) {
        uint16_t w;
        uint8_t dd[PAD_DESC_MAX];
        int dl = 0;
        char nm[sizeof(slots[0].name)];
        {
            Guard g;
            Slot &sl = slots[pad_slot(p)];
            if (sl.used && !sl.seen) {
                sl.seen = true;
                sl.seen_ms = c->io->now_ms();
            } else if (sl.used && sl.dumps < 2 && c->io->now_ms() - sl.seen_ms >= PAD_REDUMP_MS) {
                sl.dumps = 2;                                      // the descriptor once more, long after the plug (the console ring is 4 KB)
                dl = sl.desc_len;
                memcpy(dd, sl.desc, dl);
                snprintf(nm, sizeof(nm), "%s", sl.name);
            }
        }
        if (dl) dump_desc(p, nm, dd, dl);
        {
            Guard g;
            w = pad_word(spec, pad_active_map(p), slots[pad_slot(p)].raw.buttons, slots[pad_slot(p)].raw.dpad);
            if (sent_valid[p] && sent[p] == w) continue;
            sent[p] = w;
            sent_valid[p] = true;
        }
        c->io->wr(spec->reg[p], (uint8_t)w);
        c->io->wr(spec->reg[p] + 1u, (uint8_t)(w >> 8));       // the high byte commits the pair in the gateware
        wrote = true;
    }
    return wrote;
}

void pad_set_mode(int m)
{
    Guard g;
    mode = m == PAD_MODE_CUSTOM ? PAD_MODE_CUSTOM : PAD_MODE_DEFAULT;
}

int pad_mode() { return mode; }

bool pad_custom_valid(int p) { return p >= 0 && p < PAD_PLAYERS && custom_ok[p]; }

bool pad_set_custom(int p, const uint8_t *map)
{
    if (!spec || p < 0 || p >= PAD_PLAYERS) return false;
    Guard g;
    memcpy(custom[p], map, spec->nbuttons);
    custom_ok[p] = true;
    return true;
}

void pad_clear_custom(int p)
{
    if (p >= 0 && p < PAD_PLAYERS) { Guard g; custom_ok[p] = false; }
}

bool pad_map_to_text(const uint8_t *map, int n, char *out, int outn)
{
    if (n * 2 + 1 > outn) return false;
    for (int i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02X", map[i]);
    return true;
}

bool pad_map_from_text(const char *t, int n, uint8_t *map)
{
    if ((int)strlen(t) != n * 2 || n > PAD_BUTTONS_MAX) return false;
    uint8_t tmp[PAD_BUTTONS_MAX];
    for (int i = 0; i < n; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char ch = t[2 * i + k];
            unsigned d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else return false;
            v = v * 16 + d;
        }
        if (v > 35 && v != PAD_SRC_NONE) return false;
        tmp[i] = (uint8_t)v;
    }
    memcpy(map, tmp, n);
    return true;
}

static const char *const map_keys[PAD_PLAYERS] = { "Pad 1 map", "Pad 2 map" };

void pad_load_maps(Settings *s)
{
    if (!spec || !s || spec->by_id) return;                // a by_id spec keeps its maps per pad (id_refresh)
    int mi = s->find("Pad map");
    pad_set_mode(mi >= 0 ? s->value(mi) : PAD_MODE_DEFAULT);
    for (int p = 0; p < PAD_PLAYERS; p++) {
        char v[80];
        uint8_t m[PAD_BUTTONS_MAX];
        pad_clear_custom(p);
        if (s->extra_get(map_keys[p], v, sizeof(v)) && pad_map_from_text(v, spec->nbuttons, m)) {
            pad_set_custom(p, m);
        }
    }
}

bool pad_save_map(Settings *s, int p)
{
    if (!spec || !s || p < 0 || p >= PAD_PLAYERS || !custom_ok[p]) return false;
    char v[80];
    if (!pad_map_to_text(custom[p], spec->nbuttons, v, sizeof(v))) return false;
    return s->extra_set(map_keys[p], v);
}

bool pad_state(int p, uint32_t *b, uint8_t *d)
{
    Guard g;
    if (p < 0 || p >= PAD_PLAYERS || !slots[p].used) return false;
    *b = slots[p].raw.buttons;
    *d = slots[p].raw.dpad;
    return true;
}

void pad_capture_start(int p)
{
    Guard g;
    cap.active = true;
    cap.player = p;
    cap.armed = false;
    cap.got = false;
    cap.prev = slots[p].used ? pressed_set(slots[p].raw) : 0;
    if (cap.prev == 0) cap.armed = true;
}

bool pad_capture_poll(uint8_t *src)
{
    Guard g;
    if (!cap.got) return false;
    *src = cap.src;
    cap.got = false;
    return true;
}

void pad_capture_stop()
{
    Guard g;
    cap.active = false;
    cap.got = false;
}

bool pad_capturing() { return cap.active; }

const char *pad_src_name(uint8_t s, char *buf, int n)
{
    static const char *const dir[4] = { "Up", "Down", "Left", "Right" };
    if (s < 32) snprintf(buf, n, "Button %d", s + 1);
    else if (s < 36) snprintf(buf, n, "%s", dir[s - 32]);
    else snprintf(buf, n, "none");
    return buf;
}

const uint8_t *pad_descriptor(int player, int *len)
{
    if (player < 0 || player >= PAD_PLAYERS || !slots[player].used) {
        *len = 0;
        return 0;
    }
    *len = slots[player].desc_len;
    return slots[player].desc;
}

int pad_desc_dumps(int player)
{
    return (player >= 0 && player < PAD_PLAYERS) ? slots[player].dumps : 0;
}
