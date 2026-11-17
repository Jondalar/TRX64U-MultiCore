#include "picture.h"
#include "video.h"
#include "videomode.h"
#include "cfgfile.h"
#include "fsif.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// The state of the one picture this application owns (one core runs at a time). picture_extend sets it, picture_start / picture_apply /
// picture_service move it.
static uint16_t g_mask = EDID_ROW_MASK;              // the rows the screen takes
static uint16_t g_native = 0;                        // the rows it flags as native
static int g_msys = VID_SYS_PAL50;                   // the machine's 50 / 60 Hz family (Q6)
static bool g_gw = false;                            // the gateware takes the mode (status register answered)
static int g_list[VID_HDMI_COUNT], g_nlist = 0;      // the resolutions the Picture item lists: value n + 1 is g_list[n]
static int g_hd_hdmi = -1;            // the host's default: the mode the host handed over (D11c)
static int g_host_hdmi = -1;          // T164 D10c: the resolution the host's own Picture is set to (host.cfg [Video] Mode); -1: "From the screen", no file, unreadable
static int g_last_value = 0;                         // the Picture value in force
static uint32_t g_sig = 0;                           // the signature of the screen last read
static bool g_have_sig = false;
static uint32_t g_next_poll = 0;
static bool g_poll_armed = false;
static bool g_vic_scale = false;                     // T222: the core's picture is the VIC through the gateware's scaler, which has a scale register
static bool g_list_dirty = false;                    // the Picture list was rebuilt outside picture_service (the C128's System Mode): the next picture_service says so
static void set_machine_sys(CoreCtx *c, int sys);

// T222: the VIC path's gateware (mode_req.v) replaces the family of every request by the System Mode's (60 Hz: NTSC and PAL-60, else 50 Hz), so the
// application's family is that one: the list, the Hz shown and the mode kept in video_current are the row that really runs.
static int vic_family(int sys)
{
    if (!g_vic_scale || sys < 0) {
        return sys;
    }
    return (sys == VID_SYS_NTSC60 || sys == VID_SYS_PAL60) ? VID_SYS_NTSC60 : VID_SYS_PAL50;
}

// "Auto" and then the resolutions the screen takes. The first value says where its mode comes from (T164 D10c amended): "Auto (1280 x 720 from host)" when the core
// uses the mode the host stored, "Auto (1920 x 1080 from screen)" when it uses what it picked from the screen; plain "Auto" until the picture starts.
static char g_auto_text[40] = "Auto";
static const char *g_values[1 + VID_HDMI_COUNT];
static const char *g_scale_values[VID_SCALE_COUNT];     // T107 D8: Integer, Fit, Fill
static const char *const g_smooth_values[2] = { "Off", "On" };

static void drop_unlocked(void);

static void fill_values(void)
{
    g_nlist = video_modes_hdmi_list(g_mask, g_msys, g_list);
    if (g_nlist == 0) {                                // a mask that names nothing: the television rows (Q9)
        g_mask = EDID_LIST_NONE;
        g_nlist = video_modes_hdmi_list(g_mask, g_msys, g_list);
    }
    drop_unlocked();
    g_values[0] = g_auto_text;
    for (int i = 0; i < g_nlist; i++) {
        g_values[1 + i] = video_hdmi_name(g_list[i]);
    }
}

// T222: the gateware's VIC path locks 576p / 480p, 720p, 1080p and 800 x 600 only (U16 D2): 1024 x 768 and 1280 x 1024 are not offered
static void drop_unlocked(void)
{
    if (!g_vic_scale) {
        return;
    }
    int n = 0;
    for (int i = 0; i < g_nlist; i++) {
        if (g_list[i] <= VID_HDMI_800x600) {
            g_list[n++] = g_list[i];
        }
    }
    g_nlist = n;
}

