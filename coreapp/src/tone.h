// The "Tone" setting every core application has (T189): a preset (Flat / Bass boost / Treble boost) of the host's shelf filter in front of the master volume
// (host/trx_tone.v), beside Volume in the Audio group. The application adds the item to the core's own settings when it starts, as it does Volume; the value is
// kept in the core's own .cfg (no host-level value, T189 D5) and written to the host register by regset_apply and on every change. A core that declares a "Tone"
// of its own keeps it; a core with CoreDesc::no_tone (its build has no filter: neogeo until it places) gets none. own_volume does not matter (the C64 line gets it).
#ifndef COREAPP_TONE_H
#define COREAPP_TONE_H

#include "desc.h"
#include "ctx.h"

#define TONE_KEY "Tone"
#define TONE_OP "tone.apply"
#define TONE_STEPS 3                   // Flat, Bass boost, Treble boost: the register's values 0, 1, 2
#define TONE_DEFAULT 0                 // Flat

// out = *d with the Tone item after d's own settings; `store` holds the merged array (SETTINGS_MAX items, as volume_extend's) and has to outlive `out`.
// Returns null, or the reason when there is no room.
const char *tone_extend(const CoreDesc *d, CoreDesc *out, Setting *store);

// True for the item tone_extend adds (the apply operation is ours), false for a descriptor's own.
bool tone_is_generic(const Setting &s);

// The operation behind the item: writes the host's register through CoreIo::master_tone.
void tone_register(OpRegistry &r);

#endif
