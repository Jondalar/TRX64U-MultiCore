// Host test of the SD read/write speed choice (host-app/software/io/sd_card/sdio.cc, T149 D5): sdio_set_rw_speed(safe, iob) writes the fast
// divider only when bit 7 of the control register (SDIO_CAP_IOB) reads 1 - which only the T149 block (flat builds of amiga1200 and zxnext) does -
// and the safe one otherwise (host gateware, DFX cores, Gideon's block). Built with -DSDIO_IGNORE_CAP (the control) it always writes the fast
// one and the first check has to fail.
#include "sdio_fake.h"
#include "sdio.h"
#include <stdio.h>

FakeData8 f_data; FakeData32 f_data32; FakeReg8 f_speed, f_ctrl, f_crc, f_switch;
uint8_t fake_spi_xfer(uint8_t) { return 0xFF; }

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main()
{
    // gateware without the block: Gideon's control register has bits 7:4 = 0 (card detect etc. in the low bits)
    f_ctrl.rd = 0x00; f_speed.v = 0xEE;
    CHECK(sdio_set_rw_speed(3, 1) == 3 && f_speed.v == 3);
    f_ctrl.rd = 0x0F; f_speed.v = 0xEE;                    // every low bit set, still the safe one
    CHECK(sdio_set_rw_speed(3, 1) == 3 && f_speed.v == 3);
    // the T149 block: bit 7
    f_ctrl.rd = 0x82; f_speed.v = 0xEE;
    CHECK(sdio_set_rw_speed(3, 1) == 1 && f_speed.v == 1);
    f_ctrl.rd = 0x80;
    CHECK(sdio_set_rw_speed(3, 1) == 1 && f_speed.v == 1);
    // a clock <= 50 MHz build passes 0 and 0
    f_ctrl.rd = 0x80; CHECK(sdio_set_rw_speed(0, 0) == 0 && f_speed.v == 0);
    // the card detect / write protect reading of the driver ignores bit 7
    f_switch.rd = 0x8C; CHECK((sdio_sense() & SD_CARD_DETECT) && (sdio_sense() & SD_CARD_PROTECT));
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
