// The runtime state a core application passes to its descriptor's hooks (T93 D3, D4).
#ifndef COREAPP_CTX_H
#define COREAPP_CTX_H

#include "desc.h"
#include "settings.h"
#include "handover.h"
#include "fsif.h"
#include "board.h"

class Video;
class OpRegistry;
class Executor;
class PartialPort;

// What the Library shows and has marked (T104.2 UI): the menu writes it as the browser moves, the file operations (fileops.cc) read it. One writer, and the
// operations run while the menu waits for them, so there is no lock.
struct LibSel {
    char dir[200];                        // the folder the browser is on ("/" the list of media)
    char cur[FS_PATH_MAX];                    // the full path under the cursor, "" when there is none
    bool cur_dir;                         // ... and it is a folder
    // GitHub #93: the marks and the clipboard grow with the folder (heap, one row per marked name), bounded by a quarter of the free heap (ListGrow::fit).
    // What the budget leaves out is counted (`marked_cut`, `clip_cut`) and every operation says how many it did not take - never silent.
    char (*marked)[FS_NAME_MAX + 1];      // names marked in `dir` (SPACE)
    int nmarked, cap_marked, marked_cut;
    char (*clip)[FS_PATH_MAX];            // the clipboard: full paths
    int nclip, cap_clip, clip_cut;
    bool clip_move;                       // cut, not copy
    bool mark_all, mark_none;             // asked by an operation, taken by the menu
    char home[200];                       // the home folder ("" none)
    char want_dir[200];                   // an operation asks the Library to open this folder (the Home desk), taken by the menu
};

struct CoreCtx {
    const CoreDesc *desc;
    CoreIo *io;
    const BoardProfile *board;            // what the hardware answered at the start (board.h); null: the MultiCore host, as before T104.2
    CoreFs *fs;
    Settings *settings;                   // the layered settings (settings.h); null before they are loaded
    Video *video;                         // the core's picture on the pixel layer; null before the window is set up
    char cor_path[HANDOVER_PATH_MAX];     // "" when the application started without a hand-over (JTAG, developer)
    char cfg_path[HANDOVER_PATH_MAX];     // "" when there is no .cor path: then there is no .cfg either
    char core_version[24];
    char host_version[24];      // T221: the host gateware SemVer from the hand-over ("" = a host that names none)
    char config[48];                      // T198: the config this run started with or has loaded (configs/<name>.cfg), "" = the core's own .cfg
    char settings_path[HANDOVER_PATH_MAX];   // T198: the file the settings are read from and written to: the config's, or "" = cfg_path
    uint32_t host_key;                    // the host build the .cor was routed against, from the hand-over (8 hex digits); 0: none named
    PartialPort *partial;                 // the host's ICAP loader and decoupler (partial.h); null: nothing to reload a partial with
    void *state;                          // the descriptor's own state (mounted disks, loaded program)
    bool started;                         // the core has been released once
    uint32_t rom_key_loaded;              // T216 D16: desc->rom_key() of the settings the ROMs in the machine were put for
    LibSel lib;                           // the Library's cursor, marks and clipboard
    bool paused;                          // Pause (T104.2 UI): the machine stays held after the menu closes, until Resume
    bool frozen;                          // the machine is stopped for the menu (Freeze); a poll of a contributor that does not
                                          // declare poll_when_frozen waits for it to end
    bool menu_open;                       // the menu is on screen, frozen or not; nothing gates a poll on it (T104.1 D6)
    // T228 R1: the screen of the menu an operation asks for ("F1/timewarp", the operation menu.screen of a Home row or a hotkey), taken by the menu: it goes
    // there now, or when it opens (a hotkey while the core runs opens it). "" none.
    char want_screen[32];
    const struct MenuMerge *merge;       // what libraries, applications and the description put into the menu (contrib.h); null: nothing
    struct Engines *eng;                  // the media and ROM engines' state (engines.h); null until a core that declares one asks
    OpRegistry *ops;                    // the service layer (ops.h); null until something asks for it
    Executor *exec;                       // the one task that runs operations; null: they run on the caller (tests, before the start)
    bool program_loaded;                  // a program the core needs (NES) is in
    char notice[200];                     // one line for the screen from a service (a failed write-back), told once
    bool notice_pending;
    void *drive;                          // the cbm1541 library's drive (cbm1541.h); null until the first drive operation
    // D4 of T162: the paths a successful operation took as a file argument (path = AT_PATH), newest overwrites the oldest. No slot keeps its path, only
    // its file name; the bug report finds the path of a mounted image here by that name (ops_find_path). Only names with an extension are noted.
    char noted[16][FS_PATH_MAX];
    int noted_next;
    char media_dir[200];                  // the folder the menu is on when a task asks for a name (menuctl.cc): where a new image goes; empty: the first medium
    // A long write in steps (T122 §5): the menu sets job_step and calls the operation again for as long as it answers `more`; without it
    // the operation runs every chunk itself. The operation reports where it is.
    bool job_step;                        // the caller steps: do one chunk per call and answer more
    bool job_cancel;                      // the caller wants the running job dropped (the partial file is removed)
    int job_pct;                          // 0-100, written by the operation
    char job_path[260];                   // the file being written, written by the operation once it is placed
    // The files the core's bundle installs on the card (record 8, instfiles.h): the application sets it, null in builds that link no bundle; -1 = cannot write.
    int (*install_files)(CoreCtx *c);
    // T104.3: the operation `app.open` asks the application host (apphost.h) to open an application; null: this build has none. The answer is the operation's.
    int (*app_open)(CoreCtx *c, const char *app, const char *action, const char *file, char *msg, int msglen);
    // T228 R21: the action an application adds to one file when its actions are listed ("Run with App", FW user_file_interaction.cc:55-76), null: none. The application sets it.
    const FileAction *(*file_app_action)(CoreCtx *c, const char *name, uint64_t size, bool dir);
    // T180 D4: the picture while a write-back runs ("Saving media..."); the application sets it, null: nothing to show (the media code calls it
    // through MediaSaving, engines.h; saving_depth makes nested write-backs one display).
    void (*saving)(CoreCtx *c, bool on);
    int saving_depth;
    // T226 S1: the picture while a cartridge / ROM image is read in ("Loading cartridge..."); the application sets it, null: nothing to show (MediaLoading, engines.h).
    void (*loading)(CoreCtx *c, bool on);
    int loading_depth;
    // T180 D2: a slot of the core's own media engine (CoreDesc::mount_slots) changed; the engine calls it through mount_notify. The application sets it
    // (mediamounts.cc); null: nothing to store (tests of an engine alone).
    void (*mount_changed)(CoreCtx *c, int slot);
    bool mounts_ready;                    // the start has mounted what the .cfg stores; from here on a mount or unmount is stored
    // The machine is held and its caller releases it: the stored media are being mounted at the start, or a media set is being loaded. A mount then does not
    // reset or release the machine (boot_cold_reset applies the reset-time settings and returns, boot_finish does nothing), the caller does it once.
    bool media_restoring;
};

// T198: the file the settings live in - the config of this run, else the core's own .cfg.
static inline const char *ctx_settings_path(const CoreCtx *c) { return c->settings_path[0] ? c->settings_path : c->cfg_path; }

static inline void mount_notify(CoreCtx *c, int slot)
{
    if (c->mount_changed) {
        c->mount_changed(c, slot);
    }
}

#endif
