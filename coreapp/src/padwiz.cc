// T156 D5, T192.3 leak 1: "Map buttons" - the core's buttons one by one: the prompt names a button, the next press on the pad is its source. RETURN keeps the map's entry for
// it, the left arrow (the C64 key, '`' in the key map) and DEL go back one, RUN/STOP and ESC drop everything without saving. At the end the map is kept in the .cfg ("Pad N map"),
// the setting "Pad map" becomes Custom and the pad uses it at once.
//
// The wizard is this module's, behind operations and states (pad.map.*); the menu controller only knows that a wizard is up (menuflows.cc names its states and operations),
// shows the three texts it gives and sends the keys of its stage (menukeytab.cc KS_WIZARD) to its operations. The run loop's pump calls pad.map.tick while it is up.
#include "pad.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

namespace {

bool g_on;
bool g_wait;                                                       // T225 D3: the dialog is up and waits for a pad to be plugged in
int g_player, g_i;
uint8_t g_new[16];

// A wizard step's answer: text for the status line, and whether it is bad.
int say(OpOut &o, int bad, const char *fmt, int a = 0, int b = 0)
{
    snprintf(o.msg, o.msglen, fmt, a, b);
    return bad ? OPE_FAILED : OPE_OK;
}

int finish(CoreCtx *c, OpOut &o)
{
    const PadSpec *p = c->desc->pad;
    g_on = false;
    g_wait = false;
    pad_capture_stop();
    pad_set_custom(g_player, g_new);
    if (p->by_id) {                                                  // T225: the map is the pad's (VID:PID) and the target's, not the player's
        int sl = pad_slot(g_player);
        if (c->settings && pad_save_id_map(c->settings, g_player)) {
            return say(o, 0, "Port 2: mapped and saved for pad %04X:%04X.", pad_vid(sl), pad_pid(sl));
        }
        return say(o, 1, "Port 2: mapped for this session only (the settings file cannot be written).");
    }
    bool saved = c->settings && pad_save_map(c->settings, g_player);
    int mi = c->settings ? c->settings->find("Pad map") : -1;
    if (mi >= 0) {
        c->settings->set(mi, PAD_MODE_CUSTOM);
    }
    pad_set_mode(PAD_MODE_CUSTOM);
    if (saved) {
        return say(o, 0, "Pad %d: %d buttons mapped and saved. Pad map is now Custom.", g_player + 1, (int)p->nbuttons);
    }
    return say(o, 1, "Pad %d: %d buttons mapped, for this session only (the settings file cannot be written). Pad map is now Custom.", g_player + 1, (int)p->nbuttons);
}

void start_capture(const PadSpec *p, int player)
{
    memcpy(g_new, pad_active_map(player), p->nbuttons);
    g_on = true;
    g_wait = false;
    g_player = player;
    g_i = 0;
    pad_capture_start(pad_slot(player));
}

int begin(CoreCtx *c, int player, OpOut &o)
{
    const PadSpec *p = c->desc->pad;
    pad_ensure(c);
    if (!p || player < 0 || player >= PAD_PLAYERS) {
        snprintf(o.msg, o.msglen, "This core has no pad support");
        return OPE_FAILED;
    }
    uint32_t b;
    uint8_t d;
    if (!pad_state(pad_slot(player), &b, &d)) {
        if (!p->by_id) {
            return say(o, 1, "No pad is connected as pad %d", player + 1);
        }
        g_on = true;                                                       // T225 D3: open anyway, tick continues when a pad appears
        g_wait = true;
        g_player = player;
        g_i = 0;
        o.msg[0] = 0;
        return OPE_OK;
    }
    start_capture(p, player);
    o.msg[0] = 0;
    return OPE_OK;
}

int op_begin(CoreCtx *c, const OpArgs &a, OpOut &o) { return begin(c, a.i(0), o); }
int op_map_1(CoreCtx *c, const OpArgs &, OpOut &o) { return begin(c, 0, o); }
int op_map_2(CoreCtx *c, const OpArgs &, OpOut &o) { return begin(c, 1, o); }
const ArgSpec begin_args[] = { ARG_INT("player", 0, PAD_PLAYERS - 1) };

int op_next(CoreCtx *c, const OpArgs &, OpOut &o)
{
    o.msg[0] = 0;
    if (!g_on) {
        return OPE_FAILED;
    }
    if (g_wait) {
        return OPE_OK;
    }
    g_i++;
    if (g_i >= pad_nbuttons()) {
        return finish(c, o);
    }
    pad_capture_start(pad_slot(g_player));
    return OPE_OK;
}

int op_back(CoreCtx *, const OpArgs &, OpOut &o)
{
    o.msg[0] = 0;
    if (!g_on) {
        return OPE_FAILED;
    }
    if (g_wait) {
        return OPE_OK;
    }
    if (g_i > 0) {
        g_i--;
    }
    pad_capture_start(pad_slot(g_player));
    return OPE_OK;
}

int op_cancel(CoreCtx *, const OpArgs &, OpOut &o)
{
    o.msg[0] = 0;
    if (!g_on) {
        return OPE_FAILED;
    }
    g_on = false;
    g_wait = false;
    pad_capture_stop();
    snprintf(o.msg, o.msglen, "Mapping cancelled: nothing was saved");
    return OPE_OK;
}

// One look at the pad: `more` says the screen changed (a button was taken, the wizard ended, the pad is gone); the text, if any, is the status line.
int op_tick(CoreCtx *c, const OpArgs &, OpOut &o)
{
    o.msg[0] = 0;
    o.more = false;
    if (!g_on) {
        return OPE_OK;
    }
    if (g_wait) {                                                          // T225 D3: waiting for a pad
        uint32_t wb;
        uint8_t wd;
        if (pad_state(pad_slot(g_player), &wb, &wd)) {
            start_capture(c->desc->pad, g_player);
            o.more = true;
        }
        return OPE_OK;
    }
    uint8_t src;
    if (!pad_capture_poll(&src)) {
        uint32_t b;
        uint8_t d;
        if (!pad_state(pad_slot(g_player), &b, &d)) {                          // the pad was pulled
            g_on = false;
            g_wait = false;
            pad_capture_stop();
            o.more = true;
            snprintf(o.msg, o.msglen, "The pad is gone: mapping cancelled, nothing was saved");
            return OPE_FAILED;
        }
        return OPE_OK;
    }
    o.more = true;
    g_new[g_i] = src;
    g_i++;
    if (g_i >= pad_nbuttons()) {
        return finish(c, o);
    }
    pad_capture_start(pad_slot(g_player));
    return OPE_OK;
}

const char *st_active(CoreCtx *) { return g_on ? "1" : 0; }
const char *st_title(CoreCtx *c) { return c->desc->pad && c->desc->pad->by_id ? "Map port 2 to USB pad" : "Map buttons"; }

const char *st_left(CoreCtx *c)
{
    static char t[64];
    if (!g_on) {
        return 0;
    }
    if (g_wait) {
        return "Plug in a USB pad";
    }
    snprintf(t, sizeof(t), "Press the pad button for %s", c->desc->pad->names[g_i]);
    return t;
}

const char *st_right(CoreCtx *c)
{
    static char t[24];
    if (!g_on) {
        return 0;
    }
    if (g_wait) {
        return "";
    }
    snprintf(t, sizeof(t), "%d of %d", g_i + 1, pad_nbuttons());
    return t;
}

const char *st_sub(CoreCtx *c)
{
    static char t[120];
    char was[24];
    if (!g_on) {
        return 0;
    }
    if (g_wait) {
        return "No USB pad is connected. The mapping continues when you plug one in. RUN/STOP cancels";
    }
    if (c->desc->pad->by_id) {
        snprintf(t, sizeof(t), "%s %04X:%04X. Now: %s. RETURN keeps it, left arrow goes back, RUN/STOP cancels", pad_name(pad_slot(g_player)), pad_vid(pad_slot(g_player)),
                 pad_pid(pad_slot(g_player)), pad_src_name(g_new[g_i], was, sizeof(was)));
        return t;
    }
    snprintf(t, sizeof(t), "Pad %d. Now: %s. RETURN keeps it, left arrow goes back, RUN/STOP cancels", g_player + 1, pad_src_name(g_new[g_i], was, sizeof(was)));
    return t;
}

}

int pad_wizard_begin(CoreCtx *c, int player, OpOut &o)
{
    return begin(c, player, o);
}

void padwiz_register(OpRegistry &r)
{
    r.add_op("pad.map.begin", begin_args, 1, op_begin);
    r.add_op("pad.map.next", 0, 0, op_next);
    r.add_op("pad.map.back", 0, 0, op_back);
    r.add_op("pad.map.cancel", 0, 0, op_cancel);
    r.add_op("pad.map.tick", 0, 0, op_tick);
    r.add_op("menu.pad_map_1", 0, 0, op_map_1);
    r.add_op("menu.pad_map_2", 0, 0, op_map_2);
    r.add_op("menu.pad_map_port", 0, 0, op_map_1);                  // T225: the Amiga cores' game port 2 (one port, the first pad plugged)
    r.add_state("pad.map.active", st_active);
    r.add_state("pad.map.title", st_title);
    r.add_state("pad.map.left", st_left);
    r.add_state("pad.map.right", st_right);
    r.add_state("pad.map.sub", st_sub);
}
