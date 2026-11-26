// The media and ROM engines of the base (T104 5.1/5.2): the state they keep per run, the operations they register, and the one poll that
// serves them. A core declares the records (CoreDesc::romset, window_media, sector_media, mouse); nothing here names a core.
//
// Operations (registered by the base contributor, so every core can bind them by name):
//   window.mount(path, slot)   window.eject(slot)        a raw image in a window slot (winslot.h)
//   sectors.mount(path, unit)  sectors.eject(unit)       an image served sector by sector (sectors.h)
//   sectors.mount_folder(path, unit)                      a folder as a hard disk (folderhd.h); path is "<folder>/<size in GB>"; state sectors.folder.offer suggests the size
//   romset.put(data)                                      the chosen member of the ROM set into the window (romset.h)
//   media.poll                                            the service: the sector mailbox, the dirty log and its flush policy, the mouse
// A core's poll descriptor names media.poll as its service_op.
#ifndef COREAPP_ENGINES_H
#define COREAPP_ENGINES_H

#include "ctx.h"
#include "romset.h"
#include "winslot.h"
#include "sectors.h"
#include "mouse_api.h"
#include "rawkeys.h"
#include "fbwin.h"
#include "ledload.h"
#include "writeback.h"

struct Engines {
    WinState win;
    SecState sec;
    RomSetState rs;
    FbWinState fw;
    LedLoad led;
    uint8_t flush_div;
};

// The engines of this run; made on first use, zeroed, the core's mouse sink made known.
Engines *engines_of(CoreCtx *c);

// T180 D4: the picture says "Saving media..." for as long as this object lives, when there is something to write (the application's c->saving).
class MediaSaving {
public:
    MediaSaving(CoreCtx *c, bool pending) : c_(pending && c->saving ? c : 0)
    {
        if (c_ && c_->saving_depth++ == 0) {
            c_->saving(c_, true);
        }
    }
    ~MediaSaving()
    {
        if (c_ && --c_->saving_depth == 0) {
            c_->saving(c_, false);
        }
    }
private:
    CoreCtx *c_;
};

// T226 S1: the picture says "Loading cartridge..." for as long as this object lives (a cartridge or ROM image is being read in; the application's c->loading).
class MediaLoading {
public:
    explicit MediaLoading(CoreCtx *c) : c_(c->loading ? c : 0)
    {
        if (c_ && c_->loading_depth++ == 0) {
            c_->loading(c_, true);
        }
    }
    ~MediaLoading()
    {
        if (c_ && --c_->loading_depth == 0) {
            c_->loading(c_, false);
        }
    }
private:
    CoreCtx *c_;
};

// T180 D4: everything pending for a writable medium goes back to its file, completely: the cartridge (wb_flush, as its setting says), every window
// drive, every sector unit with the folder disk's full fold-back. 0, or 1 with the first reason in msg: the medium stays mounted and the caller
// stops what it was about to do (reset, reload, restart, return to host, power off).
int media_writeback(CoreCtx *c, WbWhy why, char *msg, int msglen);

// A drive that is dirty is made consistent before a machine hold; every image is announced again after it.
void engines_before_hold(CoreCtx *c);
void engines_after_hold(CoreCtx *c);

// The core's partial bitstream was loaded again: the state that remembers registers of the old core is cleared (boot.cc, Reload core).
void engines_after_reload(CoreCtx *c);

// One pass of the media service (sector mailbox, window dirty log, idle sync, mouse, picture window); true: call again at once.
bool engines_poll(CoreCtx *c);
void engines_register(OpRegistry &r);

#endif
