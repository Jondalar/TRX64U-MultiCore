#include "engines.h"
#include "video.h"
#include "mouse_api.h"
#include "pad_api.h"
#include "libtab.h"
#include <stdio.h>
#include <string.h>

// The tables of the mouse and pad libraries (libtab.h): none in an app linked without them, and then there is no mouse and no pad.
static const MouseApi *mouse_lib(void)
{
    const MouseApi *a = (const MouseApi *)libtab_table(MOUSE_API_NAME);
    return a && a->size >= sizeof(MouseApi) ? a : 0;
}

static const PadApi *pad_lib(void)
{
    const PadApi *a = (const PadApi *)libtab_table(PAD_API_NAME);
    return a && a->size >= sizeof(PadApi) ? a : 0;
}

Engines *engines_of(CoreCtx *c)
{
    if (!c->eng) {
        c->eng = new Engines();                   // value-initialised: every slot empty, no list
        if (const MouseApi *m = mouse_lib()) {
            m->configure(c->desc ? c->desc->mouse : 0);
            m->reset();
        }
        rawkeys_configure(c->desc ? c->desc->rawkeys : 0);
    }
    return c->eng;
}

int media_writeback(CoreCtx *c, WbWhy why, char *msg, int msglen)
{
    msg[0] = 0;
    char m[200];
    int bad = 0;
    const CoreDesc *d = c->desc;
    MediaSaving saving(c, wb_pending() || win_pending(c) || sec_pending(c) || (d && d->media_pending && d->media_pending(c)));
    if (wb_flush(c, why, m, sizeof(m))) {
        snprintf(msg, msglen, "%s", m);
        bad++;
    }
    m[0] = 0;
    if (!win_flush_all(c, m, sizeof(m)) && !bad++) {
        snprintf(msg, msglen, "%s", m);
    }
    m[0] = 0;
    if (!sec_sync_all(c, m, sizeof(m)) && !bad++) {
        snprintf(msg, msglen, "%s", m);
    }
    m[0] = 0;
    if (d && d->media_flush && d->media_flush(c, m, sizeof(m)) && !bad++) {         // the core's own media engine (apple2, atari, cpc, drive, saves)
        snprintf(msg, msglen, "%s", m);
    }
    return bad ? 1 : 0;
}

void engines_before_hold(CoreCtx *c)
{
    if (!c->eng) {
        return;
    }
    win_flush_all(c);                             // a disk is consistent in its file before the machine starts over
    sec_sync_all(c);
}

void engines_after_hold(CoreCtx *c)
{
    if (!c->eng) {
        return;
    }
    win_reannounce(c);
    sec_reannounce(c);
    rawkeys_reset();                              // the machine starts with no key down
}

// The core was loaded again (Reload core, T104 5.14): its registers are at the configuration values. The announcement of the images is the
// core's hold (engines_after_hold, called by its init); what is left is the state that remembers what was written into the core: the picture
// window is at the gateware's default again, so the look at the display window starts from there.
void engines_after_reload(CoreCtx *c)
{
    if (const PadApi *p = pad_lib()) {
        p->reset();                               // the pad registers are at zero again: the next pace writes the words
    }
    if (!c->eng) {
        return;
    }
    memset(&c->eng->fw, 0, sizeof(c->eng->fw));
    if (c->video) {
        c->video->set_view(0, 0);                 // the layer shows the whole picture again
    }
    if (const MouseApi *m = mouse_lib()) {
        m->reset();
    }
    rawkeys_reset();
}

