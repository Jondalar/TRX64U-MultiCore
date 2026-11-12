// The host application's picture, see hostvideo.h (T164 WP17).
#include "hostvideo.h"
#include "edid.h"
#include <stdio.h>
#include <string.h>

static const HostVideoIo *io;
static HvState st;
static uint8_t edid_buf[256];
static int edid_len;

static void ensure(void);
const HvState *hv_state(void) { return &st; }
bool hv_gateware(void) { ensure(); return st.gw; }

void hv_init(const HostVideoIo *i)
{
    io = i;
    memset(&st, 0, sizeof(st));
    st.hdmi = -1;
    st.def_hdmi = -1;
    st.def_sys = VID_SYS_PAL50;
    st.mask = EDID_LIST_NONE;
    st.gw = io && io->gw_present && io->gw_present();
    printf("[video] mode request register: %s\n", st.gw ? "answers (the host drives the mode itself)" : "not there (the old firmware path, as before)");
}

static void ensure(void)
{
    if (!io) {
        hv_init(hostvideo_board_io());
    }
}

static uint32_t digest(const uint8_t *b, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h ? h : 1;
}

// Looks at the screen: the EDID, the mask. Returns true when the EDID is not the one looked at last time (or none was).
static bool read_screen(void)
{
    int len = 0;
    bool ok = io->read_edid && io->read_edid(edid_buf, sizeof(edid_buf), &len) && len >= 128;
    edid_len = ok ? len : 0;
    uint32_t sig = ok ? digest(edid_buf, len) : 0xFFFFFFFFu;     // "nobody answered" is a state of its own
    st.have_edid = ok;
    st.mask = video_modes_offered(edid_buf, edid_len);
    EdidInfo info;
    st.native = ok && edid_parse(edid_buf, edid_len, &info) ? (uint16_t)(info.native & st.mask) : 0;     // T187 D5: the rows the screen flags as native
    bool changed = !st.sig_valid || sig != st.sig;
    st.sig = sig;
    st.sig_valid = true;
    return changed;
}

// What to put up: the stored default if the screen names it, else the best fitting row (Q9 for no EDID). *sys: the family used.
static int pick(bool use_saved, int *sys)
{
    if (use_saved && st.def_hdmi >= 0 && hv_offered(st.mask, st.def_sys, st.def_hdmi)) {
        *sys = hv_family(st.mask, st.def_sys);
        return st.def_hdmi;
    }
    int row = video_modes_default(edid_buf, edid_len, st.def_sys);
    if (row < 0) {
        row = video_row_of(VID_HDMI_480_576, st.def_sys);
    }
    *sys = video_row_sys(row);
    return video_row_hdmi(row);
}

// Puts the mode up. The gateware path writes the request and waits for its answer (0x4F04..09); false for a refusal or silence. The old path cannot fail.
static bool apply(int sys, int hdmi)
{
    if (!st.gw) {
        io->legacy_apply(sys, hdmi);
        video_current = video_pack(sys, hdmi);
        st.sys = sys;
        st.hdmi = hdmi;
        return true;
    }
    int row = video_mode_index(hdmi, sys);
    HvGwStatus a;
    if (!io->gw_status(&a)) {
        printf("[video] the mode request register did not answer\n");
        return false;
    }
    uint8_t w0 = a.written;
    io->gw_write(row);
    uint32_t t0 = io->ms();
    HvGwStatus b = a;
    for (;;) {
        if (io->gw_status(&b) && b.written != w0 && b.processed == b.written) {
            break;
        }
        if ((uint32_t)(io->ms() - t0) > HV_ACK_MS) {
            printf("[video] the request for row %d was not answered\n", row);
            return false;
        }
        io->pause_ms(1);
    }
    if (b.flags & HV_GW_REFUSED) {
        printf("[video] row %d refused by the gateware, the mode stays\n", row);
        return false;
    }
    t0 = io->ms();
    while (!((b.flags & HV_GW_RUNNING) && (b.flags & HV_GW_PIXLOCK) && (b.flags & HV_GW_GTREADY) && b.mode == row)) {
        if ((uint32_t)(io->ms() - t0) > HV_SETTLE_MS) {
            printf("[video] row %d accepted, the link is not up after %d ms (status %02x mode %d)\n", row, HV_SETTLE_MS, b.flags, b.mode);
            break;
        }
        io->pause_ms(5);
        io->gw_status(&b);
    }
    io->enable();
    video_current = video_pack(sys, hdmi);
    st.sys = sys;
    st.hdmi = hdmi;
    return true;
}

bool hv_load_default(int boot_sys, int boot_hdmi)
{
    ensure();
    int h = -1, s = VID_SYS_PAL50;
    hv_saved_load(&h, &s);
    bool moved = false;
    if (h < 0 && boot_hdmi >= 0 && boot_hdmi < VID_HDMI_COUNT) {
        h = boot_hdmi;
        s = (boot_sys == VID_SYS_NTSC60) ? VID_SYS_NTSC60 : VID_SYS_PAL50;
        hv_saved_store(h, s);
        moved = true;
        printf("[video] boot.cfg's picture %d,%d moved into host.cfg\n", s, h);
    }
    st.def_hdmi = h;
    st.def_sys = s;
    st.migrated = moved;
    st.loaded = true;
    return moved;
}

