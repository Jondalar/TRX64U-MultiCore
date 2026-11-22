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
// Rules: `size` first; members only appended inside a major. Task of every hook: the one the base names in the member; all of them run in the context of a handler, so the
// calls a "lock" task may make are allowed. No hook blocks for long: the menu and the start wait for it.
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
    // menu_closed: the menu closes, by any way, and the machine is about to run. The hook gives back what the core's debugger held and lands what the menu chose: the monitor's release
    // (breakpoints and observers disarmed, the hold given back), the ring's pending frame and its unfreeze. It runs after the menu is hidden and before the machine is let go (C64_STOP
    // is cleared after it), so a frame landed here is where the machine runs on. No result: it cannot refuse. Task: the base's main loop, the context of a handler.
    void (*menu_closed)(void);
} CoreHooks;

#ifdef __cplusplus
}
#endif

#endif
