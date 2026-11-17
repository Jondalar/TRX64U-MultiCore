#include "desc_check.h"
#include "pad.h"
#include "bindcheck.h"
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdio.h>
#include "settings.h"
#include "ops_glue.h"
#include "libfmt.h"
#include "picture.h"
#include "engines.h"
#include "mediamounts.h"

static bool text(const char *s) { return s && s[0]; }

// A refusal that names a thing: kept in one buffer, valid until the next check (the checker runs at start-up and in the benches).
static char why_[240];
static const char *fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why_, sizeof(why_), fmt, ap);
    va_end(ap);
    return why_;
}

static const char *check_call(const OpRegistry &reg, const char *what, const char *label, const char *op, const OpBind *b, int nb, bool file_ok,
                              bool ask_ok = false, bool text_ask_ok = false)
{
    return bind_check(reg, what, label, op, b, nb, file_ok, ask_ok, text_ask_ok);
}

#define REG_LIMIT 0x1000                 // the register block of the core window: page 0

// The bits setting i is, against the other settings that name the same register: the mask moved by the shift stays in the byte, every value
// fits in the mask (a table entry too), and no two settings own the same bit.
static const char *check_bind(const CoreDesc *d, int i)
{
    const Setting &s = d->settings[i];
    const RegBind &b = *s.bind;
    if (b.reg >= REG_LIMIT) return fail("settings: \"%s\" binds register 0x%x, outside the register block", s.key, (unsigned)b.reg);
    if (!b.mask || b.shift > 7 || ((unsigned)b.mask << b.shift) > 0xFF) {
        return fail("settings: \"%s\" binds bits that are not in one byte (mask 0x%x, shift %d)", s.key, (unsigned)b.mask, (int)b.shift);
    }
    for (int v = 0; v < s.nvalues; v++) {
        unsigned x = b.bytes ? b.bytes[v] : (unsigned)v;
        if ((x & b.mask) != x) {
            return fail("settings: value %d of \"%s\" does not fit its bits (0x%x, mask 0x%x)", v, s.key, x, (unsigned)b.mask);
        }
    }
    for (int j = 0; j < i; j++) {
        const Setting &o = d->settings[j];
        if (o.bind && o.bind->reg == b.reg && (((unsigned)o.bind->mask << o.bind->shift) & ((unsigned)b.mask << b.shift))) {
            return fail("settings: \"%s\" and \"%s\" own the same bits of register 0x%x", o.key, s.key, (unsigned)b.reg);
        }
    }
    return 0;
}

struct Range { uint32_t lo, hi; const char *what; };

// The DDR2 window ranges the descriptor's records claim must not overlap each other or the picture's buffers (T104 5.2: the Amiga's window is
// carved up by five records, and one wrong offset would corrupt a disk or the Kickstart silently).
static const char *check_ranges(const CoreDesc *d)
{
    Range r[8];
    int n = 0;
    if (d->romset) {
        uint32_t big = 0;
        for (int i = 0; i < ROMSET_SIZES && d->romset->sizes[i]; i++) {
            big = d->romset->sizes[i] > big ? d->romset->sizes[i] : big;
        }
        r[n++] = { d->romset->window_off, d->romset->window_off + big + (d->romset->mirror_size ? d->romset->mirror_stride : 0), "the ROM set" };
        if (d->romset->clear_below) r[n++] = { 0, d->romset->clear_below, "the RAM the ROM set clears" };
    }
    if (d->window_media) r[n++] = { d->window_media->base, d->window_media->base + d->window_media->nslots * d->window_media->stride, "the window slots" };
    if (d->sector_media) r[n++] = { d->sector_media->stage_off, d->sector_media->stage_off + 512, "the sector staging" };
    if (d->video.w && d->video.fb_base >= 0x01000000u) {
        r[n++] = { d->video.fb_base - 0x01000000u, d->video.fb_base - 0x01000000u + (uint32_t)d->video.nbuf * d->video.fb_stride, "the frame buffers" };
    }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < i; j++) {
            if (r[i].lo < r[j].hi && r[j].lo < r[i].hi) {
                return fail("DDR2 window: %s [0x%x..0x%x) overlaps %s [0x%x..0x%x)", r[i].what, (unsigned)r[i].lo, (unsigned)r[i].hi, r[j].what,
                            (unsigned)r[j].lo, (unsigned)r[j].hi);
            }
        }
    }
    return 0;
}