static int current_sys(void)
{
    int sys = -1, hdmi = -1;
    if (!video_unpack(video_current, &sys, &hdmi) || sys < 0) {
        return VID_SYS_PAL50;
    }
    return sys;
}

const char *picture_extend(const CoreDesc *d, CoreDesc *out, Setting *store, const PictureEnv *env)
{
    g_mask = env ? (env->mask & EDID_ROW_MASK) : EDID_ROW_MASK;
    g_native = env ? env->native : 0;
    g_gw = env ? env->gateware : false;
    g_msys = (env && env->sys >= 0) ? env->sys : current_sys();
    bool scaled = g_gw && d->video.w != 0 && d->nsettings + 3 <= SETTINGS_MAX;             // the frame buffer goes through the scaler (G14); a core with its own picture has no layer to scale
    bool vic_scaled = g_gw && env && env->vic_scale && d->video.w == 0 && d->nsettings + 2 <= SETTINGS_MAX;   // T222: the VIC path's scale (0x4F01)
    bool vic_smooth = vic_scaled && env->vic_smooth && d->nsettings + 3 <= SETTINGS_MAX;                    // T227: and its smoothing (0x4F0B)
    g_vic_scale = vic_scaled;
    g_msys = vic_family(g_msys);
    int extra = (scaled || vic_smooth) ? 3 : vic_scaled ? 2 : 1;       // Picture; Scale and Smoothing for a frame-buffer core on the gateware and for the VIC path with the smoothing register; Picture and Scale for an older VIC path
    if (d->nsettings + extra > SETTINGS_MAX) {
        return "settings: no room for the Picture item the application adds";
    }
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, PICTURE_KEY) || !strcasecmp(d->settings[i].key, SCALE_KEY) || !strcasecmp(d->settings[i].key, SMOOTH_KEY)) {
            return "settings: the keys Picture, Scale and Smoothing belong to the application";
        }
        store[i] = d->settings[i];
    }
    snprintf(g_auto_text, sizeof(g_auto_text), "Auto");        // the text of a core that has not started its picture: picture_start names the mode
    fill_values();
    Setting &p = store[d->nsettings];
    p = Setting();                                 // every field, `bind` too: the store may be stack memory
    p.key = PICTURE_KEY;
    p.values = g_values;
    p.nvalues = 1 + g_nlist;
    p.def = 0;
    p.when = WHEN_LIVE;
    p.apply = PICTURE_OP;
    p.group = "video";                             // the TRX64U menu lists it on the Video page (HDMI Scan Resolution); a core whose menu has no such group shows it where it did
    if (vic_scaled) {
        for (int m = 0; m < VID_SCALE_COUNT; m++) {
            g_scale_values[m] = video_scale_name(m);
        }
        Setting &q = store[d->nsettings + 1];
        q = Setting();
        q.key = SCALE_KEY;
        q.values = g_scale_values;
        q.nvalues = VID_SCALE_COUNT;
        q.def = VID_SCALE_FIT;                         // the gateware powers up at Fit (U16 D3)
        q.when = WHEN_LIVE;
        q.apply = SCALE_OP;
        q.group = "video";
    }
    if (vic_smooth) {
        Setting &r = store[d->nsettings + 2];
        r = Setting();
        r.key = SMOOTH_KEY;
        r.values = g_smooth_values;
        r.nvalues = 2;
        r.def = 0;                                     // the gateware powers up with the smoothing off (T227)
        r.when = WHEN_LIVE;
        r.apply = SMOOTH_OP;
        r.group = "video";
    }
    if (scaled) {
        for (int m = 0; m < VID_SCALE_COUNT; m++) {
            g_scale_values[m] = video_scale_name(m);
        }
        Setting &q = store[d->nsettings + 1];
        q = Setting();
        q.key = SCALE_KEY;
        q.values = g_scale_values;
        q.nvalues = VID_SCALE_COUNT;
        q.def = VID_SCALE_INTEGER;
        q.when = WHEN_LIVE;
        q.apply = SCALE_OP;
        q.group = "video";
        Setting &r = store[d->nsettings + 2];
        r = Setting();
        r.key = SMOOTH_KEY;
        r.values = g_smooth_values;
        r.nvalues = 2;
        r.def = 0;
        r.when = WHEN_LIVE;
        r.apply = SMOOTH_OP;
        r.group = "video";
    }
    *out = *d;
    out->settings = store;
    out->nsettings = d->nsettings + extra;
    return 0;
}

