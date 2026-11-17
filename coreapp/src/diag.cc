// Diagnostics, the F8 tab of that name (T104.2 UI; it was in F4 until T228 DC7): the debug log (what the application printed on the UART, kept in a ring), the EDID of the screen, the heap, the debug register, the trace as
// text. Operations to save them, state values for the rows, and `diag_text()` for the viewer.
#include "diag.h"
#include "corereport.h"
#include "bugreport.h"
#include "usb_hid.h"
#include "netops.h"
#include "ops_glue.h"
#include "trx64u_ops.h"
#include "videomode.h"
#include "trx_ring.h"
#include "u64.h"
#include "FreeRTOS.h"
#include "product.h"
#include <stdio.h>
#include <string.h>

// ---- the log ring: every byte the application prints goes through outbyte(); the linker wraps it (Makefile --wrap=outbyte) --------------------------------
#define LOG_N 24576
static char g_log[LOG_N];
static uint32_t g_head;                                     // the next byte to write; the ring is full once it has wrapped
static bool g_wrapped;

extern "C" void __real_outbyte(int c);
extern "C" void __wrap_outbyte(int c)
{
    if (c != '\r') {
        g_log[g_head++] = (char)c;
        if (g_head == LOG_N) {
            g_head = 0;
            g_wrapped = true;
        }
    }
    __real_outbyte(c);
}

static int log_linear(char *out, int max)
{
    int n = 0;
    if (g_wrapped) {
        for (uint32_t i = g_head; i < LOG_N && n < max; i++) {
            out[n++] = g_log[i];
        }
    }
    for (uint32_t i = 0; i < g_head && n < max; i++) {
        out[n++] = g_log[i];
    }
    return n;
}

static char g_text[28000];

// ---- the bug report (T162): the text from the context (corereport.cc), the log's tail from the ring above -------------------------------------------
static int log_tail(char *out, int max)
{
    uint32_t have = g_wrapped ? LOG_N : g_head;
    uint32_t take = have < (uint32_t)max ? have : (uint32_t)max;
    uint32_t from = (g_head + LOG_N - take) % LOG_N;
    for (uint32_t i = 0; i < take; i++) {
        out[i] = g_log[(from + i) % LOG_N];
    }
    return (int)take;
}
extern "C" uint32_t get_fattime(void);
extern "C" bool rtc_time_valid(void);
static char g_tail[BR_LOG_BYTES];
static char g_rep[CORE_REPORT_MAX];
static volatile bool g_rep_busy;                           // the menu and REST run in two tasks: a second user is told "busy"

// REST: GET /v1/report. The text goes into `body` (the caller's buffer); -1 busy or it does not fit; *stamp is the file name for Content-Disposition.
int diag_report_rest(CoreCtx *c, char *body, int cap, char *stamp, int stamplen)
{
    if (g_rep_busy || cap < CORE_REPORT_MAX) {
        return -1;
    }
    g_rep_busy = true;
    bool valid = rtc_time_valid();
    uint32_t ft = valid ? get_fattime() : 0;
    int tn = log_tail(g_tail, sizeof(g_tail));
    int n = core_report_build(c, ft, valid, g_tail, tn, body, cap);
    char st[BR_STAMP_LEN];
    br_stamp(st, ft, valid);
    snprintf(stamp, stamplen, "%s", st);
    g_rep_busy = false;
    return n;
}

static bool g_help_tw, g_help_mon;
void diag_help_areas(bool timewarp, bool monitor)
{
    g_help_tw = timewarp;
    g_help_mon = monitor;
}