// The data records of T104 5.2, before anything that refers to them (the settings' choice, the poll).
static const char *check_engines(const CoreDesc *d, const OpRegistry &reg)
{
    for (int i = 0; i < d->nreset_writes; i++) {
        if (!d->reset_writes || d->reset_writes[i].reg >= REG_LIMIT) return "reset_writes: a register outside the register block";
    }
    if (d->romset) {
        const RomSetSpec &s = *d->romset;
        if (!text(s.role) || !text(s.ext) || s.ext[0] != '.' || !s.sizes[0] || s.max < 1 || s.max > ROMSET_MAX || !text(s.choice_key) || !s.labels ||
            !text(s.auto_label) || !text(s.size_text) || !text(s.example)) {
            return "romset: role, extension with a dot, a size, 1..8 members, the choice, its labels and the texts are needed";
        }
        if (!reg.find_op("romset.put")) return "romset: the base's operation romset.put is not registered";
        if (s.mirror_size && !s.mirror_stride) return "romset: a mirror needs its stride";
        int found = 0;
        for (int i = 0; i < d->nsettings; i++) {
            found += !strcasecmp(d->settings[i].key, s.choice_key);
        }
        if (found != 1) return fail("romset: the choice \"%s\" is not one setting of the descriptor", s.choice_key);
    }
    if (d->window_media) {
        const WindowMediaSpec &w = *d->window_media;
        if (!text(w.prefix) || w.nslots < 1 || w.nslots > WIN_SLOTS_MAX || w.ntracks < 1 || w.ntracks > WIN_TRACKS_MAX || !w.track_bytes ||
            w.track_bytes > 8192 || !w.max_image) {
            return "window_media: a prefix, 1..4 slots, 1..160 tracks of at most 8192 bytes and a largest image are needed";
        }
        if (w.stride < w.max_image) return "window_media: the stride is smaller than the largest image";
        if (w.base + w.nslots * w.stride > 0x01000000u) return "window_media: the slots do not fit in the DDR2 window";
        if ((uint32_t)w.ntracks * w.track_bytes < w.max_image) return "window_media: the tracks do not cover the largest image";
        if (w.mount_reg + w.mount_step * (w.nslots - 1) >= REG_LIMIT || w.dlog_status >= REG_LIMIT || w.dlog_track >= REG_LIMIT ||
            w.dlog_drive >= REG_LIMIT || w.dlog_pop >= REG_LIMIT) {
            return "window_media: a register outside the register block";
        }
        if (!w.idle_flush_ms || !w.max_dirty_ms) return "window_media: the flush policy needs both times";
    }
    if (d->sector_media) {
        const SectorMediaSpec &s = *d->sector_media;
        if (!text(s.prefix) || s.nunits < 1 || s.nunits > SEC_UNITS_MAX || !s.feat_sectors || !s.idle_sync_ms) {
            return "sector_media: a prefix, 1..2 units, the feature bit and the sync time are needed";
        }
        if (s.stage_off + SEC_BYTES > 0x01000000u) return "sector_media: the staging sector is outside the DDR2 window";
        if (s.mount_reg + s.mount_step * (s.nunits - 1) + 8 >= REG_LIMIT || s.feat_reg >= REG_LIMIT || s.mbox_status >= REG_LIMIT ||
            s.mbox_lba + 4 >= REG_LIMIT || s.mbox_done >= REG_LIMIT) {
            return "sector_media: a register outside the register block";
        }
    }
    if (d->mount_slots) {                                           // T180 D2, D5: the slots are keys of the .cfg ("Mount <name>=<spec>")
        const MountSlots &m = *d->mount_slots;
        if (m.n < 1 || m.n > MOUNT_SLOTS_MAX || !m.names || !m.spec || !m.mount || !m.eject) {
            return "mount_slots: 1..8 slots with names and the three operations (spec, mount, eject) are needed";
        }
        if ((d->window_media ? d->window_media->nslots : 0) + (d->sector_media ? d->sector_media->nunits : 0) + m.n > Settings::MOUNT_MAX) {
            return "mount_slots: more media than the .cfg keeps";
        }
        for (int i = 0; i < m.n; i++) {
            const char *nm = m.names[i];
            int l = nm ? (int)strlen(nm) : 0;
            if (l < 1 || l > 9 || nm[0] == ' ' || nm[l - 1] == ' ') return "mount_slots: a name of 1..9 characters is needed";
            for (int k = 0; k < l; k++) {
                if ((unsigned char)nm[k] < 0x20 || (unsigned char)nm[k] > 0x7E || nm[k] == '=' || nm[k] == '[' || nm[k] == ']') return "mount_slots: a name has a character a key cannot hold";
            }
            for (int k = 0; k < i; k++) {
                if (!strcasecmp(m.names[k], nm)) return "mount_slots: two slots with one name";
            }
            if ((d->window_media && !strncasecmp(nm, d->window_media->prefix, strlen(d->window_media->prefix))) ||
                (d->sector_media && !strncasecmp(nm, d->sector_media->prefix, strlen(d->sector_media->prefix)))) {
                return "mount_slots: a name begins like a slot of the base";
            }
        }
    }
    if (d->mouse) {
        const MouseSinkSpec &m = *d->mouse;
        if (m.kind != MOUSE_COUNTERS && m.kind != MOUSE_POT1351) return "mouse: an unknown backend";
        if (m.kind == MOUSE_POT1351) return check_ranges(d);         // the 1351 uses no register of the core
        if (m.clamp < 1 || m.step < 1 || m.step > 127 || m.interval_ms < 1) return "mouse: a clamp, a step of 1..127 and an interval are needed";
        if (m.reg_dx >= REG_LIMIT || m.reg_dy >= REG_LIMIT || m.reg_btn >= REG_LIMIT) return "mouse: a register outside the register block";
    }
    if (d->pad) {
        const PadSpec &p = *d->pad;
        if (p.nbuttons < 1 || p.nbuttons > PAD_BUTTONS_MAX || !p.names || !p.def_map) return "pad: 1..16 buttons with names and a default map are needed";
        for (int i = 0; i < (p.by_id ? 1 : PAD_PLAYERS); i++) {
            if (!p.reg[i] || p.reg[i] + 1u >= REG_LIMIT) return "pad: a register outside the register block";
        }
        if (!p.by_id && (p.reg[0] == p.reg[1] || p.reg[0] + 1u == p.reg[1] || p.reg[1] + 1u == p.reg[0])) return "pad: the two players' registers overlap";
        for (int i = 0; i < p.nbuttons; i++) {
            if (!p.names[i] || !p.names[i][0]) return "pad: a button without a name";
            if (p.def_map[i] > 35 && p.def_map[i] != PAD_SRC_NONE && (p.def_map[i] < PAD_ROLE_RIGHT || p.def_map[i] > PAD_ROLE_START)) return "pad: a default map entry is no source";
        }
    }
    if (d->fbwin) {
        const FbWinSpec &f = *d->fbwin;
        const uint16_t regs[] = { f.reg_feat, f.reg_ctrl, (uint16_t)(f.reg_diw_h + 3), (uint16_t)(f.reg_diw_v + 3), (uint16_t)(f.reg_skip_x + 1), f.reg_skip_y,
                                  f.reg_cols, (uint16_t)(f.reg_lines + 1) };
        for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
            if (!regs[i] || regs[i] >= REG_LIMIT) return "fbwin: a register outside the register block";
        }
        if (!f.feat_bit || !f.ctrl_video_off || f.x_per_unit < 1 || f.src_w < 8 || f.src_h < 1 || !f.interval_ms || !f.frames_ms || !f.def_cols || !f.def_lines) {
            return "fbwin: the feature bit, VIDEO_OFF, the scale, the source size, the default window and the interval are needed";
        }
        if (d->video.w < 8 || d->video.h < 2 || !d->video.nbuf || d->video.fb_base < 0x01000000u) return "fbwin: the core has no picture window to move";
    }
    return check_ranges(d);
}

