// The board side of the TRX64U application, as far as the core application's base (menuctl.cc, c64.cc) reaches it. The contributors the C64 descriptor registers
// (u64cfg, libfiles, ringops, monitor, online, diag, netops, trx64u_ops) and the monitor session menuctl.cc drives need the board's headers (U64 registers, USB HID,
// FileManager, the network stack, FreeRTOS), which this native build does not have: each registers nothing and answers nothing here. The image registers them,
// and its export is tests/coreapp/golden/ops_trx64u.txt (tests/ui/export.py; the suite's export.feature compares it with a running image). t64.cc is linked for real.
#include <deque>
#include <fstream>
#include <string>
#include <vector>
#include <stdlib.h>
#include "ops.h"
#include "ctx.h"
#include "u64cfg.h"
#include "libfiles.h"
#include "ringops.h"
#include "monitor.h"
#include "online.h"
#include "diag.h"
#include "netops.h"
#include "trx64u_ops.h"

void trx64u_kernal_put(CoreCtx *, const uint8_t *, uint32_t) { }
void trx64u_uci_df1c(CoreCtx *) { }
void trx64u_load_assoc_cfg(CoreCtx *, const char *, bool) { }
void trx64u_basic_put(CoreCtx *, const uint8_t *, uint32_t) { }           // T228 R21 item 7: the SID player's BASIC copy
void trx64u_sid_service(CoreCtx *) { }
void trx64u_sid_cancel(void) { }
void u64cfg_register(OpRegistry &) { }
void u64cfg_power_on_keys(CoreCtx *) { }
__attribute__((weak)) void libfiles_register(OpRegistry &) { }     // tests/coreapp/test_menuctl.cc links the real one
void ringops_register(OpRegistry &) { }
static int stub_mon_key(CoreCtx *c, const OpArgs &a, OpOut &o) { mon_key(c, a.i(0)); o.msg[0] = 0; return OPE_OK; }
static int stub_mon_tick(CoreCtx *c, const OpArgs &, OpOut &o) { o.msg[0] = 0; o.more = mon_tick(c); return OPE_OK; }
static int stub_mon_open(CoreCtx *c, const OpArgs &, OpOut &o) { mon_open(c); o.msg[0] = 0; return OPE_OK; }
static const ArgSpec stub_mon_key_arg[] = { ARG_INT("key", 0, 0x1FFF) };
static const char *stub_mon_wants_stop(CoreCtx *) { return mon_wants_stop() ? "yes" : 0; }
void monitor_register(OpRegistry &r)
{
    r.contributor("monitor");
    r.add_op("monitor.key", stub_mon_key_arg, 1, stub_mon_key);
    r.add_op("monitor.open", 0, 0, stub_mon_open);
    r.add_op("monitor.tick", 0, 0, stub_mon_tick);
    r.add_state("monitor.wants_stop", stub_mon_wants_stop);
}
void online_register(OpRegistry &) { }
void diag_register(OpRegistry &) { }
void netops_register(OpRegistry &) { }

