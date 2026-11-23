#include "resid8.h"
#include "trx_io.h"
#include "drvtab.h"

bool resid8_present(void)
{
    return (trx_io_caps3() & TRX_CAPS3_RESID8) != 0;
}

void resid8_filter(int u, uint8_t src, uint8_t auto_bits, int base, int offset)
{
    if (!resid8_present()) {
        return;
    }
    u &= 1;
    base &= 0x1FF;
    offset &= 0x7FF;
    DRV_resid8_fcsel_WR(u, src & 3);
    DRV_resid8_auto_WR(u, auto_bits & 3);
    DRV_resid8_curve_base_WR(u, base);
    DRV_resid8_curve_offset_WR(u, offset);
}

void resid8_bypass(int u, bool bypass)
{
    if (resid8_present()) {
        DRV_resid8_bypass_WR(u & 1, bypass ? 1 : 0);
    }
}