void hv_start(bool use_saved, int *used_sys, int *used_hdmi)
{
    ensure();
    if (use_saved && !st.loaded) {
        hv_load_default(-1, -1);
    }
    read_screen();
    int sys;
    int hdmi = pick(use_saved, &sys);
    printf("[video] %s, %s (%s, %s)\n", video_sys_name(sys), video_hdmi_name(hdmi),
           use_saved && st.def_hdmi == hdmi ? "chosen" : "from the screen", st.have_edid ? "EDID read" : "no EDID");
    // The host asks twice (before the flash disk is mounted, then with the stored choice): the same answer twice is not applied twice.
    if (video_current != video_pack(sys, hdmi)) {
        apply(sys, hdmi);
    } else {
        st.sys = sys;
        st.hdmi = hdmi;
        printf("[video] this mode is up already, not applied again\n");
    }
    if (used_sys) {
        *used_sys = st.sys;
    }
    if (used_hdmi) {
        *used_hdmi = st.hdmi;
    }
}

bool hv_user_set(int hdmi, char *why, int why_n)
{
    ensure();
    int sys;
    int def = hdmi;
    if (hdmi < 0) {
        hdmi = pick(false, &sys);
    } else if (hv_offered(st.mask, st.def_sys, hdmi)) {
        sys = hv_family(st.mask, st.def_sys);
    } else {
        if (why) {
            snprintf(why, why_n, "the screen does not list %s", video_hdmi_name(hdmi));
        }
        return false;
    }
    int ps = st.sys, ph = st.hdmi;
    if (st.pending) {
        ps = st.prev_sys;
        ph = st.prev_hdmi;
    }
    if (!apply(sys, hdmi)) {
        if (why) {
            snprintf(why, why_n, "the host refused %s", video_hdmi_name(hdmi));
        }
        return false;
    }
    if (st.gw) {
        st.pending = true;
        st.prev_sys = ps;
        st.prev_hdmi = ph;
        st.pend_def = def;
        st.deadline = io->ms() + HV_CONFIRM_MS;
    } else {
        st.def_hdmi = def;
        hv_saved_store(st.def_hdmi, st.def_sys);
    }
    return true;
}

bool hv_keep(void)
{
    if (!st.pending) {
        return false;
    }
    st.pending = false;
    st.def_hdmi = st.pend_def;
    hv_saved_store(st.def_hdmi, st.def_sys);
    return true;
}

int hv_auto(void)
{
    ensure();
    int sys;
    return pick(false, &sys);
}

bool hv_revert(void)
{
    ensure();
    if (!st.pending) {
        return false;
    }
    printf("[video] reverted by the key: back to %s\n", video_hdmi_name(st.prev_hdmi));
    st.pending = false;
    if (!apply(st.prev_sys, st.prev_hdmi)) {
        st.hdmi = -1;
    }
    return true;
}

int hv_seconds_left(void)
{
    if (!st.pending) {
        return 0;
    }
    int32_t left = (int32_t)(st.deadline - io->ms());
    return left <= 0 ? 0 : (left + 999) / 1000;
}

HvEvent hv_tick(void)
{
    ensure();
    uint32_t now = io->ms();
    if (st.pending) {
        if ((int32_t)(now - st.deadline) >= 0) {
            printf("[video] no key within %d s: back to %s\n", HV_CONFIRM_MS / 1000, video_hdmi_name(st.prev_hdmi));
            st.pending = false;
            if (!apply(st.prev_sys, st.prev_hdmi)) {
                // the old row would be refused only if the screen changed meanwhile; the poll below picks again
                st.hdmi = -1;
            }
            return HV_REVERTED;
        }
        return HV_CONFIRM_TICK;
    }
    if ((int32_t)(now - st.next_poll) < 0) {
        return HV_NONE;
    }
    st.next_poll = now + HV_POLL_MS;
    if (!io->hpd || !io->hpd()) {
        return HV_NONE;                     // unplugged: nothing to read, nothing to change; the next plug-in is looked at
    }
    if (!read_screen()) {
        return HV_NONE;                     // the same EDID again applies nothing
    }
    if (st.hdmi >= 0 && (st.mask & (1u << video_row_of(st.hdmi, st.sys)))) {
        return HV_NONE;                     // the running mode is in the new list
    }
    int sys;
    int hdmi = pick(true, &sys);
    printf("[video] the screen changed: %s, %s\n", video_sys_name(sys), video_hdmi_name(hdmi));
    return apply(sys, hdmi) ? HV_FOLLOWED : HV_NONE;       // a refusal leaves the mode as it is; the same EDID is not tried again
}
