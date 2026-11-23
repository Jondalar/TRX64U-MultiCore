// Base 3.3, group `ctx` (T239.3 section 5; boundary rows 4.5, 8.1, 11.3): the state and the fields of the running core that a handler, a hook or a job reads
// or writes. It replaces the members of CoreCtx that the moving code reached (started, paused, frozen, menu_open, notice, media_dir, cor_path, job_*) and the
// definitions that move from trx64u_ops.cc into the core application (trx_target_path). It joins LibApi as `const SvcCtx *ctx`.
//
// Contracts of the whole group:
//   - No call takes a CoreCtx; each acts on the running core.
//   - Task classes: "any" = any task; "lock" = a task that holds the core lock (an operation or state handler, a core hook, a poll); "job" = the function of a
//     job started with SvcJob::start (the job runs without the core lock; the menu's stepped operation is the exception, see job_step).
//   - Strings the base returns are owned by the base and valid until the call that changes them; copy them at once.
#ifndef COREAPP_SVC_CTX_H
#define COREAPP_SVC_CTX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ctx.state bits (CoreCtx::started, paused, frozen, menu_open).
#define CTX_STARTED   0x01u         // the core has been released once
#define CTX_PAUSED    0x02u         // Pause: the machine stays held after the menu closes, until Resume
#define CTX_FROZEN    0x04u         // the machine is stopped for the menu (Freeze)
#define CTX_MENU_OPEN 0x08u         // the menu is on screen, frozen or not
#define CTX_MEDIA_RESTORING 0x10u   // the base mounts what a start or a media set stores (CoreCtx::media_restoring): the machine is held and the base releases it later

typedef struct SvcCtx {
    uint32_t size;
    // The four flags as bits CTX_*. Task: any (a snapshot; the flags change on other tasks).
    uint32_t (*state)(void);
    // Pause (on) or resume (off): the machine stays stopped (io.stop) after the menu closes while paused (CoreCtx::paused). It sets the flag only; the stop itself is the
    // menu's close. The caller tests CTX_STARTED first (op_pause_toggle answers "not running yet" when it is clear). Task: lock.
    void (*set_paused)(bool on);
    // A line the screen shows when the menu next opens, told once (CoreCtx::notice, notice_pending). `only_if_free` true: written only when no notice waits
    // (u64cfg's rule); false: it replaces a waiting one (trx64u_ops' rule). The text is cut to 199 characters. Result: true when it was stored. Replaces
    // ui_notice and ui_notice_pending. Task: any.
    bool (*notice)(const char *text, bool only_if_free);
    // The folder the menu was on when a task asks for a name (CoreCtx::media_dir): where a new file goes. "" when none. Never null. Task: any.
    const char *(*media_dir)(void);
    // The full path of a new file `name` on the medium the menu saves to (trx_target_path): media_dir, or the first medium of the root except /flash; `ext`
    // (".bin") is added when `name` has no extension. Result: false when there is no medium (nothing written to out). A path that does not fit n is cut.
    // Task: any.
    bool (*target_path)(const char *name, const char *ext, char *out, int n);
    // The folder the core's .cor installed its data into: "<folder of the .cor>/<name of the .cor>", no trailing slash; with `sub` ("roms", "saves", "configs",
    // any name) that sub folder; `sub` null is the core folder itself (corefolder_dir, corefolder_sub). Result: false when the core was not started from a
    // .cor (JTAG, developer) or the path does not fit n; out is then "". The core folder and roms, saves, configs are made by the base at the start when the medium can be written; another sub folder is the library's to make. Task: any.
    bool (*core_folder)(const char *sub, char *out, int n);
    // The progress protocol of a write in steps (CoreCtx::job_step, job_cancel, job_pct, job_path; the menu's start_job, newdisk.cc is the pattern).
    // job_step: true when the menu steps this operation: do one chunk per call and answer `more` in the OpResult until done; false: run every chunk in this
    // call. Task: lock, inside the operation's handler.
    bool (*job_step)(void);
    // True when the person asked to drop the running write; the flag is read and cleared by this call, so a second call answers false. The operation then
    // removes its partial file and answers done. Task: lock, inside the operation's handler.
    bool (*job_cancelled)(void);
    // Reports where the write is: pct 0-100, and the file being written once it is placed (null keeps the one reported before). The menu's progress display
    // reads it the next time it draws. Task: lock, inside the operation's handler.
    void (*job_progress)(int pct, const char *path);
    // What is in the media slot named `slot` of the descriptor, as the engine's own spec: the path of the medium, with a word in front when the engine needs
    // one ("ro /usb0/x.d64", "unlinked /usb0/x.d64", "folder /usb0/set"; a path starts with '/'). `slot` is the name of a mount slot without its colon
    // ("D1", "Drive A", "Cartridge": MountSlots::names) or of a media slot of the descriptor (MediaSlot::name, "D1:" with the colon dropped), case insensitive.
    // Result: true with the spec in buf (cut to len - 1); false for no such slot or an empty one (buf is ""). Task: any.
    bool (*media_source)(const char *slot, char *buf, int len);
    // A slot of the core's own engine changed by the engine's doing (CoreHooks::slot_*; mount_notify in ctx.h): the base stores it for "Persist medium mounts" and the
    // media sets once the start has mounted what the .cfg stores. `slot` as in CoreHooks. Task: lock.
    void (*mount_notify)(int slot);
} SvcCtx;

#ifdef __cplusplus
}
#endif

#endif