// what the viewer shows: 0 log, 1 EDID (hex), 2 trace, 3 system information
const char *diag_text(int kind, int *n)
{
    static CoreCtx *c = 0;
    (void)c;
    if (kind == 0) {
        *n = log_linear(g_text, (int)sizeof(g_text));
        return g_text;
    }
    if (kind == 1) {
        static uint8_t ed[256];
        int len = 0;
        if (!video_read_edid(ed, sizeof(ed), &len) || len <= 0) {
            *n = snprintf(g_text, sizeof(g_text), "The screen answered no EDID (a TV on a converter, or no screen).\n");
        } else {
            memcpy(g_text, ed, len);
            *n = len;
        }
        return g_text;
    }
    if (kind == 2) {
        TrxTraceStatus st;
        int w = 0;
        if (!trx_ring_trace_status(&st) || !st.present || !st.on) {
            *n = snprintf(g_text, sizeof(g_text), "The instruction trace is off (Settings F6 > TRX64 > Instruction Trace).\n");
            return g_text;
        }
        static TrxTraceRec rec[64];
        uint32_t first = st.records > 64 ? st.records - 64 : 0;
        int got = trx_ring_trace_read(first, 64, rec);
        for (int i = 0; i < got && w < (int)sizeof(g_text) - 100; i++) {
            w += snprintf(g_text + w, sizeof(g_text) - w, "%04x  %02x %02x %02x  A=%02x X=%02x Y=%02x S=%02x P=%02x\n", rec[i].pc, rec[i].opcode, rec[i].b1, rec[i].b2,
                          rec[i].a, rec[i].x, rec[i].y, rec[i].sp, rec[i].p);
        }
        *n = w;
        return g_text;
    }
    if (kind == 5) {
        int w = snprintf(g_text, sizeof(g_text),
            "KEYS OF THE MENU\n\n"
            "F1 .. F8        the areas (the rail names them)\n"
            "1 .. 8          the tabs of an area\n"
            "Cursor keys     move; RETURN selects\n"
            "Left arrow      back\n"
            "R/S, F10, Menu  close the menu (the machine goes on)\n"
            "Letters         the foot of a list names its letters\n\n"
            "THE LIBRARY (F3)\n\n"
            "RETURN          the actions of the file; a folder: open it\n"
            "RIGHT           a folder: go in; on ..: the folder's actions;\n"
            "                else the first action of the file\n"
            "A letter        in the sheet runs the item with that letter\n"
            "SPACE           marks a file; C= A all, C= N none\n"
            "C= C, C= V      copy, paste the marked files\n"
            "/               quick seek: type the name, ? is any character\n"
            "UP on 1st row   the media chips: LEFT/RIGHT, RETURN opens\n");
        if (g_help_tw && w < (int)sizeof(g_text)) {
            w += snprintf(g_text + w, sizeof(g_text) - w,
                "\nTIMEWARP (F2)\n\n"
                "LEFT/RIGHT      one frame\n"
                "UP/DOWN         one second\n"
                "F1/F7 (PAGE)    ten seconds\n"
                "HOME            the oldest frame\n"
                "RETURN          rewind there and run\n"
                "R/S, left arrow cancel\n");
        }
        if (g_help_mon && w < (int)sizeof(g_text)) {
            w += snprintf(g_text + w, sizeof(g_text) - w,
                "\nMONITOR (F4)\n\n"
                "Every key is the monitor's: F1/F7 scroll, F3 help,\n"
                "? lists the commands, x resumes and leaves,\n"
                "R/S leaves while the machine stands.\n"
                "\nBOARD HOTKEYS\n\n"
                "C= R reset, C= Z reboot, C= B power cycle, C= K power off\n"
                "(every menu of the firmware); the menu switch held up\n"
                "for one second resets the machine.\n"
                "At power on: Ctrl-P / Ctrl-N PAL / NTSC,\n"
                "Ctrl-B / S / F the model (the firmware's own).\n");
        }
        *n = w < (int)sizeof(g_text) ? w : (int)sizeof(g_text) - 1;
        return g_text;
    }
    *n = snprintf(g_text, sizeof(g_text), "Board %s (%s)\nHeap free %u bytes\nDebug register %02x\n", getProductString(), getProductFamily(),
                  (unsigned)xPortGetFreeHeapSize(), (unsigned)U64_DEBUG_REGISTER);
    return g_text;
}