void picture_env_read(PictureEnv *env, int sys)
{
    static uint8_t edid[256];
    int len = 0;
    if (!video_read_edid(edid, sizeof(edid), &len)) {
        len = 0;
    }
    env->mask = video_modes_offered(edid, len);
    EdidInfo info;
    env->native = len > 0 && edid_parse(edid, len, &info) ? (uint16_t)(info.native & env->mask) : 0;
    env->gateware = video_gateware_present();
    env->vic_scale = video_gateware_vic_scale();
    env->vic_smooth = video_gateware_vic_smooth();
    env->sys = sys;
}

int picture_index(const CoreDesc *d)
{
    for (int i = 0; i < d->nsettings; i++) {
        if (d->settings[i].apply && !strcmp(d->settings[i].apply, PICTURE_OP) && !strcasecmp(d->settings[i].key, PICTURE_KEY)) {
            return i;
        }
    }
    return -1;
}

// op and canonical key: a core's record may carry a hidden setting with the same op (T228 H1); the application's row is the one that counts
static int index_of_op(const CoreDesc *d, const char *op, const char *key)
{
    for (int i = 0; i < d->nsettings; i++) {
        if (d->settings[i].apply && !strcmp(d->settings[i].apply, op) && !strcasecmp(d->settings[i].key, key)) {
            return i;
        }
    }
    return -1;
}

int scale_index(const CoreDesc *d)  { return index_of_op(d, SCALE_OP, SCALE_KEY); }
int smooth_index(const CoreDesc *d) { return index_of_op(d, SMOOTH_OP, SMOOTH_KEY); }

// A mode of the machine's family and the resolution the person means. With the gateware the row decides the family (Q6: the other one only
// when the screen has nothing in the machine's); the firmware path of an old host has one family, the machine's.
struct Pick { int sys, hdmi; };

static bool resolve_pick(int hdmi, Pick *p)
{
    int row;
    if (!video_modes_resolve(g_mask, g_msys, hdmi, &row)) {
        return false;
    }
    p->sys = g_gw && !g_vic_scale ? video_row_sys(row) : g_msys;      // the VIC path runs the System Mode's family only
    p->hdmi = video_row_hdmi(row);
    return true;
}

static Pick best_pick(void)
{
    int row = video_modes_best(g_mask, g_msys);
    Pick p;
    if (row < 0) {
        p.sys = g_msys;
        p.hdmi = VID_HDMI_480_576;
    } else {
        p.sys = g_gw && !g_vic_scale ? video_row_sys(row) : g_msys;
        p.hdmi = video_row_hdmi(row);
    }
    return p;
}

static int current_hdmi(CoreCtx *c)
{
    if (c->video->mode() >= 0) {
        return c->video->mode();
    }
    int sys = -1, hdmi = -1;
    return video_unpack(video_current, &sys, &hdmi) ? hdmi : -1;
}

