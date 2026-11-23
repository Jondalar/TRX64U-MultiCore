// The tables of a descriptor that sections 7 .. 16 of the description record carry (reset writes, ID feature, report registers, alternative video, raw keys, mouse, picture window,
// ROM set, window media, sector media) and its pad, compared field by field (T239.3 D17): shared by test_descgen (every core with a TOML) and test_descsec (the Amiga 1200's).
#ifndef TEST_DESC_TABLES_CMP_H
#define TEST_DESC_TABLES_CMP_H
#include "check.h"
#include "desc.h"
#include "romset.h"
#include "winslot.h"
#include "sectors.h"
#include "mousesink.h"
#include "fbwin.h"
#include "rawkeys.h"
#include "pad.h"

static int g_fields;                                       // what has been compared

#define EQN(what, a, b) do { g_fields++; CHECK_EQ((long long)(a), (long long)(b)); (void)(what); } while (0)
#define EQS(what, a, b) do { g_fields++; CHECK_STR((a), (b)); (void)(what); } while (0)
#define NUL(what, a) do { g_fields++; CHECK((a) == 0); (void)(what); } while (0)


static void compare_tables(const CoreDesc &want, const CoreDesc &got)
{
    // the tables of the sections 7 .. 16
    EQN("nreset_writes", want.nreset_writes, got.nreset_writes);
    for (int i = 0; i < want.nreset_writes && i < got.nreset_writes; i++) {
        EQN("reset reg", want.reset_writes[i].reg, got.reset_writes[i].reg);
        EQN("reset value", want.reset_writes[i].value, got.reset_writes[i].value);
    }
    EQN("id_feat_reg", want.id_feat_reg, got.id_feat_reg);
    EQN("id_feat_mask", want.id_feat_mask, got.id_feat_mask);
    EQN("id_feat_val", want.id_feat_val, got.id_feat_val);
    EQN("nreport_regs", want.nreport_regs, got.nreport_regs);
    for (int i = 0; i < want.nreport_regs && i < got.nreport_regs; i++) {
        EQS("report name", want.report_regs[i].name, got.report_regs[i].name);
        EQN("report reg", want.report_regs[i].reg, got.report_regs[i].reg);
        EQN("report bytes", want.report_regs[i].bytes, got.report_regs[i].bytes);
    }
    EQN("has alt_video", want.alt_video != 0, got.alt_video != 0);
    if (want.alt_video && got.alt_video) {
        const VideoDesc &a = *want.alt_video, &b = *got.alt_video;
        EQN("alt w", a.w, b.w); EQN("alt h", a.h, b.h); EQN("alt scale", a.scale, b.scale); EQN("alt x0", a.x0, b.x0); EQN("alt y0", a.y0, b.y0);
        EQN("alt fb_base", a.fb_base, b.fb_base); EQN("alt fb_stride", a.fb_stride, b.fb_stride); EQN("alt nbuf", a.nbuf, b.nbuf);
        EQN("alt reg_latest", a.reg_latest, b.reg_latest); EQN("alt reg_seq", a.reg_seq, b.reg_seq);
        EQN("alt reg_fb_base", a.reg_fb_base, b.reg_fb_base); EQN("alt reg_fb_stride", a.reg_fb_stride, b.reg_fb_stride);
        EQN("alt bpp", a.bpp, b.bpp); EQN("alt pxfmt", a.pxfmt, b.pxfmt); EQN("alt h60", a.h60, b.h60);
    }
    EQN("has rawkeys", want.rawkeys != 0, got.rawkeys != 0);
    if (want.rawkeys && got.rawkeys) {
        const RawKeySpec &a = *want.rawkeys, &b = *got.rawkeys;
        EQN("rawkeys reg", a.reg_key, b.reg_key); EQN("rawkeys nhid", a.nhid, b.nhid);
        EQN("rawkeys caps_usage", a.caps_usage, b.caps_usage); EQN("rawkeys caps_code", a.caps_code, b.caps_code);
        EQN("rawkeys has hid", a.hid != 0, b.hid != 0); EQN("rawkeys has mod", a.mod != 0, b.mod != 0);
        for (int i = 0; a.hid && b.hid && i < a.nhid; i++) {
            EQN("rawkeys hid", a.hid[i], b.hid[i]);
        }
        for (int i = 0; a.mod && b.mod && i < 8; i++) {
            EQN("rawkeys mod", a.mod[i], b.mod[i]);
        }
    }
    EQN("has mouse", want.mouse != 0, got.mouse != 0);
    if (want.mouse && got.mouse) {
        const MouseSinkSpec &a = *want.mouse, &b = *got.mouse;
        EQN("mouse kind", a.kind, b.kind); EQN("mouse dx", a.reg_dx, b.reg_dx); EQN("mouse dy", a.reg_dy, b.reg_dy); EQN("mouse btn", a.reg_btn, b.reg_btn);
        EQN("mouse clamp", a.clamp, b.clamp); EQN("mouse step", a.step, b.step); EQN("mouse interval", a.interval_ms, b.interval_ms);
    }
    EQN("has fbwin", want.fbwin != 0, got.fbwin != 0);
    if (want.fbwin && got.fbwin) {
        const FbWinSpec &a = *want.fbwin, &b = *got.fbwin;
        EQN("fbwin reg_feat", a.reg_feat, b.reg_feat); EQN("fbwin feat_bit", a.feat_bit, b.feat_bit); EQN("fbwin reg_ctrl", a.reg_ctrl, b.reg_ctrl);
        EQN("fbwin ctrl_video_off", a.ctrl_video_off, b.ctrl_video_off); EQN("fbwin reg_diw_h", a.reg_diw_h, b.reg_diw_h); EQN("fbwin reg_diw_v", a.reg_diw_v, b.reg_diw_v);
        EQN("fbwin reg_skip_x", a.reg_skip_x, b.reg_skip_x); EQN("fbwin reg_skip_y", a.reg_skip_y, b.reg_skip_y);
        EQN("fbwin reg_cols", a.reg_cols, b.reg_cols); EQN("fbwin reg_lines", a.reg_lines, b.reg_lines);
        EQN("fbwin x_per_unit", a.x_per_unit, b.x_per_unit); EQN("fbwin x_origin", a.x_origin, b.x_origin); EQN("fbwin y_origin", a.y_origin, b.y_origin);
        EQN("fbwin src_w", a.src_w, b.src_w); EQN("fbwin src_h", a.src_h, b.src_h);
        EQN("fbwin def_skip_x", a.def_skip_x, b.def_skip_x); EQN("fbwin def_skip_y", a.def_skip_y, b.def_skip_y);
        EQN("fbwin def_cols", a.def_cols, b.def_cols); EQN("fbwin def_lines", a.def_lines, b.def_lines);
        EQN("fbwin interval_ms", a.interval_ms, b.interval_ms); EQN("fbwin settle_ms", a.settle_ms, b.settle_ms);
        EQN("fbwin frames_ms", a.frames_ms, b.frames_ms); EQN("fbwin min_apply_ms", a.min_apply_ms, b.min_apply_ms);
    }
    EQN("has romset", want.romset != 0, got.romset != 0);
    if (want.romset && got.romset) {
        const RomSetSpec &a = *want.romset, &b = *got.romset;
        EQS("romset role", a.role, b.role); EQS("romset ext", a.ext, b.ext);
        for (int k = 0; k < ROMSET_SIZES; k++) {
            EQN("romset size", a.sizes[k], b.sizes[k]);
        }
        EQS("romset size_text", a.size_text, b.size_text); EQS("romset example", a.example, b.example); EQN("romset max", a.max, b.max);
        EQS("romset choice_key", a.choice_key, b.choice_key); EQS("romset auto_label", a.auto_label, b.auto_label); EQN("romset want", a.want, b.want);
        EQN("romset head_bytes", a.head_bytes, b.head_bytes); EQN("romset window_off", a.window_off, b.window_off);
        EQN("romset mirror_size", a.mirror_size, b.mirror_size); EQN("romset mirror_stride", a.mirror_stride, b.mirror_stride);
        EQN("romset clear_below", a.clear_below, b.clear_below);
        EQN("romset has labels", a.labels != 0, b.labels != 0);
        for (int i = 0; a.labels && b.labels && i <= ROMSET_MAX; i++) {
            EQS("romset label", a.labels->text[i], b.labels->text[i]);
            EQN("romset item", a.labels->item[i] == a.labels->text[i], b.labels->item[i] == b.labels->text[i]);
        }
    }
    EQN("has window_media", want.window_media != 0, got.window_media != 0);
    if (want.window_media && got.window_media) {
        const WindowMediaSpec &a = *want.window_media, &b = *got.window_media;
        EQS("winmedia prefix", a.prefix, b.prefix); EQN("winmedia nslots", a.nslots, b.nslots); EQN("winmedia base", a.base, b.base); EQN("winmedia stride", a.stride, b.stride);
        EQN("winmedia max_image", a.max_image, b.max_image); EQN("winmedia mount_reg", a.mount_reg, b.mount_reg); EQN("winmedia mount_step", a.mount_step, b.mount_step);
        EQN("winmedia track_bytes", a.track_bytes, b.track_bytes); EQN("winmedia ntracks", a.ntracks, b.ntracks); EQN("winmedia dlog_status", a.dlog_status, b.dlog_status);
        EQN("winmedia dlog_track", a.dlog_track, b.dlog_track); EQN("winmedia dlog_drive", a.dlog_drive, b.dlog_drive); EQN("winmedia dlog_pop", a.dlog_pop, b.dlog_pop);
        EQN("winmedia idle_flush_ms", a.idle_flush_ms, b.idle_flush_ms); EQN("winmedia max_dirty_ms", a.max_dirty_ms, b.max_dirty_ms);
    }
    EQN("has sector_media", want.sector_media != 0, got.sector_media != 0);
    if (want.sector_media && got.sector_media) {
        const SectorMediaSpec &a = *want.sector_media, &b = *got.sector_media;
        EQS("secmedia prefix", a.prefix, b.prefix); EQN("secmedia nunits", a.nunits, b.nunits); EQN("secmedia mount_reg", a.mount_reg, b.mount_reg);
        EQN("secmedia mount_step", a.mount_step, b.mount_step); EQN("secmedia feat_reg", a.feat_reg, b.feat_reg); EQN("secmedia feat_sectors", a.feat_sectors, b.feat_sectors);
        EQN("secmedia mbox_status", a.mbox_status, b.mbox_status); EQN("secmedia mbox_lba", a.mbox_lba, b.mbox_lba); EQN("secmedia mbox_done", a.mbox_done, b.mbox_done);
        EQN("secmedia stage_off", a.stage_off, b.stage_off); EQN("secmedia idle_sync_ms", a.idle_sync_ms, b.idle_sync_ms); EQN("secmedia done_tag_mask", a.done_tag_mask, b.done_tag_mask);
    }
    // the pad (T228 R10, the record's [pad]): the registers, the buttons with their names and default map
    EQN("has pad", want.pad != 0, got.pad != 0);
    if (want.pad && got.pad) {
        const PadSpec &a = *want.pad, &b = *got.pad;
        EQN("pad reg0", a.reg[0], b.reg[0]); EQN("pad reg1", a.reg[1], b.reg[1]); EQN("pad nbuttons", a.nbuttons, b.nbuttons);
        EQN("pad by_id", a.by_id, b.by_id); EQN("pad out", a.out, b.out);
        for (int i = 0; i < a.nbuttons && i < b.nbuttons; i++) {
            EQS("pad name", a.names[i], b.names[i]);
            EQN("pad def_map", a.def_map[i], b.def_map[i]);
        }
    }
}

#endif