// ---- operations ---------------------------------------------------------------------------------------------------------------------------------------
static int save_bytes(CoreCtx *c, const char *name, const char *ext, int kind, OpOut &o)
{
    char p[260];
    if (!trx_target_path(c, name, ext, p, sizeof(p))) {
        snprintf(o.msg, o.msglen, "No medium to save to");
        return OPE_FAILED;
    }
    int n;
    const char *d = diag_text(kind, &n);
    if (!c->fs->save(p, d, (uint32_t)n)) {
        snprintf(o.msg, o.msglen, "Cannot write %s (read only, full or gone)", p);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Saved %s (%d bytes)", p, n);
    return OPE_OK;
}
static int op_save_log(CoreCtx *c, const OpArgs &a, OpOut &o) { return save_bytes(c, a.s(0), ".txt", 0, o); }
static int op_save_edid(CoreCtx *c, const OpArgs &a, OpOut &o) { return save_bytes(c, a.s(0), ".bin", 1, o); }
static int op_bug_report(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (g_rep_busy) {
        snprintf(o.msg, o.msglen, "Another report is being made");
        return OPE_FAILED;
    }
    g_rep_busy = true;
    bool valid = rtc_time_valid();
    uint32_t ft = valid ? get_fattime() : 0;
    int tn = log_tail(g_tail, sizeof(g_tail));
    int n = core_report_build(c, ft, valid, g_tail, tn, g_rep, sizeof(g_rep));
    char name[40], why[80];
    bool ok = core_report_save(c, g_rep, n, ft, valid, name, sizeof(name), why, sizeof(why));
    g_rep_busy = false;
    if (!ok) {
        snprintf(o.msg, o.msglen, "No bug report: %s", why);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Bug report written: %s/%s", core_report_dir(), name);
    return OPE_OK;
}
static int op_nop(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }      // a node whose work is the menu's viewer
static int op_clear_log(CoreCtx *, const OpArgs &, OpOut &o)
{
    g_head = 0;
    g_wrapped = false;
    snprintf(o.msg, o.msglen, "Debug log cleared");
    return OPE_OK;
}

static char sb[64];
static const char *state_heap(CoreCtx *) { snprintf(sb, sizeof(sb), "%u KB free", (unsigned)(xPortGetFreeHeapSize() / 1024)); return sb; }
static const char *state_debugreg(CoreCtx *) { snprintf(sb, sizeof(sb), "$%02X", (unsigned)U64_DEBUG_REGISTER); return sb; }
static const char *state_board(CoreCtx *) { snprintf(sb, sizeof(sb), "%s", getProductString()); return sb; }
static const char *state_uptime(CoreCtx *c)
{
    uint32_t s = c->io->now_ms() / 1000;
    snprintf(sb, sizeof(sb), "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    return sb;
}
// T228 DC1: the application knows it was started by the MultiCore host (the ICAP loader answered, board.h); a row that hides on this state is not offered there
static const char *state_host_multicore(CoreCtx *c) { return !c->board || c->board->can_return_host ? "MultiCore host" : 0; }
static const char *state_log(CoreCtx *) { snprintf(sb, sizeof(sb), "%u KB", (unsigned)((g_wrapped ? LOG_N : g_head) / 1024)); return sb; }
static const char *state_trace(CoreCtx *)
{
    TrxTraceStatus st;
    if (!trx_ring_trace_status(&st) || !st.present) {
        return "no trace in this bitstream";
    }
    snprintf(sb, sizeof(sb), "%s, %u records", st.on ? "on" : "off", (unsigned)st.records);
    return sb;
}
static t_usb_hid_status_snapshot g_hid;
static void hid_refresh(void) { usb_hid_get_status_snapshot(g_hid); }
static const char *st_mouse(CoreCtx *) { hid_refresh(); return g_hid.mouse_name[0] ? g_hid.mouse_name : "none"; }
static const char *st_mouse_mode(CoreCtx *) { hid_refresh(); return g_hid.mouse_mode[0] ? g_hid.mouse_mode : "-"; }
static const char *st_keyboard(CoreCtx *) { hid_refresh(); return g_hid.keyboard_name[0] ? g_hid.keyboard_name : "none"; }
static const char *st_keyboard_mode(CoreCtx *) { hid_refresh(); return g_hid.keyboard_mode[0] ? g_hid.keyboard_mode : "-"; }
// the free room of the media, from the file system (FatFs f_getfree); "none" when the medium is not there
static const char *medium_room(CoreCtx *c, const char *path)
{
    static char b[40];
    uint64_t sz;
    if (!c->fs->free_bytes(path, &sz)) {
        return "none";
    }
    snprintf(b, sizeof(b), "%u MB free", (unsigned)(sz >> 20));
    return b;
}
static const char *st_sd(CoreCtx *c) { return medium_room(c, "/sd"); }
static const char *st_usb0(CoreCtx *c) { return medium_room(c, "/usb0"); }
static const char *st_usb1(CoreCtx *c) { return medium_room(c, "/usb1"); }
static const char *st_flash(CoreCtx *c) { return medium_room(c, "/flash"); }
static const char *state_suggest_log(CoreCtx *) { return "debuglog"; }
static const char *state_suggest_edid(CoreCtx *) { return "edid"; }

static const ArgSpec name_arg[] = { ARG_STRING("name", 55) };

void diag_register(OpRegistry &r)
{
    r.contributor("diag");
    r.add_op("diag.save_log", name_arg, 1, op_save_log);
    r.add_op("diag.save_edid", name_arg, 1, op_save_edid);
    r.add_op("diag.clear_log", 0, 0, op_clear_log);
    r.add_op("diag.bug_report", 0, 0, op_bug_report);
    r.add_op("diag.nop", 0, 0, op_nop);
    r.add_state("usb.mouse", st_mouse);
    r.add_state("usb.mouse_mode", st_mouse_mode);
    r.add_state("usb.keyboard", st_keyboard);
    r.add_state("usb.keyboard_mode", st_keyboard_mode);
    r.add_state("media.sd", st_sd);
    r.add_state("media.usb0", st_usb0);
    r.add_state("media.usb1", st_usb1);
    r.add_state("media.flash", st_flash);
    r.add_state("diag.heap", state_heap);
    r.add_state("diag.debugreg", state_debugreg);
    r.add_state("diag.board", state_board);
    r.add_state("diag.uptime", state_uptime);
    r.add_state("host.multicore", state_host_multicore);
    r.add_state("diag.log", state_log);
    r.add_state("diag.trace", state_trace);
    r.add_state("diag.suggest.log", state_suggest_log);
    r.add_state("diag.suggest.edid", state_suggest_edid);
}