// The mode a value asks for (D11c, D10c): the core's own choice when the screen takes it; else (Auto, or a choice that is gone) the host's own Picture
// choice (host.cfg) when it names a resolution the screen takes, else the mode the host handed over when the screen takes that; else the best the screen offers. Without the gateware Auto is still what the picture fits in (ADR-0057: the frame is fixed
// at 1080p there and the mode is the only lever). A core that draws its own picture (Video::own_picture) has no wish, and Auto then leaves the
// screen in the mode it is in (`now`, -1 when nobody knows).
static Pick target(CoreCtx *c, int value, int now)
{
    Pick p;
    if (g_nlist == 0) {                            // a caller that never built the items (the list is the defaults')
        fill_values();
    }
    if (value > 0 && value - 1 < g_nlist && resolve_pick(g_list[value - 1], &p)) {
        return p;
    }
    if (g_gw) {
        if (c->video->own_picture()) {
            p.sys = current_sys();
            p.hdmi = now;
            return p;
        }
        if (g_host_hdmi >= 0 && resolve_pick(g_host_hdmi, &p)) {         // D10c: Auto is the host's own choice ...
            return p;
        }
        if (g_hd_hdmi >= 0 && resolve_pick(g_hd_hdmi, &p)) {             // ... else the mode the host had up
            return p;
        }
        return best_pick();
    }
    if (g_host_hdmi >= 0 && !c->video->own_picture() && resolve_pick(g_host_hdmi, &p)) {      // D10c, without the gateware too
        return p;
    }
    int w = c->video->wanted_hdmi();
    if (w < 0) {
        p.sys = g_msys;
        p.hdmi = now;
        return p;
    }
    if (resolve_pick(w, &p)) {
        return p;
    }
    return best_pick();
}

void picture_modes(CoreCtx *c, PictureModes *out)
{
    memset(out, 0, sizeof(*out));
    if (g_nlist == 0) {
        fill_values();
    }
    int now = c->video ? current_hdmi(c) : -1;                  // a rig with no video (a bench) lists the names only
    int pi = picture_index(c->desc);
    out->cur = pi >= 0 && c->settings ? c->settings->value(pi) : g_last_value;
    out->no_edid = g_mask == EDID_LIST_NONE;
    out->n = 1 + g_nlist;
    Pick a = { 0, -1 };
    if (c->video) {
        a = target(c, 0, now);
    }
    for (int i = 0; i < out->n; i++) {
        int hdmi = i == 0 ? a.hdmi : g_list[i - 1];
        picmodes_set_row(out, i, g_values[i], hdmi, g_mask, g_native, g_msys, now, out->cur == 0);
    }
}


// The text of the first value: the mode Auto asks for now, and whose it is. The host's when it is the mode its host.cfg stores, else the screen's (what the core
// picked from the EDID, or the mode the host had up when it stored none).
static void auto_label(CoreCtx *c, int now)
{
    Pick p = target(c, 0, now);
#ifdef TRX_AUTO_PLAIN                              // render bench control: the old plain text
    p.hdmi = -1;
#endif
    if (p.hdmi < 0 || p.hdmi >= VID_HDMI_COUNT) {
        snprintf(g_auto_text, sizeof(g_auto_text), "Auto");
        return;
    }
    snprintf(g_auto_text, sizeof(g_auto_text), "Auto (%s from %s)", video_hdmi_name(p.hdmi), g_host_hdmi >= 0 && p.hdmi == g_host_hdmi ? "host" : "screen");
}

static bool apply_mode(int sys, int hdmi)
{
    if (g_gw) {
        return video_apply_gateware(sys, hdmi);
    }
    video_apply(sys, hdmi);
    return true;
}

// ---- Q7: confirm or revert --------------------------------------------------------------------------------------------------------
static struct {
    bool active, fresh, end;
    uint32_t t0;
    int prev_sys, prev_hdmi, prev_value;
    int new_sys, new_hdmi;
} g_cf;
static char g_result[80];                          // T187 D4/D5: what the chip says once the question ended ("Picture 1280 x 720", "Picture: back to ...")

static void mode_text(int sys, int hdmi, char *out, int n)
{
    picmodes_mode_text(g_mask, g_gw ? (sys ? VID_SYS_NTSC60 : VID_SYS_PAL50) : sys, hdmi, out, n);
}