// ---- the operations ----
static int op_win_mount(CoreCtx *c, const OpArgs &a, OpOut &o) { return win_mount(c, a.i(1), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_win_eject(CoreCtx *c, const OpArgs &a, OpOut &o) { return win_eject(c, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_sec_mount(CoreCtx *c, const OpArgs &a, OpOut &o) { return sec_mount(c, a.i(1), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_sec_eject(CoreCtx *c, const OpArgs &a, OpOut &o) { return sec_eject(c, a.i(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

static int op_sec_mount_folder(CoreCtx *c, const OpArgs &a, OpOut &o) { return sec_mount_folder(c, a.i(1), a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

static int op_romset_put(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (!c->desc->romset) {
        snprintf(o.msg, o.msglen, "This machine has no ROM set.");
        return OPE_UNSUPPORTED;
    }
    return romset_put(c, a.p(0), a.len(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// The service: the sector mailbox first (a disk is read sector after sector, so a pass that served any asks to be called again at once), then
// the dirty log and its flush policy, then the idle sync, then the mouse, then the USB keyboard's key events (rawkeys.h), then the picture window (fbwin.h).
bool engines_poll(CoreCtx *c)
{
    engines_of(c);
    bool more = sec_service(c, 16) != 0;
    win_service(c);
    sec_idle(c);
    if (const MouseApi *m = mouse_lib()) {
        m->pace(c);
    }
    rawkeys_pace(c);
    if (const PadApi *p = pad_lib()) {
        p->pace(c);
    }
    fbwin_poll(c);
    return more;
}

static int op_led_load(CoreCtx *c, const OpArgs &a, OpOut &) { ledload_set(c, a.i(0) == 0); return OPE_OK; }

static int op_mouse_sens(CoreCtx *, const OpArgs &a, OpOut &)
{
    if (const MouseApi *m = mouse_lib()) {
        m->set_sensitivity(a.i(0));
    }
    return OPE_OK;
}
static int op_mouse_accel(CoreCtx *, const OpArgs &a, OpOut &)
{
    if (const MouseApi *m = mouse_lib()) {
        m->set_acceleration(a.i(0));
    }
    return OPE_OK;
}

static int op_poll(CoreCtx *c, const OpArgs &, OpOut &o)
{
    o.more = engines_poll(c);
    return OPE_OK;
}

static const ArgSpec win_mount_args[] = { ARG_PATH("path"), ARG_INT("slot", 0, WIN_SLOTS_MAX - 1) };
static const ArgSpec win_eject_args[] = { ARG_INT("slot", 0, WIN_SLOTS_MAX - 1) };
static const ArgSpec sec_mount_args[] = { ARG_PATH("path"), ARG_INT("unit", 0, SEC_UNITS_MAX - 1) };
static const ArgSpec sec_folder_args[] = { ARG_STRING("path", 255), ARG_INT("unit", 0, SEC_UNITS_MAX - 1) };   // the menu's typed text: "<folder>/<GB>"
static const ArgSpec sec_eject_args[] = { ARG_INT("unit", 0, SEC_UNITS_MAX - 1) };
static const char *const led_values[] = { "On", "Off" };
static const ArgSpec led_args[] = { ARG_ENUM("value", led_values, 2) };
static const ArgSpec mouse_sens_args[] = { ARG_INT("value", 1, 16) };
static const char *const mouse_accel_values[] = { "Off", "Adaptive" };
static const ArgSpec mouse_accel_args[] = { ARG_ENUM("value", mouse_accel_values, 2) };
static const ArgSpec put_args[] = { ARG_BLOB("data") };

// T192.3 (the controller's "Return to host menu", menutree.cc FixedDef::pre_op): the media are written back first; one that cannot be written back keeps the core.
static int op_flush_for_core(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (media_writeback(c, WB_WHY_CORE, o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    return OPE_OK;
}

void engines_register(OpRegistry &r)
{
    r.add_op("window.mount", win_mount_args, 2, op_win_mount);
    r.add_op("window.eject", win_eject_args, 1, op_win_eject);
    r.add_op("sectors.mount", sec_mount_args, 2, op_sec_mount);
    r.add_op("sectors.eject", sec_eject_args, 1, op_sec_eject);
    r.add_op("sectors.mount_folder", sec_folder_args, 2, op_sec_mount_folder);
    r.add_state("sectors.folder.offer", sec_folder_offer);
    r.add_op("romset.put", put_args, 1, op_romset_put);
    r.add_op("led.load", led_args, 1, op_led_load);
    r.add_op("mouse.sens", mouse_sens_args, 1, op_mouse_sens);
    r.add_op("mouse.accel", mouse_accel_args, 1, op_mouse_accel);
    r.add_op("media.poll", 0, 0, op_poll);
    r.add_op("media.flush_for_core", 0, 0, op_flush_for_core);
}
