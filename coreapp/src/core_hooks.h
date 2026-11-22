// The export "corehooks" (major 1): the hooks of a core that its own library provides (T239.3 D10, section 5; boundary 12.1). The code of a core is its library, so the hooks
// of its descriptor (CoreDesc::init, release, warm_reset, cold_start, media_flush, media_pending in desc.h) are named in the core's data (D8) and implemented here. The
// descriptor names the library whose export table they are; the key of that name belongs to the core TOML's specification (D8, D11), not to this header. A core that leaves
// a member null is as before: no hook. The table goes into LibRecord::exports (base 3.2). It is not a member of LibApi.
//
// Two members are new: `start` and `menu_closed`. The earlier draft of the boundary map called the second one "resume"; it is menu_closed.
//
// Differences from core_hook_t of desc.h: no call takes a CoreCtx (the base knows its context) and the state a hook needs comes from the groups of the api (ctx, settings, io,
// ops). A hook returns 0 for done, or non-zero with the reason in msg (cut to msglen - 1; msg is never null here).
//
// Rules: `size` first; members only appended inside a major. Task of every hook: the one the base names in the member; all of them but menu_closed run in the context of a
// handler, so the calls a "lock" task may make are allowed; menu_closed does not hold the core lock (see it). No hook blocks for long: the menu and the start wait for it.
#ifndef COREAPP_CORE_HOOKS_H
#define COREAPP_CORE_HOOKS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CoreHooks {
    uint32_t size;
    // ---- the hooks of the descriptor today, same meaning (desc.h) ------------------------------------------------------------------------------------------------
    // The meaning, the order of calls and the result of each are those of the descriptor member of the same name (CoreDesc in desc.h, called by boot.cc and run.cc); only the
    // argument list changes. cold_start: 0, or a reason (the restart then stops).
    int (*init)(char *msg, int msglen);
    int (*release)(char *msg, int msglen);
    int (*warm_reset)(char *msg, int msglen);
    int (*cold_start)(char *msg, int msglen);
    // media_flush: 0, or a reason (the medium stays mounted). media_pending: whether something waits to be written. media_writeback of the base calls both.
    int (*media_flush)(char *msg, int msglen);
    bool (*media_pending)(void);
    // ---- new in 3.3 ------------------------------------------------------------------------------------------------------------------------------------------------
    // start: called once, after the groups of the api are installed (ops, fs, job, net, sync, settings, ctx, io, board) and the shared libraries named in LIB_NEEDS are loaded, and
    // before the polls start. It is the place a library starts its engine (the ring's buffers and its init, the monitor's engine, uci.start); lib_register is not, because
    // the groups do not exist when it runs (boundary 2.4). A library with no hook starts it in its first poll. Result: 0, or a reason; a non-zero answer is logged and told to the
    // person as a notice (ctx.notice), the core runs on without what failed. Task: the base's start task, before any poll or menu.
    int (*start)(char *msg, int msglen);
    // menu_closed: the menu closes, by any way (the close key, the menu hotkey, a remote `back`), and the machine is about to run. The hook gives back what the core's debugger
    // held and lands what the menu chose: the monitor's release (breakpoints and observers disarmed, the hold given back), the ring's pending frame and its unfreeze.
    // No result: it cannot refuse. The rules of the task, from close_menu in run.cc:
    //   - Which task: the application's main loop, the task that runs the menu; once per close, never from a poll, an operation handler or a job.
    //   - The core lock is NOT held. open_menu has set `frozen` and passed the lock once, so no ordinary poll is in the core and none starts until the close ends (when
    //     the core had started; before the first start there is no freeze and a poll may run). A poll declared `when_frozen` keeps running while the menu is open, so it
    //     may run at the same time, and so may an operation on the executor (a REST request). The hook protects state it shares with its own tasks, its when_frozen
    //     poll or the executor by its own mutex (sync).
    //   - State seen from the hook: ctx.state still has CTX_MENU_OPEN and CTX_FROZEN set (the base clears both after it) and the menu is already hidden; the machine is
    //     still stopped when it was frozen, the base releases it (io.stop off) after the hook returns, so a state loaded here is where the machine runs on.
    //   - Order: it takes the place of the base's mon_release and trx_ring_pending/trx_ring_unfreeze calls, and runs where the second pair runs today, after the menu is hidden
    //     (the monitor's release therefore comes a few lines later than it does now).
    //   - It does not block for long: the person waits for the machine to run.
    void (*menu_closed)(void);
    // The base applied one of its own settings (svc_settings.h: the generic application's settings), `key` its key. The core's hardware that follows the
    // setting is set here: the C64's paddle swap follows "Joystick Input", whose keyboard part the base sets itself. Called after every change and once after
    // the start hook. Null: the core follows none. Task: lock, like a setting's apply operation.
    void (*base_setting_changed)(const char *key);
} CoreHooks;

#ifdef __cplusplus
}
#endif

#endif
