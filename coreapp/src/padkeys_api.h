// The table "padkeys": what the generic application asks of the pad-keys library (padkeys.cc): whether a setting row is a pad-key row (a button's key, padkeys.h PADKEY_ROW) and which entry of the
// key list a pressed key picks. The generic code finds the table through libtab_table("padkeys") (libtab.h) and calls no function of the library; an app linked without it has no pad-key rows.
// Size first; within a major members are only appended. The header ships in the SDK (include/), so a library or a core's app can read the table.
#ifndef COREAPP_PADKEYS_API_H
#define COREAPP_PADKEYS_API_H

#include <stdint.h>

struct Setting;

#define PADKEYS_API_NAME "padkeys"

typedef struct PadkeysApi {
    uint32_t size;
    bool (*row)(const Setting *s);                                    // the setting is the PADKEY_ROW shape
    int (*learn_value)(uint64_t down, char *line, int n);             // the row's new value for the keys down (bit = matrix index col*8+row), or -1 with the reason in `line`
    const char *not_available;                                        // the reason for a key that is no entry of the list
} PadkeysApi;

#endif
