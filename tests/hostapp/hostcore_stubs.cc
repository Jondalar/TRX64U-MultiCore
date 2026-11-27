// The modules host_core.cc calls that live on the board or in the hardware's policy (hostvideo, flashlock, ftpstate), as the host tests see them: recorded calls
// instead of a picture, a flash chip and a settings store. Prototypes as in hostvideo.h, flashlock.h and ftpstate.h (no board headers here).
#include <stdio.h>
#include <string.h>

int stub_pic_calls, stub_pic_last = -2, stub_keep_calls, stub_protect = -1, stub_ftp = -1, stub_load_default_calls;
bool stub_pic_ok = true, stub_pending = false, stub_video_moves = false;
int stub_video_sys = -9, stub_video_hdmi = -9;

bool hv_user_set(int hdmi, char *why, int why_n)
{
    stub_pic_calls++;
    stub_pic_last = hdmi;
    if (!stub_pic_ok) {
        snprintf(why, why_n, "the screen does not name it");
        return false;
    }
    return true;
}

bool hv_keep(void)
{
    stub_keep_calls++;
    return stub_pending;
}

bool hv_load_default(int boot_sys, int boot_hdmi)
{
    stub_load_default_calls++;
    stub_video_sys = boot_sys;
    stub_video_hdmi = boot_hdmi;
    return stub_video_moves;
}

void flashprot_set(bool on) { stub_protect = on; }
void ftp_state_set(bool on) { stub_ftp = on; }

// hostvideo.h
#include "hostvideo.h"
int stub_hv_init_calls, stub_hv_start_calls, stub_hv_tick_calls;
HvState stub_hv_state_data;
const HvState *hv_state(void) { return &stub_hv_state_data; }
void hv_init(const HostVideoIo *) { stub_hv_init_calls++; }
int stub_av_applied = -1;
void hv_av_apply(int on) { stub_av_applied = on; }
void hv_start(bool, int *sys, int *hdmi) { stub_hv_start_calls++; *sys = 0; *hdmi = 1; }
HvEvent hv_tick(void) { stub_hv_tick_calls++; return HV_NONE; }

// ---- the start sequence's modules (coreboot.h, legacy.h, flashlock.h): call-recording fakes. core_jump / legacy_jump do not return on the board; here they throw StubJump. ----
#include "coreboot.h"
#include "legacy.h"
#include <string>
#include <vector>

std::vector<std::string> stub_calls;                 // what the sequence called, in order, with the arguments that matter
bool stub_stage_ok = true, stub_swap_ok = true, stub_lock_ok = true, stub_trusted_seen, stub_skip_seen;
int stub_stage_cb_abort = 0;                         // 1: the progress callback is called at the end of the stage and its answer is honoured (as the real stage does)

struct StubJump { };

bool core_stage(const char *, CorInfo *ci, const char **why, core_progress_t cb, void *ctx, const BootEntry *trusted, bool skip_checks)
{
    stub_calls.push_back(std::string("core_stage") + (skip_checks ? " skip" : "") + (trusted ? " trusted" : ""));
    memset(ci, 0, sizeof(*ci));
    if (cb && cb(STEP_VERIFY, 50, 0, ctx) && stub_stage_cb_abort) {
        *why = CORE_ABORTED;
        return false;
    }
    if (!stub_stage_ok) {
        *why = "cannot read the bundle";
        return false;
    }
    return true;
}

bool core_swap(const CorInfo *, const char **why, core_progress_t cb, void *)
{
    stub_calls.push_back(std::string("core_swap") + (cb ? " cb" : " nocb"));
    if (!stub_swap_ok) {
        *why = "the ICAP did not answer";
        return false;
    }
    return true;
}

std::string stub_start_config;                       // T198: what core_set_start_config last got
void core_set_start_config(const char *name) { stub_start_config = name ? name : ""; }

bool stub_start_safe;                                // T224 D2: what core_set_start_safe last got
void core_set_start_safe(bool on) { stub_start_safe = on; }

void core_jump(const CorInfo *)
{
    stub_calls.push_back("core_jump");
    throw StubJump();
}

bool legacy_stage(const char *, LegacyPlan *p, const char **why, core_progress_t cb, void *ctx, bool verify, bool direct)
{
    stub_calls.push_back(std::string("legacy_stage") + (verify ? " verify" : "") + (direct ? " direct" : ""));
    memset(p, 0, sizeof(*p));
    if (cb && cb(STEP_VERIFY, 50, 0, ctx) && stub_stage_cb_abort) {
        *why = CORE_ABORTED;
        return false;
    }
    if (!stub_stage_ok) {
        *why = "cannot read the firmware";
        return false;
    }
    return true;
}

void legacy_jump(const LegacyPlan *)
{
    stub_calls.push_back("legacy_jump");
    throw StubJump();
}

bool flashprot_before_handover(const char **msg)
{
    stub_calls.push_back("lock_flash");
    *msg = stub_lock_ok ? "" : "the status register did not take it";
    return stub_lock_ok;
}
