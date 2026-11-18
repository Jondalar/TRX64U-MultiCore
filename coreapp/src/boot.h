// The start sequence and the resets (T93 D4 steps 4-7). Pure: everything reaches the machine through CoreCtx's io and fs, and
// the screen through BootUi, so tests/coreapp/ runs the whole sequence against a model of the core and checks the register
// writes in order - and that the core is never released with a missing ROM (S2).
#ifndef COREAPP_BOOT_H
#define COREAPP_BOOT_H

#include "ctx.h"
#include "romfind.h"

class BootUi {
public:
    virtual ~BootUi() { }
    virtual void message(const char *title, const char *text, bool bad) = 0;
    virtual void clear() = 0;
    // The wait loop calls this between two passes: the board renders and reads keys here.
    virtual void tick() = 0;
};

enum BootResult {
    BOOT_RUNNING = 0,          // the core is released and runs
    BOOT_WAITING_PROGRAM       // everything is in place but the core needs a program first (the NES waits for a .nes)
};

// Steps 1-7 of D4 with the core held: descriptor init, ROM search and wait (never leaves without every required ROM), ROMs
// into the core, frame buffers and layer, settings into the registers, release. The settings object and the video object
// must be set in the context. Does not return while a required ROM is missing.
BootResult boot_start(CoreCtx *c, BootUi *ui);

// Writes every setting whose `when` is WHEN_RESET (the core must be held) or every one (all = true) into the core.
void boot_apply_settings(CoreCtx *c, bool all);

// The task hooks every core with ROMs shares. All return 0 or a reason in msg.
int boot_reload(CoreCtx *c, char *msg, int msglen);       // scan again, hold, put the ROMs, settings, release
// F8 "Restart core" / "Reload core": boot_reload as a cold boot - the machine RAM cleared (desc->cold_ram_bytes), every setting written, not only
// the reset-time ones. A core that came from a .cor on a host with an ICAP loader (partial.h reload_is_dfx) first has its partial bitstream
// loaded again (checked before anything is touched, then: hold, cut off, stream, give back, hold, the libraries' registers, the picture's
// buffers); any other core runs desc->cold_start when it has one. Nothing leaves to the host.
int boot_restart(CoreCtx *c, char *msg, int msglen);
int boot_cold_reset(CoreCtx *c, char *msg, int msglen);   // hold, the reset-time settings, wait, release
int boot_warm_reset(CoreCtx *c, char *msg, int msglen);   // the descriptor's warm_reset, a cold one when it has none

// The end of every start: the picture's buffers (the first time), the settings into the registers, the reset wait, release. For a
// core that was held to load a program (the NES): the shared tail of the file hook. 0 or a reason.
int boot_finish(CoreCtx *c, char *msg, int msglen);

// One pass of D5's put loop: loads each found file and gives it to the spec's put operation. 0 or a reason.
int boot_put_roms(CoreCtx *c, const RomResult *res, char *msg, int msglen);

// The built-in file of one ROM (the spec whose put operation is put_op) goes into the machine again, as at the start. 0 or a reason.
int boot_put_builtin_rom(CoreCtx *c, const char *put_op, char *msg, int msglen);

// The shared resets as operations of the base contributor: machine.warm_reset, machine.cold_reset, machine.reload_roms.
void boot_register(OpRegistry &r);

#endif