void picture_confirm_names(char *now, int nn, char *prev, int pn)
{
    mode_text(g_cf.new_sys, g_cf.new_hdmi, now, nn);
    mode_text(g_cf.prev_sys, g_cf.prev_hdmi, prev, pn);
}

const char *picture_take_result(void)
{
    static char r[80];
    snprintf(r, sizeof(r), "%s", g_result);
    g_result[0] = 0;
    return r[0] ? r : 0;
}

static uint32_t g_cf_now;                          // the time of the last picture_service

bool picture_confirm_pending() { return g_cf.active; }

int picture_confirm_left_s()
{
    if (!g_cf.active) {
        return 0;
    }
    uint32_t used = g_cf_now - g_cf.t0;
    return used >= PICTURE_CONFIRM_MS ? 0 : (int)((PICTURE_CONFIRM_MS - used + 999) / 1000);
}

void picture_confirm_key()
{
    if (g_cf.active) {
        g_cf.active = false;
        g_cf.end = true;
        char t[48];
        mode_text(g_cf.new_sys, g_cf.new_hdmi, t, sizeof(t));
        picmodes_kept_text(t, g_result, sizeof(g_result));
    }
}

void picture_confirm_revert()
{
    if (g_cf.active) {
        g_cf.t0 = g_cf_now - PICTURE_CONFIRM_MS;       // the time is up: the next picture_service puts the previous mode back
    }
}

void scale_apply(CoreCtx *c, int value)
{
    if (g_vic_scale) {                             // T222: the VIC's picture is scaled by the gateware, at 0x4F01
        if (!video_set_scale_gateware(value)) {
            printf("[core] scale: the gateware did not take %s\n", video_scale_name(value));
        }
        return;
    }
    if (c->video) {
        c->video->set_scale_mode(value);
    }
}

void smooth_apply(CoreCtx *c, int value)
{
    if (g_vic_scale) {                             // T227: the VIC's picture is smoothed by the gateware, at 0x4F0B
        if (!video_set_smooth_gateware(value == 1 ? 1 : 0)) {
            printf("[core] smoothing: the gateware did not take %s\n", value == 1 ? "On" : "Off");
        }
        return;
    }
    if (c->video) {
        c->video->set_smoothing(value == 1);
    }
}

void picture_apply(CoreCtx *c, int value)
{
    if (!c->video) {
        return;
    }
    int now = current_hdmi(c), nsys = current_sys();
    Pick p = target(c, value, now);
    bool changed = false;
    if (p.hdmi >= 0 && (p.hdmi != now || (g_gw && p.sys != nsys))) {
        printf("[core] picture: %s (%s)\n", video_hdmi_name(p.hdmi), value <= 0 ? "auto" : "chosen");
        if (!apply_mode(p.sys, p.hdmi)) {
            printf("[core] picture: the mode did not come up, the previous one stays\n");
            p.sys = nsys;
            p.hdmi = now;
        } else {
            video_current = video_pack(p.sys, p.hdmi);
            changed = true;
        }
    }
    c->video->set_machine_sys(g_msys);
    c->video->set_mode(p.hdmi, p.sys);
    if (changed) {                                 // Q7 (every change - menu, REST, web - asks; run.cc shows the question with the menu closed too): any key keeps it, else the previous mode and the previous item come back
        g_cf.active = true;
        g_cf.fresh = true;
        g_cf.t0 = c->io->now_ms();
        g_cf.prev_sys = nsys;
        g_cf.prev_hdmi = now;
        g_cf.prev_value = g_last_value;
        g_cf.new_sys = p.sys;
        g_cf.new_hdmi = p.hdmi;
    }
    g_last_value = value;
}