const char *desc_check(const CoreDesc *d)
{
    OpRegistry reg;
    ops_register_base(reg);
    if (reg.error()[0]) {
        return fail("base: %s", reg.error());
    }
    if (d->register_ops) {
        d->register_ops(reg);
        if (reg.error()[0]) {
            return fail("%s: %s", d->cor_name ? d->cor_name : "?", reg.error());
        }
    }
    return desc_check_with(d, reg);
}

const char *desc_check_with(const CoreDesc *d, const OpRegistry &reg)
{
    for (int i = 0; i < 4; i++) {
        if (d->id[i] < 33 || d->id[i] > 126) {
            return "id: four printable characters";
        }
    }
    if (!text(d->cor_name)) return "cor_name: the manifest name is missing";
    if (!text(d->title)) return "title is missing";
    if (!text(d->folder)) return "folder (roms/<folder>) is missing";
    if (!text(d->store)) return "store (the [section] of the .cfg) is missing";
    if (d->min_contract < 1) return "min_contract must be 1 or more";
    if (!d->init || !d->release) return "init and release: the control hooks are missing";
    if (!d->reset_ms) return "reset_ms: how long a reset is held is missing";
    if (d->video.w == 0) {
        // T104.2 D5: the core draws its own picture; nothing of the video service applies, and a half-filled descriptor is a mistake
        // (the service would write FB_BASE to register offsets that are the ID bytes)
        if (d->video.h || d->video.fb_base || d->video.fb_stride || d->video.nbuf) {
            return "video: w == 0 means the core draws its own picture - h, fb_base, fb_stride and nbuf have to be 0 too";
        }
    } else {
        if (d->video.scale < 1 || d->video.scale > 3) return "video.scale must be 1, 2 or 3";
        if (!d->video.h || d->video.nbuf < 2) return "video: size and at least two buffers";
        if (d->video.x0 < 0 || d->video.y0 < 0 || d->video.x0 + (int)(d->video.w * d->video.scale) > 1920 ||
            d->video.y0 + (int)(d->video.h * d->video.scale) > 1080) {
            return "video: the layer does not fit on 1920 x 1080";
        }
        if (d->video.bpp != 0 && d->video.bpp != 2 && d->video.bpp != 4) return "video.bpp is 2 or 4";
        if (d->video.fb_stride < d->video.w * d->video.h * d->video.pixel_bytes()) return "video.fb_stride is smaller than a frame";
        if (d->video.fb_base < 0x01000000u) return "video.fb_base is below the DDR2 window";
        if (!d->video.reg_latest || !d->video.reg_seq) return "video: FB_LATEST and FRAME_SEQ registers are needed";
        // a core with its buffers fixed in the gateware has no FB_BASE / FB_STRIDE register: both 0 (video.cc writes nothing then)
        if (!d->video.reg_fb_base != !d->video.reg_fb_stride) return "video: reg_fb_base and reg_fb_stride are both given or both 0";
        if (d->video.nbuf > 4) return "video: at most four buffers (FB_LATEST names one in two bits)";
    }
    if ((!d->freeze.mask && !d->menu_first) || !d->freeze.timeout_ms) return "freeze: the PAUSED bit and its timeout are missing";       // menu_first (T175): a core with no machine has nothing to stop
    if (!d->tasks && d->ntasks) return "tasks: count without a table";
    {
        const char *why = check_engines(d, reg);
        if (why) return why;
    }
    if (d->nroms < 0 || d->nroms > ROMS_MAX || (d->nroms && !d->roms)) return fail("roms: %d listed, the scan's result arrays take ROMS_MAX (%d)", d->nroms, ROMS_MAX);
    for (int i = 0; i < d->nroms; i++) {
        const RomSpec &r = d->roms[i];
        if (!text(r.role) || !text(r.names[0]) || !r.sizes[0] || !text(r.put_op)) return "roms: role, a name, a size and put are needed";
        const OpDef *o = reg.find_op(r.put_op);
        if (!o) return fail("roms: \"%s\" is put by operation \"%s\", which no contributor registered", r.role, r.put_op);
        if (o->nargs != 1 || o->args[0].type != AT_BLOB) return fail("roms: operation \"%s\" has to take exactly one blob, the ROM's bytes", r.put_op);
    }
    if (d->nroms && !d->folder) return "roms need a folder";
    if (d->nreport_regs < 0 || d->nreport_regs > REPORT_REGS_MAX || (d->nreport_regs && !d->report_regs)) return "report_regs: a count without a table, or more than REPORT_REGS_MAX";
    for (int i = 0; i < d->nreport_regs; i++) {      // T162 D3
        const ReportReg &r = d->report_regs[i];
        if (!text(r.name)) return "report_regs: every register needs a name";
        if (r.bytes != 1 && r.bytes != 2 && r.bytes != 4) return fail("report_regs: \"%s\" is %u bytes wide (1, 2 or 4)", r.name, (unsigned)r.bytes);
        if ((uint32_t)r.reg + r.bytes > 0x1000) return fail("report_regs: \"%s\" lies outside the register page", r.name);
    }
    if (d->nsettings > SETTINGS_MAX - 1) return "settings: too many - the application adds Picture and the libraries add theirs (SETTINGS_MAX)";
    for (int i = 0; i < d->nsettings; i++) {
        const Setting &s = d->settings[i];
        if (!text(s.key) || !s.values || s.nvalues < (s.kind ? 1 : 2)) return "settings: a key and at least two values";
        if (s.def < 0 || s.def >= s.nvalues) return "settings: the default is not one of the values";
        bool choice = d->romset && text(d->romset->choice_key) && !strcasecmp(s.key, d->romset->choice_key);
        if ((text(s.apply) ? 1 : 0) + (s.bind ? 1 : 0) + (choice ? 1 : 0) != 1) {
            return fail("settings: \"%s\" needs exactly one of apply, bind (register bits) or being the rom_set's choice", s.key);
        }
        if (s.bind) {
            const char *why = check_bind(d, i);
            if (why) return why;
        }
        if (choice) {
            if (s.nvalues != d->romset->max + 1 || s.values != d->romset->labels->item) {
                return fail("romset: the choice \"%s\" has to have max + 1 values and be the labels of the set", s.key);
            }
        }
        const OpDef *o = text(s.apply) ? reg.find_op(s.apply) : 0;
        if (text(s.apply) && !o) return fail("settings: \"%s\" is applied by operation \"%s\", which no contributor registered", s.key, s.apply);
        if (o && s.kind == 3) {
            if (o->nargs != 1 || strcmp(o->args[0].name, "path") || o->args[0].type != AT_STRING) {
                return fail("settings: operation \"%s\" has to take exactly one text named \"path\" (the file selected for \"%s\")", s.apply, s.key);
            }
            o = 0;
        }
        if (o && (o->nargs != 1 || strcmp(o->args[0].name, "value") || (o->args[0].type != AT_INT && o->args[0].type != AT_ENUM))) {
            return fail("settings: operation \"%s\" has to take exactly one number or enum named \"value\"", s.apply);
        }
        if (o && (o->args[0].lo > s.base || o->args[0].hi < s.base + s.nvalues - 1)) {
            return fail("settings: \"%s\" has the values %d .. %d, operation \"%s\" takes %d .. %d", s.key, (int)s.base, (int)(s.base + s.nvalues - 1), s.apply, (int)o->args[0].lo, (int)o->args[0].hi);
        }
        for (int j = 0; j < s.nvalues; j++) {
            if (!text(s.values[j]) && !(s.kind == 3 && j == 0)) return "settings: an empty value label";
        }
        for (int j = 0; j < i; j++) {
            if (!strcasecmp(d->settings[j].key, s.key)) return "settings: two items with the same key";
        }
        if (!strcasecmp(s.key, PICTURE_KEY) || !strcasecmp(s.key, SCALE_KEY) || !strcasecmp(s.key, SMOOTH_KEY) || !strcasecmp(s.key, FOLLOW_KEY)) return "settings: the keys Picture, Scale, Smoothing and Frame follow belong to the application";
        if (s.apply && (!strcmp(s.apply, PICTURE_OP) || !strcmp(s.apply, SCALE_OP) || !strcmp(s.apply, SMOOTH_OP) || !strcmp(s.apply, FOLLOW_OP))) return "settings: the operations picture.apply, scale.apply, smooth.apply and follow.apply belong to the application's own Picture, Scale, Smoothing and Frame follow rows";
    }
    for (int i = 0; i < d->ntasks; i++) {
        if (!text(d->tasks[i].label) || !text(d->tasks[i].op)) return "tasks: label and op";
        const Task &tk = d->tasks[i];
        const char *why = check_call(reg, "tasks", tk.label, tk.op, tk.bind, tk.nbind, false, false, text(tk.ask));
        if (why) return why;
        if (text(tk.ask)) {
            int asked = 0;
            for (int k = 0; k < tk.nbind; k++) {
                asked += tk.bind[k].kind == BIND_TEXT;
            }
            if (asked != 1) return fail("tasks: \"%s\" asks for a name but binds it %d times (once)", tk.label, asked);
            if (tk.ask_state && !reg.find_state(tk.ask_state)) return fail("tasks: \"%s\" takes its default from state \"%s\", which no contributor registered", tk.label, tk.ask_state);
        } else if (tk.ask_state) {
            return fail("tasks: \"%s\" has a default for a name it does not ask", tk.label);
        }
        if (tk.pick_dir && !text(tk.ask)) {
            return fail("tasks: \"%s\" picks a folder for a name it does not ask", tk.label);
        }
    }
    for (int i = 0; i < d->nfiletypes; i++) {
        const FileType &f = d->filetypes[i];
        if (!text(f.ext) || f.ext[0] != '.' || !f.actions || f.nactions < 1) return "filetypes: an extension with a dot and an action";
        for (int j = 0; j < f.nactions; j++) {
            if (!text(f.actions[j].label) || !text(f.actions[j].op)) return "filetypes: action label and op";
            const FileAction &fa = f.actions[j];
            const char *why = check_call(reg, "filetypes", fa.label, fa.op, fa.bind, fa.nbind, true, text(fa.ask));
            if (why) return why;
            if (text(fa.ask)) {
                int asked = 0;
                for (int k = 0; k < fa.nbind; k++) {
                    asked += fa.bind[k].kind == BIND_ASK;
                }
                if (asked != 1) return fail("filetypes: \"%s\" asks for an address but binds it %d times (once)", fa.label, asked);
            }
        }
    }
    for (int i = 0; i < d->nfiletypes; i++) {
        const FileType &f = d->filetypes[i];
        if (!f.ninner) continue;
        if (!text(f.inner_ext) || f.inner_ext[0] != '.' || !f.inner || f.ninner < 0) return fail("filetypes: \"%s\" has actions for files inside but no extension for them", f.ext);
        for (int j = 0; j < f.ninner; j++) {
            const FileAction &fa = f.inner[j];
            if (!text(fa.label) || !text(fa.op)) return "filetypes: inner action label and op";
            const char *why = check_call(reg, "filetypes", fa.label, fa.op, fa.bind, fa.nbind, true);
            if (why) return why;
        }
    }
    for (int i = 0; i < d->nmedia; i++) {
        const MediaSlot &m = d->media[i];
        if (!text(m.name) || !text(m.state)) return "media: name and state";
        if (!reg.find_state(m.state)) return fail("media: \"%s\" reads state \"%s\", which no contributor registered", m.name, m.state);
        if (m.eject_op) {
            const char *why = check_call(reg, "media", m.name, m.eject_op, m.eject_bind, m.neject_bind, false);
            if (why) return why;
        }
    }
    if (d->type_op) {
        const OpDef *o = reg.find_op(d->type_op);
        if (!o) return fail("type_op: operation \"%s\" is not registered by any contributor", d->type_op);
        if (o->nargs != 1 || o->args[0].type != AT_STRING || strcmp(o->args[0].name, "text")) {
            return fail("type_op: operation \"%s\" takes one text argument \"text\"", d->type_op);
        }
    }
    if (d->run_op) {
        const OpDef *o = reg.find_op(d->run_op);
        if (!o) return fail("run_op: operation \"%s\" is not registered by any contributor", d->run_op);
        if (o->nargs != 1 || o->args[0].type != AT_PATH || strcmp(o->args[0].name, "path")) {
            return fail("run_op: operation \"%s\" takes one path argument \"path\"", d->run_op);
        }
    }
    if (d->service_op) {
        if (!d->service_period_ms) return "service needs a period";
        const OpDef *o = reg.find_op(d->service_op);
        if (!o) return fail("service: operation \"%s\" is not registered by any contributor", d->service_op);
        if (o->nargs) return fail("service: operation \"%s\" is called without arguments", d->service_op);
    }
    if (d->needs_program && (!text(d->needs_program) || !text(d->program_ext))) return "needs_program goes with program_ext";
    if (d->needs_program && !d->nfiletypes) return "a core that needs a program needs a file type to load it";
    if (d->ncaps < 0 || (d->ncaps && !d->caps)) return "capabilities: a count without a table";
    for (int i = 0; i < d->ncaps; i++) {
        const char *c = d->caps[i];
        int n = 0;
        while (c && n < U2A3_NAME_MAX && c[n] && ((c[n] >= 'a' && c[n] <= 'z') || (c[n] >= '0' && c[n] <= '9') || c[n] == '_')) {
            n++;
        }
        if (!c || n == 0 || n >= U2A3_NAME_MAX || c[n]) {
            return fail("capabilities: \"%s\" is not a name of 1..%d characters of a-z, 0-9 and _", c ? c : "(null)", U2A3_NAME_MAX - 1);
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(d->caps[j], c)) return fail("capabilities: \"%s\" is listed twice", c);
        }
    }
    return 0;
}

const char *desc_check_table(const CoreDesc *const *table)
{
    for (int i = 0; table[i]; i++) {
        for (int j = 0; j < i; j++) {
            // the Amiga and the A1200 are one gateware with one ID word: both say so, and the bundle's name tells them apart (start.cc)
            if (!memcmp(table[i]->id, table[j]->id, 4) && !(table[i]->id_shared && table[j]->id_shared)) return "two cores share an ID block";
            if (!strcasecmp(table[i]->cor_name, table[j]->cor_name)) return "two cores share a manifest name";
        }
    }
    return 0;
}
