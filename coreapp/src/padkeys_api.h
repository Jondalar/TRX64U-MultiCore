// The table "padkeys": what the generic application asks of the pad-keys library (padkeys.cc): whether a setting row is a pad-key row (a button's key, padkeys.h PADKEY_ROW) and which entry of the
// key list a pressed key picks. The generic code finds the table through libtab_table("padkeys") (libtab.h) and calls no function of the library; an app linked without it has no pad-key rows.
// Size first; within a major members are only appended. The table is the application's own (not shipped in the SDK).
#ifndef COREAPP_PADKEYS_API_H
#define COREAPP_PADKEYS_API_H

#include <stdint.h>
#include "libtab.h"

struct Setting;

#define PADKEYS_API_NAME "padkeys"

typedef struct PadkeysApi {
    uint32_t size;
    bool (*row)(const Setting *s);                                    // the setting is the PADKEY_ROW shape
    int (*learn_value)(uint64_t down, char *line, int n);             // the row's new value for the keys down (bit = matrix index col*8+row), or -1 with the reason in `line`
    const char *not_available;                                        // the reason for a key that is no entry of the list
} PadkeysApi;

static inline const PadkeysApi *padkeys_api(void)
{
    const PadkeysApi *a = (const PadkeysApi *)libtab_table(PADKEYS_API_NAME);
    return a && a->size >= sizeof(PadkeysApi) ? a : 0;
}

// The generic side: false (not a pad-key row) in an app without the library.
static inline bool padkeys_is_row(const Setting &s)
{
    const PadkeysApi *a = padkeys_api();
    return a && a->row(&s);
}

#endif
