#include "cfwicon.h"
#include "cfw_icons.h"
#include <string.h>
#include <strings.h>

// The design is field a of the .bit header, which Vivado fills with the name of the top module. Measured in the real
// files (T115 §7): Gideon's firmware 3.15a says "u64_mk2_artix" (his 50T and 100T bitstreams alike), the TRX64 firmware says
// "top" (every release of ours; also the bitstream of a MultiCore host update). Anything else is a firmware we know nothing
// about. "top" is Vivado's usual name, so another project could carry it too: it then gets our logo, which a sidecar corrects.
int cfw_icon_kind(const char *name)
{
    static const char gideon[] = "u64_mk2_artix";
    size_t n = 0;
    while (name[n] && name[n] != ' ') {
        n++;
    }
    if (n >= sizeof(gideon) - 1 && !strncmp(name, gideon, sizeof(gideon) - 1)) {
        return CFW_ICON_KIND_GIDEON;
    }
    if (n == 3 && !strncmp(name, "top", 3)) {
        return CFW_ICON_KIND_TRX64;
    }
    // T254: the name a firmware gives itself (cfw_read_self), which a menu entry added with A carries since. The icon stays the one the
    // .bit header's name gives: Commodore's firmware is built from Gideon's, its bitstreams say "u64_mk2_artix" too (measured in
    // update_1.2.1.cfw and c64u 1.1.0, 2026-11-22); TRX64-Ultimate's say "top".
    if ((n == 6 && !strncmp(name, "Gideon", 6)) || (n == 9 && !strncmp(name, "Commodore", 9))) {
        return CFW_ICON_KIND_GIDEON;
    }
    if (n == 5 && !strncmp(name, "TRX64", 5)) {
        return CFW_ICON_KIND_TRX64;
    }
    return CFW_ICON_KIND_GENERIC;
}

const uint8_t *cfw_icon_builtin(int kind)
{
    switch (kind) {
    case CFW_ICON_KIND_GIDEON: return CFW_ICON_GIDEON;
    case CFW_ICON_KIND_TRX64: return CFW_ICON_TRX64;
    default: return CFW_ICON_GENERIC;
    }
}

bool cfw_icon_sidecar_path(const char *cfw_path, char *out, int n)
{
    size_t l = strlen(cfw_path);
    if (l <= 4 || (strcasecmp(cfw_path + l - 4, ".cfw") && strcasecmp(cfw_path + l - 4, ".ue2")) || (int)l - 4 + 5 >= n) {
        return false;
    }
    memcpy(out, cfw_path, l - 4);
    strcpy(out + l - 4, ".icon");
    return true;
}

bool cfw_icon_load(const char *name, CorSource *side, void *dst)
{
    if (side && side->size() == CFW_ICON_BYTES && side->read(0, dst, CFW_ICON_BYTES)) {
        return true;
    }
    memcpy(dst, cfw_icon_builtin(cfw_icon_kind(name)), CFW_ICON_BYTES);
    return false;
}