void picture_set_system(CoreCtx *c, int sys)
{
    if (!c->video || sys < 0) {
        return;
    }
    sys = vic_family(sys);
    c->video->set_machine_sys(sys);                // the capture's family is the machine's, before the early return below
    int hdmi = current_hdmi(c);
    if (hdmi < 0) {
        hdmi = VID_HDMI_1080P;
    }
    int now_sys = -1, now_hdmi = -1;
    bool known = video_unpack(video_current, &now_sys, &now_hdmi);
    if (known && now_sys == sys && now_hdmi == hdmi) {
        return;
    }
    printf("[core] system mode: %s\n", video_sys_name(sys));
    if (!apply_mode(sys, hdmi)) {
        printf("[core] system mode: the mode did not come up, the previous one stays\n");
        return;
    }
    video_current = video_pack(sys, hdmi);
    set_machine_sys(c, sys);
    g_list_dirty = true;
    c->video->set_mode(hdmi, sys);
}

static int op_picture(CoreCtx *c, const OpArgs &a, OpOut &)
{
    picture_apply(c, a.i(0));
    return OPE_OK;
}
static int op_scale(CoreCtx *c, const OpArgs &a, OpOut &)
{
    scale_apply(c, a.i(0));
    return OPE_OK;
}
static int op_smooth(CoreCtx *c, const OpArgs &a, OpOut &)
{
    smooth_apply(c, a.i(0));
    return OPE_OK;
}
static const ArgSpec scale_args[] = { ARG_INT("value", 0, VID_SCALE_COUNT - 1) };
static const ArgSpec smooth_args[] = { ARG_INT("value", 0, 1) };
static const ArgSpec picture_args[] = { ARG_INT("value", 0, VID_HDMI_COUNT) };

// The picture as the descriptor states it, for the Home panel (menuflows.h MenuFacts): the scale it is at now, not the descriptor's.
static const char *st_video_line(CoreCtx *c)
{
    static char t[64];
    const VideoDesc &v = c->desc->video;
    if (!v.w) {
        return "the core's own picture";
    }
    unsigned scale = c->video ? c->video->scale() : v.scale;
    snprintf(t, sizeof(t), "%u x %u, scale %u", (unsigned)v.w, (unsigned)v.h, scale);
    return t;
}

void picture_register(OpRegistry &r)
{
    r.add_state("video.line", st_video_line);
    r.add_op(PICTURE_OP, picture_args, 1, op_picture);
    r.add_op(SCALE_OP, scale_args, 1, op_scale);
    r.add_op(SMOOTH_OP, smooth_args, 1, op_smooth);
}

static void note_screen(void)
{
    uint32_t sig = 0;
    g_have_sig = video_edid_sig(&sig);
    g_sig = sig;
    g_poll_armed = false;
}

// The machine's family changed (the hand-over, or a core whose clock follows its System Mode): the Picture list is built again for it and the
// stored choice keeps its resolution.
static void set_machine_sys(CoreCtx *c, int sys)
{
    if (sys == g_msys) {
        return;
    }
    const char *old[1 + VID_HDMI_COUNT];
    int nold = 1 + g_nlist;
    memcpy(old, g_values, sizeof(old));
    g_msys = sys;
    fill_values();
    int pidx = picture_index(c->desc);
    if (pidx >= 0) {
        Setting &s = const_cast<Setting &>(c->desc->settings[pidx]);
        s.nvalues = 1 + g_nlist;
        if (c->settings) {
            c->settings->remap(pidx, old, nold);
        }
    }
}

// T164 D10c: the host's own Picture choice, read from the file the host writes it to (the same [section] text as the core's .cfg, cfgfile.h). The flash disk
// is readable here (only writes are refused, fsif.h). "From the screen" matches no resolution, so it stays -1, as do a missing file and a failed read.
#define HOST_CFG_PATH "/Flash/trx/host.cfg"
#define HOST_CFG_MAX  8192                         // the host's HOSTCFG_MAX: the whole file
static void host_mode_item(void *user, const char *store, const char *key, const char *value)
{
    if (!cfgfile_label_equal(store, "Video") || !cfgfile_label_equal(key, "Mode")) {
        return;
    }
    for (int h = 0; h < VID_HDMI_COUNT; h++) {
        if (cfgfile_label_equal(value, video_hdmi_name(h))) {
            *(int *)user = h;
        }
    }
}

