#include "tone.h"
#include "menubuild.h"
#include "ops.h"
#include "ops_glue.h"
#include "settings.h"
#include <string.h>
#include <strings.h>

const char *tone_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    if (d->no_tone) {
        return 0;
    }
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, TONE_KEY)) {
            return 0;                                      // the core declares its own Tone item
        }
    }
    const BaseRow *row = menu_base_row(TONE_OP, TONE_KEY);            // the row (values, default) is the base UI TOML's
    if (!row || row->nvalues != TONE_STEPS || row->def != TONE_DEFAULT) {
        return "settings: the base UI TOML has no Tone row that fits the tone register";
    }
    if (d->nsettings + 1 > SETTINGS_MAX) {
        return "settings: no room for the Tone item the application adds";
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    menu_base_setting(store[d->nsettings], *row);
    out->settings = store;
    out->nsettings = d->nsettings + 1;
    return 0;
}

bool tone_is_generic(const Setting &s)
{
    return s.apply && !strcmp(s.apply, TONE_OP);
}

static int op_tone(CoreCtx *c, const OpArgs &a, OpOut &)
{
    c->io->master_tone((uint8_t)a.i(0));
    return OPE_OK;
}
static const ArgSpec tone_args[] = { ARG_INT("value", 0, TONE_STEPS - 1) };

void tone_register(OpRegistry &r)
{
    r.add_op(TONE_OP, tone_args, 1, op_tone);
}
