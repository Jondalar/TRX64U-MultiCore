// The mounted media kept across a restart, and the named media sets (T180 D2, D5).
//
// The slots are the ones the base mounts itself: the window drives (winslot.h, the Amiga's DF0..DF3) and the sector units (sectors.h: HD0, HD1, the Next's SD),
// folder disks included, and then the slots of the core's own media engine (desc.h MountSlots: the Apple's D1/D2/S7.1/S7.2, the Atari's D1..D4 and Cartridge,
// the CPC's A/B/Tape, the drive library's Drive A, the cartridge ports, a game cartridge). Every slot has a name (the descriptor's prefix and the number: DF0, HD1,
// or the engine's word) and a "spec", the words the .cfg and a set file use for what is in it: the file's path, or "folder <path>, <GB> GB" for a folder disk (the
// size as it was typed), or the engine's own words (a word in front of the path: "ro /usb0/x.d64").
//
//   D2  "Persist medium mounts" (Yes/No, F8, default No; the application adds the item like Picture and Volume, for a core that has such slots). With Yes every mount
//       and every unmount is written into the core's .cfg at once, one key per slot ("Mount DF0=/usb0/WB32/Install.adf"); boot_start mounts them after the ROMs
//       and the settings and before the core is released. A stored medium that is not there leaves its slot empty with a note; the core starts, and the key stays
//       for the next start until the slot is mounted or ejected by hand. With No nothing stored is mounted; switching to No removes the keys.
//   D5  A media set is the same keys in "<core folder>/media/<name>.mnt". Saving writes what is mounted now; loading writes everything back first (D4), mounts what
//       the set holds, unmounts the slots it leaves empty and resets the core once.
//
// The engines that keep their own mount state hand their slots over through CoreDesc::mount_slots and tell the application when one changes (mount_notify).
// While the stored media are mounted at the start, and while a set is loaded, CoreCtx::media_restoring is set: the machine is held, no mount resets or releases
// it (boot_cold_reset applies the reset-time settings and returns, boot_finish does nothing), and the caller does that once.
#ifndef COREAPP_MEDIAMOUNTS_H
#define COREAPP_MEDIAMOUNTS_H

#include <string.h>
#include "ctx.h"

class BootUi;

#define PERSIST_KEY "Persist medium mounts"
#define PERSIST_OP "media.persist_apply"
#define MEDIASET_NAME_MAX 24
#define MEDIASETS_MAX 32
#define MOUNT_SLOTS_MAX 8                 // the most slots a descriptor's MountSlots has

// out = *d with the Persist item after d's own settings, for a core that has media slots (mounts_have_slots); `store` holds the merged array (as volume_extend's) and has to
// outlive `out`. Null, or the reason when there is no room.
const char *persist_extend(const CoreDesc *d, CoreDesc *out, Setting *store);
// True for the item persist_extend adds.
static inline bool persist_is_generic(const Setting &s) { return s.apply && !strcmp(s.apply, PERSIST_OP); }
// The core has slots that a mount can be stored for (menutree.cc: the media tab gets "Save media set..." and "Load media set..." then).
static inline bool mounts_have_slots(const CoreDesc *d) { return d->window_media || d->sector_media || d->mount_slots; }
// The item says Yes.
bool persist_on(CoreCtx *c);

// The slots: the windows first, then the sector units.
int mounts_slots(CoreCtx *c);
void mounts_slot_name(CoreCtx *c, int idx, char *out, int n);
// What is mounted in the slot, in the words of the file; null: empty (or a path too long to store).
const char *mounts_slot_spec(CoreCtx *c, int idx);

// winslot.cc / sectors.cc: a slot was mounted or let go. With the item on (and the start done) the .cfg gets the new state of that slot.
void mounts_changed(CoreCtx *c, bool sector, int n);
// CoreCtx::mount_changed (the application sets it): slot n of the core's own engine (desc.h MountSlots) changed.
void mounts_engine_changed(CoreCtx *c, int n);
// boot_start: the stored mounts go in (item on), then later changes are stored. `ui` shows "Mounting the folder..." for a folder disk; may be null.
void mounts_restore(CoreCtx *c, BootUi *ui);

// ---- media sets (D5)
// The folder of the sets, "<core folder>/media". False when the core has no folder (it did not start from a .cor).
bool mediaset_dir(CoreCtx *c, char *out, int n);
// The names of the sets (the .mnt files without the extension), at most `max`; -1 when there is no folder to look in. 0 when there are none.
int mediaset_list(CoreCtx *c, char names[][MEDIASET_NAME_MAX + 1], int max);
// 0, or a reason in msg.
int mediaset_save(CoreCtx *c, const char *name, char *msg, int msglen);
// Writes everything back, mounts what the set holds, unmounts the rest, resets the core once. 0 (msg tells what was left out), or a reason when nothing was done
// (no such set, a write-back that failed: the media stay as they are and the core is not reset).
int mediaset_load(CoreCtx *c, const char *name, char *msg, int msglen);

// The operations: media.persist_apply (the item's apply), media.save_set(name), media.load_set(name); the state media.sets (the count).
void mounts_register(OpRegistry &r);

#endif
