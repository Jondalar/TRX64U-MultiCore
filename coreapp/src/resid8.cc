#include "resid8.h"
#include "trx_io.h"
#include "iomap.h"

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
    ioWrite8(TRX_RS8_FCSEL(u), src & 3);
    ioWrite8(TRX_RS8_AUTO(u), auto_bits & 3);
    ioWrite8(TRX_RS8_BASE(u), uint8_t(base));
    ioWrite8(TRX_RS8_BASE(u) + 1, uint8_t(base >> 8));
    ioWrite8(TRX_RS8_OFFSET(u), uint8_t(offset));
    ioWrite8(TRX_RS8_OFFSET(u) + 1, uint8_t(offset >> 8));
}

void resid8_bypass(int u, bool bypass)
{
    if (resid8_present()) {
        ioWrite8(TRX_RS8_BYPASS(u & 1), bypass ? 1 : 0);
    }
}