static int read_host_choice(CoreCtx *c)
{
    static char text[HOST_CFG_MAX + 1];
    int hdmi = -1;
    if (!c->fs) {
        return -1;
    }
    int n = c->fs->load(HOST_CFG_PATH, text, HOST_CFG_MAX);
    if (n > 0) {
        cfgfile_parse(text, n, host_mode_item, &hdmi);
    }
    return hdmi;
}

int picture_safe(CoreCtx *c)
{
    if (!c->settings) {
        return 0;
    }
    int n = 0;
    const int idx[3] = { picture_index(c->desc), scale_index(c->desc), smooth_index(c->desc) };
    const int safe[3] = { 0 /* Auto */, VID_SCALE_INTEGER, 0 /* Off */ };
    for (int k = 0; k < 3; k++) {
        if (idx[k] >= 0) {
            c->settings->override_value(idx[k], safe[k]);
            n++;
        }
    }
    return n;
}

int picture_start(CoreCtx *c, int sys, int hdmi_now)
{
    if (sys < 0) {
        sys = VID_SYS_PAL50;
    }
    int pidx = picture_index(c->desc);
    if (g_vic_scale && g_gw) {                     // T222: the family the gateware runs (its row, from the System Mode), not the hand-over's
        int grow = video_gateware_row();
        if (grow >= 0) {
            sys = video_row_sys(grow);
        }
    }
    set_machine_sys(c, vic_family(sys));           // the list is the machine's family's: built again when the hand-over says another
    g_hd_hdmi = hdmi_now;
    g_host_hdmi = read_host_choice(c);
    int run_sys = sys;
    if (g_gw) {
        // The gateware says what it runs (a large core's own copy starts in 1080p50, a DFX core's follows the host): no guess from the hand-over.
        int row = video_gateware_row();
        if (row >= 0) {
            run_sys = video_row_sys(row);
            hdmi_now = video_row_hdmi(row);
            video_current = video_pack(run_sys, hdmi_now);
        } else if (hdmi_now >= 0) {
            video_current = video_pack(sys, hdmi_now);
        }
    } else if (hdmi_now >= 0) {
        video_current = video_pack(sys, hdmi_now);
    }
    c->video->set_gateware(g_gw);
    int idx = pidx;
    int value = (idx >= 0 && c->settings) ? c->settings->value(idx) : 0;
    g_last_value = value;
    Pick p = target(c, value, hdmi_now);
    int sidx = scale_index(c->desc);
    if (sidx >= 0 && c->settings) {
        scale_apply(c, c->settings->value(sidx));                    // before set_mode: the layout is made once (a VIC path: the gateware's register)
    }
    int midx = smooth_index(c->desc);
    if (midx >= 0 && c->settings) {
        smooth_apply(c, c->settings->value(midx));
    }
    if (p.hdmi >= 0 && (p.hdmi != hdmi_now || (g_gw && p.sys != run_sys))) {
        printf("[core] picture %s: %s instead of %s\n", value <= 0 ? "auto" : "chosen", video_hdmi_name(p.hdmi),
               hdmi_now < 0 ? "what the screen said" : video_hdmi_name(hdmi_now));
        if (apply_mode(p.sys, p.hdmi)) {
            video_current = video_pack(p.sys, p.hdmi);
        } else {
            printf("[core] picture: the mode did not come up\n");
            p.sys = run_sys;
            p.hdmi = hdmi_now;
        }
    }
    memset(&g_cf, 0, sizeof(g_cf));
    note_screen();
    auto_label(c, hdmi_now);
    return c->video->set_mode(p.hdmi, p.sys);
}

