// The window-slot media kind (T104 5.1, 5.2 `media_slot: window`): a raw image lives in a slot of the core's DDR2 window, the core reads and
// writes it there as a floppy, and the base writes the changed tracks back into the file. The Amiga's DF0-DF3 are four of these; any core
// whose drive is a window of memory with a dirty-track log can use the same record.
//
// Mount: the file goes into the slot (zero-filled over the whole stride), then the mount register announces the drive. Write-back: the
// core logs every track it wrote (track number + drive) in a FIFO; the base pops the log, marks the tracks, and writes a drive's marked
// tracks to the file when a burst of writes is over (idle_flush_ms without a new entry) or the drive has been dirty for max_dirty_ms. A log
// that overflowed has lost entries: every track of every writable drive is then suspect and written. Pure: CoreIo and CoreFs only.
#ifndef COREAPP_WINSLOT_H
#define COREAPP_WINSLOT_H

#include "ctx.h"

#define WIN_SLOTS_MAX 4
#define WIN_TRACKS_MAX 160

struct WindowMediaSpec {
    const char *prefix;                   // "DF": the slot is named DF0, DF1 ... in the messages
    uint8_t nslots;                       // 1..WIN_SLOTS_MAX
    uint32_t base, stride;                // slot n starts at DDR2 window offset base + n * stride
    uint32_t max_image;                   // the largest file that is a disk (901,120 for an ADF)
    uint16_t mount_reg, mount_step;       // slot n's MOUNT register: mount_reg + n * mount_step (b0 present)
    uint32_t track_bytes;                 // a log entry names one of these
    uint16_t ntracks;                     // 1..WIN_TRACKS_MAX
    uint16_t dlog_status;                 // b0 not empty, b1 overflow (write b1 to clear)
    uint16_t dlog_track, dlog_drive;      // the head of the log
    uint16_t dlog_pop;                    // any write moves the head on
    uint16_t idle_flush_ms, max_dirty_ms; // 1000, 5000
};

struct WinSlot {
    CoreFile *file;
    bool mounted, writable, err_told;
    bool unsynced;                        // tracks went to the file and the sync after them failed: the file does not have them yet (T180 D4)
    uint8_t dirty[WIN_TRACKS_MAX / 8];
    int ndirty;
    uint32_t first_ms, last_ms;
    uint32_t size;
    char name[48];                        // the file's name, for the status line
    char spec[200];                       // what the .cfg and a media set say about it: the file's path (T180 D2, D5); empty: nothing stored for it
};

struct WinState {
    WinSlot slot[WIN_SLOTS_MAX];
};

// ---- operations (the base registers them as window.mount / window.eject / media.poll's share) ----
// Mounts the file at `path` in slot n. 0 or a reason in msg. A mounted slot is ejected first (flushed, then replaced).
int win_mount(CoreCtx *c, int n, const char *path, char *msg, int msglen);
// Writes this drive's marked tracks back first, then tells the core and closes the file. A write-back that fails keeps the disk mounted: 5 and the
// reason in msg (T180 D4).
int win_eject(CoreCtx *c, int n, char *msg, int msglen);
// Writes the marked tracks of every drive; true when all went to their files, else false with the first drive's reason in msg (T180 D4).
bool win_flush_all(CoreCtx *c, char *msg = 0, int msglen = 0);
// A writable drive holds tracks its file does not have.
bool win_pending(CoreCtx *c);
// Announces every mounted slot to the core again (after a reset).
void win_reannounce(CoreCtx *c);
// One pass of the poll: pops the log, flushes by the policy.
void win_service(CoreCtx *c);
// What is in slot n (the file's name), null when empty. For the status state values.
const char *win_name(CoreCtx *c, int n);

// The state values of the slots, one function per index (a state getter has no argument); the core's contributor registers
// win_state<0> .. win_state<nslots-1> under its own names.
template <int N> const char *win_state(CoreCtx *c) { return win_name(c, N); }

// For the bench: marks a track by hand, as a log entry would.
void win_mark_dirty(CoreCtx *c, int n, int track);

#endif
