// The table of the SID library (sidfile.cc): its table "sid" (T239.3 D12, base 3.3), its export (base 4.0: declared in the TOML B, `exports`, reached through libslot.h). This file is part of the library: the slot's table function is sid_table.
// Pure functions: no member of the table takes a context, and the library needs none.
#include "libexports.h"
#include "libexp_util.h"
#include "sid_api.h"
#include "sidfile.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---- sid (sid_api.h) ---------------------------------------------------------------------------------------------------------------------------------

static bool sid_t_parse(const uint8_t *head, uint32_t n, const char *name, bool ext_mus, struct SidTune *t) { return sid_parse(head, n, name, ext_mus, t); }
static int sid_t_info_text(const struct SidTune *t, char *out, int outlen) { return sid_info_text(*t, out, outlen); }
static int sid_t_plan(struct SidTune *t, uint32_t fsize, uint16_t load_le, int song, const char **why)
{
    const char *none;                                                 // sid_plan writes *why always; the header allows null
    return sid_plan(t, fsize, load_le, song, why ? why : &none);
}
static int sid_t_requests(const struct SidTune *t, struct SidReq out[3]) { return sid_requests(*t, out); }
static bool sid_t_mus_stereo(const struct SidTune *t, uint32_t load_end, uint8_t (*rd)(void *, uint32_t), void *user, uint32_t *first_end)
{
    return sid_mus_stereo(*t, load_end, rd, user, first_end);
}
static void sid_t_mus_header(struct SidTune *t, bool stereo) { sid_mus_header(t, stereo); }

static const SidApi g_sid = { sizeof(SidApi), sid_t_parse, sid_t_info_text, sid_t_plan, sid_t_requests, sid_t_mus_stereo, sid_t_mus_header };

const void *sid_table(void) { return &g_sid; }
