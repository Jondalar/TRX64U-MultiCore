// T101: the layout resolution of the host app is a build choice (UI_RES); what the port writes into the pixel layer follows from it.
// Compiled once per value by run.sh; the expected values are the registers of host/pix_layer.v (PX_CTRL b5:4 scale, PX_POS {y0, x0}).
#include <stdio.h>
#include <stdint.h>
#include "trx_lv_port.h"

#ifndef WANT_SCALE
#error "WANT_SCALE"
#endif

int main()
{
    int fails = 0;
    uint32_t ctrl = 0x02 | (TRX_LV_SCALE << 4), pos = ((uint32_t)TRX_LV_Y0 << 16) | TRX_LV_X0;
    printf("UI_RES %d: %d x %d at scale %d, PX_CTRL 0x%02x, PX_POS 0x%08x\n", UI_RES, TRX_LV_W, TRX_LV_H, TRX_LV_SCALE, (unsigned)ctrl, (unsigned)pos);
    if (TRX_LV_SCALE != WANT_SCALE) { printf("FAIL scale\n"); fails++; }
    if (TRX_LV_W > 960) { printf("FAIL the pixel layer's line buffer holds 960 pixels (pix_layer.v f_w)\n"); fails++; }
    if (TRX_LV_X0 + TRX_LV_W * TRX_LV_SCALE > 1920 || TRX_LV_Y0 + TRX_LV_H * TRX_LV_SCALE > 1080) { printf("FAIL outside the output\n"); fails++; }
    if (TRX_LV_X0 * 2 + TRX_LV_W * TRX_LV_SCALE != 1920 || TRX_LV_Y0 * 2 + TRX_LV_H * TRX_LV_SCALE != 1080) { printf("FAIL not centred\n"); fails++; }
    if (UI_RES == 0 && (ctrl != 0x22 || pos != 0)) { printf("FAIL the default is no longer today's PX_CTRL 0x22 at 0, 0\n"); fails++; }
    return fails != 0;
}