// ---- G12: follow the screen ---------------------------------------------------------------------------------------------------------
int picture_service(CoreCtx *c)
{
    int ev = 0;
    if (!c->video) {
        return 0;
    }
    uint32_t now = c->io->now_ms();
    g_cf_now = now;
    if (g_list_dirty) {
        g_list_dirty = false;
        ev |= PS_LIST_CHANGED;
    }
    if (g_cf.fresh) {
        g_cf.fresh = false;
        ev |= PS_CONFIRM_ASK;
    }
    if (g_cf.end) {
        g_cf.end = false;
        ev |= PS_CONFIRM_END;
    }
    if (g_cf.active && (uint32_t)(now - g_cf.t0) >= PICTURE_CONFIRM_MS) {
        g_cf.active = false;
        printf("[core] picture: nobody confirmed, back to %s\n", video_hdmi_name(g_cf.prev_hdmi));
        if (g_cf.prev_hdmi >= 0 && apply_mode(g_cf.prev_sys, g_cf.prev_hdmi)) {
            video_current = video_pack(g_cf.prev_sys, g_cf.prev_hdmi);
            c->video->set_mode(g_cf.prev_hdmi, g_cf.prev_sys);
        }
        int pidx = picture_index(c->desc);
        if (pidx >= 0 && c->settings) {
            c->settings->set(pidx, g_cf.prev_value);
        }
        g_last_value = g_cf.prev_value;
        char t[48];
        mode_text(g_cf.prev_sys, g_cf.prev_hdmi, t, sizeof(t));
        picmodes_back_text(t, g_result, sizeof(g_result));
        ev |= PS_CONFIRM_END | PS_LIST_CHANGED;
    }
    if (g_poll_armed && (int32_t)(now - g_next_poll) < 0) {
        return ev;
    }
    g_poll_armed = true;
    g_next_poll = now + PICTURE_POLL_MS;
    uint32_t sig = 0;
    if (!video_edid_sig(&sig)) {                   // nothing attached: the mode stays; the next screen is compared with the last one seen
        return ev;
    }
    if (g_have_sig && sig == g_sig) {
        return ev;                                 // the same screen: nothing is applied twice
    }
    uint8_t edid[256];
    int len = 0;
    if (!video_read_edid(edid, sizeof(edid), &len) || len <= 0) {
        return ev;                                 // attached but silent: asked again at the next poll
    }
    g_sig = sig;
    g_have_sig = true;
    uint16_t mask = video_modes_offered(edid, len);
    EdidInfo ei;
    g_native = edid_parse(edid, len, &ei) ? (uint16_t)(ei.native & mask) : 0;      // the new screen's native flags, not the last one's
    const char *old[1 + VID_HDMI_COUNT];
    int nold = 1 + g_nlist;
    memcpy(old, g_values, sizeof(old));
    g_mask = mask;
    fill_values();
    int pidx = picture_index(c->desc);
    if (pidx >= 0) {
        Setting &s = const_cast<Setting &>(c->desc->settings[pidx]);
        s.nvalues = 1 + g_nlist;
        if (c->settings) {
            c->settings->remap(pidx, old, nold);
        }
    }
    ev |= PS_LIST_CHANGED;
    int hdmi = current_hdmi(c), sys = current_sys();
    auto_label(c, hdmi);
    if (hdmi >= 0 && !(mask & (1u << video_mode_index(hdmi, sys)))) {
        Pick p = best_pick();
        printf("[core] screen changed: %s %s is not on its list, applying %s\n", video_sys_name(sys), video_hdmi_name(hdmi), video_hdmi_name(p.hdmi));
        if (g_cf.active) {                         // a question about a mode that is gone has no answer
            g_cf.active = false;
            ev |= PS_CONFIRM_END;
        }
        if (apply_mode(p.sys, p.hdmi)) {
            video_current = video_pack(p.sys, p.hdmi);
            c->video->set_mode(p.hdmi, p.sys);
        }
    }
    return ev;
}
