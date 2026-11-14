/*
 * T79 ICAP loader test (docs/specs/T79.1-host-core-contract.md section 7): copies LEN bytes from a
 * DDR2 address to the ICAP loader of layer 1 (ultimate_sys.vhd, io 0x10060600) and checks the
 * word count the loader reports. Parameters and results live in DDR2 (JTAG reads them):
 *
 *   0xFF80 source address (CPU view; DDR2 starts at 0)   written by tools/t79/icap_load.py
 *   0xFF84 length in bytes (multiple of 4)               written by tools/t79/icap_load.py
 *   0xFF90 magic: 0x1CA90001 started, 0x1CA90002 finished
 *   0xFF94 status: 0 ok, 1 wrong ID, 2 bad length, 3 word count differs
 *   0xFF98 ID byte read from +0x00
 *   0xFF9C word count read back from +0x18..+0x1B
 *   0xFFA0 words expected (LEN / 4)
 *
 * The console (trxdbg.sh console) shows one line with the same numbers.
 */
#include <stdint.h>

#define IO8(a)          (*(volatile uint8_t *)(a))
#define DDR32(a)        (*(volatile uint32_t *)(a))
#define UART_DATA       IO8(0x10000010)
#define UART_FLAGS      IO8(0x10000012)
#define UART_TX_FULL    0x10

#define ICAP_ID         IO8(0x10060600)
#define ICAP_DATA       IO8(0x10060610)
#define ICAP_CTRL       IO8(0x10060614)
#define ICAP_CNT(n)     IO8(0x10060618 + (n))
#define ICAP_ID_VALUE   0x7A

#define P_SRC           DDR32(0xFF80)
#define P_LEN           DDR32(0xFF84)
#define R_MAGIC         DDR32(0xFF90)
#define R_STATUS        DDR32(0xFF94)
#define R_ID            DDR32(0xFF98)
#define R_COUNT         DDR32(0xFF9C)
#define R_EXPECT        DDR32(0xFFA0)

static void outbyte(int c)
{
    while (UART_FLAGS & UART_TX_FULL)
        ;
    UART_DATA = c;
}

static void puts_(const char *s)
{
    while (*s)
        outbyte(*s++);
}

static void hexword(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4)
        outbyte("0123456789ABCDEF"[(v >> i) & 15]);
    outbyte(' ');
}

void main(void)
{
    const volatile uint8_t *src = (const volatile uint8_t *)P_SRC;
    uint32_t len = P_LEN;
    uint32_t status = 0;
    uint32_t count = 0;

    R_MAGIC = 0x1CA90001;
    R_STATUS = 0xFFFFFFFF;
    R_EXPECT = len / 4;
    R_ID = ICAP_ID;
    if (ICAP_ID != ICAP_ID_VALUE) {
        status = 1;
    } else if (len == 0 || (len & 3)) {
        status = 2;
    } else {
        ICAP_CTRL = 0;                      /* clears the byte counter */
        ICAP_CTRL = 1;                      /* enable, clears the word counter */
        for (uint32_t i = 0; i < len; i++)
            ICAP_DATA = src[i];
        for (volatile int d = 0; d < 1000; d++)
            ;                               /* the last word and the configuration logic settle */
        count = ICAP_CNT(0) | (ICAP_CNT(1) << 8) | (ICAP_CNT(2) << 16) | ((uint32_t)ICAP_CNT(3) << 24);
        ICAP_CTRL = 0;
        if (count != len / 4)
            status = 3;
    }
    R_COUNT = count;
    R_STATUS = status;
    R_MAGIC = 0x1CA90002;
    puts_("\r\nICAP test: status ");
    hexword(status);
    puts_("id ");
    hexword(R_ID);
    puts_("words ");
    hexword(count);
    puts_("expected ");
    hexword(len / 4);
    puts_("\r\n");
    for (;;)
        ;
}
