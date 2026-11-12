// The home folder every core has (T180 D10, #40): "Home Directory" (the folder "Set as home folder" in the F3 Library stores) and "Enter Home on Startup" (F3 opens
// there). The application adds both items to the core's own settings when it starts, as it does Persist medium mounts, so the values live in that core's own .cfg and
// no descriptor needs a block for them. A description that declares the items itself (the TRX64U menu) keeps its own and gets none added.
#ifndef COREAPP_HOME_H
#define COREAPP_HOME_H

#include <string.h>
#include "desc.h"
#include "ctx.h"

#define HOME_DIR_KEY "Home Directory"
#define HOME_ENTER_KEY "Enter Home on Startup"
#define REOPEN_KEY "Menu opens at"      // T192.3 D30: 0 "Home (F1)" (default), 1 "Last screen"
#define HOME_OP "home.apply"

// out = *d with the two items after d's own settings; `store` holds the merged array (as volume_extend's) and has to outlive `out`. An item the descriptor declares
// already is not added again. Null, or the reason when there is no room.
const char *home_extend(const CoreDesc *d, CoreDesc *out, Setting *store);
// True for the items home_extend adds (menutree.cc lists them on F8 with the other generic items).
static inline bool home_is_generic(const Setting &s) { return s.apply && !strcmp(s.apply, HOME_OP); }

// What the .cfg holds, read from the core's own settings: the home folder ("" when there is none or the core has no such item) and "Enter Home on Startup".
const char *home_dir(CoreCtx *c);
bool home_enter(CoreCtx *c);
bool home_reopen_last(CoreCtx *c);                 // "Menu opens at" = Last screen

void home_register(OpRegistry &r);

#endif
