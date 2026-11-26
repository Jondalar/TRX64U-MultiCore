// The registers of the diag core's TOML A by the name a panel file gives them (T260 P5): "block.property" or "block.property.field". Kept apart from the application's code so a unit that
// needs only the names (the test runner's, the panel's) links it without the descriptor.
#include "portdiag_regs.h"
#include "portdiag.h"
#include <string.h>

// The offsets are the driver table's (drvtab.h, made from gateware.toml), so a name
// cannot drift from the register it stands for; tests/coreapp/test_panel.cc resolves every name the shipped panels use.
#define PR(n, reg, bytes, shift, mask) { n, { (uint16_t)PD_OFS(reg), bytes, shift, mask, false } }
static const struct { const char *name; PanelReg r; } reg_names[] = {
    PR("av.std", DRV_av_std_ADDR, 1, 0, 0xFF), PR("av.perm", DRV_av_perm_ADDR, 1, 0, 0xFF), PR("av.rev", DRV_av_rev_ADDR, 1, 0, 0xFF),
    PR("av.vinv", DRV_av_vinv_ADDR, 1, 0, 0xFF), PR("av.vinv.vinv", DRV_av_vinv_ADDR, 1, 0, 0x01), PR("av.vinv.freeze", DRV_av_vinv_ADDR, 1, 1, 0x01),
    PR("av.trim", DRV_av_trim_ADDR, 1, 0, 0xFF), PR("av.bstart", DRV_av_bstart_ADDR, 1, 0, 0xFF), PR("av.bend", DRV_av_bend_ADDR, 2, 0, 0xFFFF),
    PR("av.bamp", DRV_av_bamp_ADDR, 1, 0, 0xFF), PR("av.gain", DRV_av_gain_ADDR, 1, 0, 0xFF), PR("av.sync", DRV_av_sync_ADDR, 1, 0, 0xFF),
    PR("av.blank", DRV_av_blank_ADDR, 1, 0, 0xFF), PR("av.white", DRV_av_white_ADDR, 1, 0, 0xFF), PR("av.syncb", DRV_av_syncb_ADDR, 1, 0, 0xFF),
    PR("av.syncb.eq", DRV_av_syncb_ADDR, 1, 0, 0x01), PR("av.syncb.l262", DRV_av_syncb_ADDR, 1, 1, 0x01), PR("av.vlen", DRV_av_vlen_ADDR, 1, 0, 0xFF),
    PR("av.vpos", DRV_av_vpos_ADDR, 1, 0, 0xFF), PR("av.map", DRV_av_map_ADDR, 1, 0, 0xFF), PR("av.mode", DRV_av_mode_ADDR, 1, 0, 0xFF),
    PR("av.pch", DRV_av_pch_ADDR, 1, 0, 0xFF), PR("av.rch", DRV_av_rch_ADDR, 1, 0, 0xFF), PR("av.synm", DRV_av_synm_ADDR, 1, 0, 0xFF),
    PR("av.fbm", DRV_av_fbm_ADDR, 1, 0, 0xFF),
};
bool portdiag_reg_by_name(const char *name, PanelReg *out)
{
    for (unsigned i = 0; i < sizeof(reg_names) / sizeof(reg_names[0]); i++) {
        if (!strcmp(reg_names[i].name, name)) {
            *out = reg_names[i].r;
            return true;
        }
    }
    return false;
}

