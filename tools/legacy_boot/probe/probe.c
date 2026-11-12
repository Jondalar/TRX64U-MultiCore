/* DDR2 retention probe: started by Gideon's boot ROM (boot magic) after an IPROG. Compares the
   pseudo-random pattern legacy_boot.c (mode 2) wrote before the IPROG and prints the result on
   the UART, which Gideon's USER4 console FIFO captures. */
#include <stdint.h>
#define IO8(a)       (*(volatile uint8_t *)(a))
#define UART_DATA    IO8(0x10000010)
#define UART_FLAGS   IO8(0x10000012)
#define PAT_BASE     0x00100000u
#define PAT_WORDS    (0x00200000u / 4)
static void outbyte(int c) { while (UART_FLAGS & 0x10) ; UART_DATA = c; }
static void puts_(const char *s) { while (*s) outbyte(*s++); }
static void hex(uint32_t v) { for (int i = 28; i >= 0; i -= 4) outbyte("0123456789ABCDEF"[(v >> i) & 15]); outbyte(' '); }
static int popc(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
void main(void)
{
    volatile uint32_t *p = (volatile uint32_t *)PAT_BASE;
    uint32_t x = 0x1B0B5EED, bad = 0, bits = 0, up = 0, down = 0, first = 0, last = 0;
    for (uint32_t i = 0; i < PAT_WORDS; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        uint32_t v = p[i];
        if (v != x) {
            if (!bad) first = PAT_BASE + 4 * i;
            last = PAT_BASE + 4 * i;
            bad++;
            bits += popc(v ^ x);
            up += popc(v & ~x);
            down += popc(x & ~v);
        }
    }
    puts_("\n[probe] words "); hex(PAT_WORDS); puts_("bad "); hex(bad); puts_("bits "); hex(bits);
    puts_("0->1 "); hex(up); puts_("1->0 "); hex(down); puts_("first "); hex(first); puts_("last "); hex(last);
    puts_("\n[probe] done\n");
    for (;;) ;
}