// The last contributor registers what the image's export has and this build does not: every operation with its typed arguments and every state value of
// golden/ops_trx64u.txt (tests/ui/export.py makes it from the real image) that is not registered yet. The handlers do nothing and the states are empty but
// those stub_state_of names below; what they are for is the checks that need the NAMES and the SHAPES: desc_check and tools/descrec.py validate the TRX64U menu record against
// the registry.
static int stub_op(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
int stub_state_calls;                                       // how often a menu asked one of these states (tools/menusim --stats counts it per key)
static const char *stub_state(CoreCtx *) { stub_state_calls++; return ""; }
// T228: the states that decide which rows the TRX64U menu shows, answered as the board does under the MultiCore host with the ring in the bitstream:
// Softboot is not offered (DC1), Save frame and Save the ring can run, the Online form's twelve choice rows stay hidden until the choices are loaded
static const char *stub_multicore(CoreCtx *) { stub_state_calls++; return "MultiCore host"; }
static const char *stub_yes(CoreCtx *) { stub_state_calls++; return "1"; }
static state_fn stub_state_of(const std::string &n)
{
    if (n == "host.multicore") return stub_multicore;
    if (n == "tw.present" || (n.compare(0, 8, "online.p") == 0 && n.size() > 5 && n.compare(n.size() - 5, 5, ".hide") == 0)) return stub_yes;
    return stub_state;
}
void trx64u_register(OpRegistry &r)
{
    std::ifstream f(std::string(GOLDEN_DIR) + "/ops_trx64u.txt");
    std::string line;
    static std::deque<std::string> keep;                    // the names and labels outlive the registry
    static std::deque<std::vector<ArgSpec> > specs;
    static std::deque<std::vector<const char *> > labs;
    while (std::getline(f, line)) {
        std::vector<std::string> p;
        size_t at = 0;
        for (;;) {
            size_t t = line.find('\t', at);
            p.push_back(line.substr(at, t == std::string::npos ? std::string::npos : t - at));
            if (t == std::string::npos) break;
            at = t + 1;
        }
        if (p.size() < 2) continue;
        keep.push_back(p[1]);
        const char *name = keep.back().c_str();
        if (p[0] == "state") {
            if (!r.find_state(name)) r.add_state(name, stub_state_of(p[1]));
            continue;
        }
        if (p[0] != "op" || r.find_op(name)) continue;
        static const char *const tn[] = { "int", "bool", "enum", "string", "path", "slot", "blob" };
        specs.push_back(std::vector<ArgSpec>());
        std::vector<ArgSpec> &sp = specs.back();
        sp.reserve(OP_ARGS_MAX);
        for (size_t k = 2; k < p.size() && sp.size() < OP_ARGS_MAX; k++) {
            std::vector<std::string> q;
            size_t b = 0;
            for (;;) {
                size_t t = p[k].find(':', b);
                q.push_back(p[k].substr(b, t == std::string::npos ? std::string::npos : t - b));
                if (t == std::string::npos) break;
                b = t + 1;
            }
            if (q.size() < 6) continue;
            ArgSpec a = { 0, 0, 0, 0, 0, 0 };
            keep.push_back(q[0]);
            a.name = keep.back().c_str();
            for (int ti = 0; ti < 7; ti++) if (q[1] == tn[ti]) a.type = (uint8_t)ti;
            a.lo = atoi(q[2].c_str());
            a.hi = atoi(q[3].c_str());
            a.maxlen = (uint16_t)atoi(q[4].c_str());
            if (a.type == AT_ENUM) {
                labs.push_back(std::vector<const char *>());
                size_t lb = 0;
                for (;;) {
                    size_t t = q[5].find('|', lb);
                    keep.push_back(q[5].substr(lb, t == std::string::npos ? std::string::npos : t - lb));
                    labs.back().push_back(keep.back().c_str());
                    if (t == std::string::npos) break;
                    lb = t + 1;
                }
                a.labels = labs.back().data();
            }
            sp.push_back(a);
        }
        r.add_op(name, sp.empty() ? 0 : sp.data(), (int)sp.size(), stub_op);
    }
}

// the monitor session (monitor.cc) that menuctl.cc opens on the F4 tab: no debug block here
int stub_mon_open_n;                                        // monitor_grid.cc over the stubs (tests/coreapp/test_cut.cc): what the monitor was asked
void mon_open(CoreCtx *) { stub_mon_open_n++; }
int stub_mon_key_n, stub_mon_key_last;                      // the monitor keys the controller sent (tests/coreapp/test_cut.cc)
bool mon_key(CoreCtx *, int k) { stub_mon_key_n++; stub_mon_key_last = k; return true; }
bool mon_wants_stop(void) { return false; }
bool mon_leave_requested(void) { return false; }
bool mon_diag_requested(void) { return false; }
void mon_release(void) { }
const char *diag_text(int, int *n)
{
    if (n) {
        *n = 0;
    }
    return "";
}
void diag_help_areas(const char *, const char *) { }
int stub_mon_tick_n, stub_mon_render_n;
bool stub_mon_tick_answer;
bool mon_tick(CoreCtx *) { stub_mon_tick_n++; bool a = stub_mon_tick_answer; stub_mon_tick_answer = false; return a; }
void mon_run(CoreCtx *, const char *) { }
void mon_render(void) { stub_mon_render_n++; }
int mon_cols(void) { return 80; }
int mon_rows(void) { return 0; }
const char *mon_line(int) { return ""; }
int mon_attr(int) { return 0; }
int mon_blank_rows(void) { return 0; }
void mon_cursor(int *x, int *y) { *x = 0; *y = 0; }
uint32_t stub_mon_sig;
uint32_t mon_sig(void) { return stub_mon_sig; }

