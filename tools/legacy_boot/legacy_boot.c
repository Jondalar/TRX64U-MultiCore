/*
 * Legacy boot PoC: start Gideon's complete firmware (bitstream + application) from a vanilla .cfw,
 * on a board whose flash holds the TRX64 MultiCore host at 0x0.
 *
 *   1. the .cfw lies in DDR2 (tools: trxdbg ddrload); find its 50T bitstream and its application;
 *   2. find the same bitstream as a file in the flash drive (FAT12 from flash 0x400000), check that
 *      its clusters are contiguous and that its bytes equal the .cfw's;
 *   3. copy the application to its load address, put the boot magic at 0xFFF8/0xFFFC (Gideon's boot
 *      ROM jumps there instead of loading 0x220000 from flash); every address through remap(), the
 *      DDR2 address map of Gideon's controller (docs/specs/T115-full-bitstream-cores.md section 3);
 *   4. ICAP: WBSTAR := the file's flash address, IPROG. The FPGA loads Gideon's bitstream from flash.
 *
 * Parameters and results in DDR2 at 0x02810000 (JTAG reads them), one console line per step.
 *   +0x00 .cfw address   +0x04 .cfw length   +0x08 mode: 0 dry run (steps 1-2), 1 go, 2 probe instead of
 *   the application (probe/, staged at +0x0C, length +0x10), 3 pattern only, no .cfw (WBSTAR from +0x14)
 *   +0x20 magic 0x1B0B0001 started, 0x1B0B0002 dry run done / about to IPROG
 *   +0x24 status (0 ok, else the step that failed)   +0x28 flash address   +0x2C file length
 */
#include <stdint.h>

#define IO8(a)          (*(volatile uint8_t *)(a))
#define DDR32(a)        (*(volatile uint32_t *)(a))
#define UART_DATA       IO8(0x10000010)
#define UART_FLAGS      IO8(0x10000012)
#define UART_TX_FULL    0x10

#define SPI_DATA        IO8(0x10060200)
#define SPI_CTRL        IO8(0x10060208)
#define SPI_FORCE_SS    0x01

#define ICAP_ID         IO8(0x10060600)
#define ICAP_DATA       IO8(0x10060610)
#define ICAP_EN         IO8(0x10060614)
#define ICAP_DC         IO8(0x10060620)

#define P_BASE          0x02810000u
#define P_CFW           DDR32(P_BASE + 0x00)
#define P_LEN           DDR32(P_BASE + 0x04)
#define P_GO            DDR32(P_BASE + 0x08)
#define P_PROBE         DDR32(P_BASE + 0x0C)
#define P_PROBE_LEN     DDR32(P_BASE + 0x10)
#define P_FADDR         DDR32(P_BASE + 0x14)
#define R_MAGIC         DDR32(P_BASE + 0x20)
#define R_STATUS        DDR32(P_BASE + 0x24)
#define R_ADDR          DDR32(P_BASE + 0x28)
#define R_FLEN          DDR32(P_BASE + 0x2C)

#define VOL_BASE        0x400000u
#define FILE_NAME       "G315A50TBIT"      /* 8.3 name in the directory entry, no dot */
#define APP_LOAD        0x00030000u
#define BOOT_JUMP       DDR32(0xFFF8)
#define BOOT_MAGIC      DDR32(0xFFFC)
#define BOOT_MAGIC_VAL  0x1571BABEu

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

static void hex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4)
        outbyte("0123456789ABCDEF"[(v >> i) & 15]);
    outbyte(' ');
}

static int fail(int step, const char *why)
{
    puts_("[legacy] FAIL: ");
    puts_(why);
    puts_("\n");
    R_STATUS = step;
    return step;
}

/* ---- SPI flash, read only (W25Q, 0x03 read), as Gideon's boot loader does */
static void flash_read(uint32_t addr, uint8_t *buf, uint32_t n)
{
    SPI_CTRL = SPI_FORCE_SS;
    SPI_DATA = 0x03;
    SPI_DATA = (uint8_t)(addr >> 16);
    SPI_DATA = (uint8_t)(addr >> 8);
    SPI_DATA = (uint8_t)addr;
    for (uint32_t i = 0; i < n; i++)
        buf[i] = SPI_DATA;
    SPI_CTRL = 0;
}

static uint32_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static int mem_eq(const uint8_t *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != (uint8_t)b[i])
            return 0;
    return 1;
}

static int contains(const char *s, uint32_t n, const char *w)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = 0;
        while (w[k] && i + k < n && s[i + k] == w[k])
            k++;
        if (!w[k])
            return 1;
    }
    return 0;
}

static const uint8_t BIT_MAGIC[13] = { 0x00, 0x09, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x00, 0x00, 0x01 };

/* A .bit file at p (bounded by end): its total length (header + data) and whether its part is a 7a50t. */
static uint32_t bit_file(const uint8_t *p, const uint8_t *end, int *is50t)
{
    if (end - p < 64 || !mem_eq(p, (const char *)BIT_MAGIC, 13))
        return 0;
    const uint8_t *q = p + 13;
    *is50t = 0;
    for (int f = 0; f < 6 && q + 3 < end; f++) {
        uint8_t k = *q++;
        if (k == 'e')
            return (uint32_t)(q + 4 - p) + be32(q);
        uint32_t n = be16(q);
        q += 2;
        if (k == 'b' && contains((const char *)q, n, "7a50t"))
            *is50t = 1;
        q += n;
    }
    return 0;
}

/* Where a byte Gideon's CPU sees at g lies for ours: measured on the board 2026-10-21 (pattern written
   by our host, read through Gideon's USER4 after the IPROG; 300 of 300 random addresses, nothing lost).
   Gideon's controller moves 8-byte units (BL4), ours 16-byte (BL8): only address bits 6:3 differ,
   ours[3] = g[6], ours[6:4] = g[5:3]; every other bit is the same. */
static uint32_t remap(uint32_t g)
{
    return (g & ~0x78u) | (((g >> 6) & 1) << 3) | (((g >> 3) & 7) << 4);
}
#define GW(g)  (*(volatile uint32_t *)remap(g))

static void iprog(uint32_t faddr)
{
    /* 4. dummy, sync, NOP, WBSTAR := faddr, CMD := IPROG, NOP, NOP - bytes in bitstream (file) order */
    uint8_t seq[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0x99, 0x55, 0x66, 0x20, 0x00, 0x00, 0x00,
        0x30, 0x02, 0x00, 0x01, (uint8_t)(faddr >> 24), (uint8_t)(faddr >> 16), (uint8_t)(faddr >> 8), (uint8_t)faddr,
        0x30, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F,
        0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    };
    ICAP_DC = 0;
    ICAP_EN = 1;
    for (unsigned i = 0; i < sizeof(seq); i++)
        ICAP_DATA = seq[i];
    ICAP_EN = 0;
    for (;;)
        ;
}

static uint8_t sec[512];
static uint8_t fat[2];
static uint8_t chunk[4096];

void main(void)
{
    R_MAGIC = 0x1B0B0001;
    R_STATUS = 0xFF;
    const uint8_t *cfw = (const uint8_t *)P_CFW;
    const uint8_t *cend = cfw + P_LEN;
    puts_("[legacy] start, .cfw at "); hex(P_CFW); puts_("len "); hex(P_LEN); puts_(P_GO ? "GO\n" : "dry run\n");

    if (ICAP_ID != 0x7A) { fail(1, "no ICAP loader (ID byte is not 0x7A)"); return; }
    /* mode 3 (DDR2 map/retention): no .cfw; the pattern only (mode 2's fill), WBSTAR from P_FADDR */
    if (P_GO == 3) {
        volatile uint32_t *pat = (volatile uint32_t *)0x00100000;
        uint32_t x = 0x1B0B5EED;
        for (uint32_t i = 0; i < 0x00200000u / 4; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            pat[i] = x;                       /* plain, NOT remapped: the read-back finds where it lands */
        }
        GW(0xFFF8) = 0x0000FFF0;              /* jump to 0xFFF0: an endless loop there */
        GW(0xFFF0) = 0x0000006F;              /* j . (jal x0, 0) */
        GW(0xFFFC) = BOOT_MAGIC_VAL;
        iprog(P_FADDR);
    }
    if (P_LEN < 0x1000 || !mem_eq(cfw, "\xDE\xAD\x15\x41U64MK2", 10)) { fail(2, "not a U64MK2 .cfw"); return; }

    /* 1. the .cfw: the 50T bitstream, then the application record after the last bitstream */
    const uint8_t *bit = 0, *last_end = 0;
    uint32_t bit_len = 0;
    for (const uint8_t *p = cfw; p + 64 < cend; p++) {
        int is50 = 0;
        uint32_t n = bit_file(p, cend, &is50);
        if (!n || p + n > cend)
            continue;
        if (is50 && !bit) { bit = p; bit_len = n; }
        last_end = p + n;
        p += n - 1;
    }
    if (!bit) { fail(3, "no 7a50t bitstream in the .cfw"); return; }
    const uint8_t *app = 0;
    for (const uint8_t *a = (const uint8_t *)(((uint32_t)last_end + 3) & ~3u); a <= last_end + 16; a += 4)
        if (le32(a) == APP_LOAD && le32(a + 8) == APP_LOAD && le32(a + 4) > 0x10000 && le32(a + 4) < 0x400000) { app = a; break; }
    if (!app) { fail(4, "no application record after the bitstreams"); return; }
    uint32_t app_len = le32(app + 4);
    puts_("[legacy] .cfw: 50T bitstream at +"); hex(bit - cfw); puts_("len "); hex(bit_len);
    puts_("app at +"); hex(app - cfw); puts_("len "); hex(app_len); puts_("\n");

    /* 2. the file in the flash drive (FAT12 at 0x400000) */
    flash_read(VOL_BASE, sec, 512);
    uint32_t bps = le16(sec + 11), spc = sec[13], rsv = le16(sec + 14), nfat = sec[16];
    uint32_t nroot = le16(sec + 17), spf = le16(sec + 22);
    puts_("[legacy] FAT: bps "); hex(bps); puts_("spc "); hex(spc); puts_("rsv "); hex(rsv); puts_("fats "); hex(nfat);
    puts_("root "); hex(nroot); puts_("spf "); hex(spf); puts_("sig "); hex(le16(sec + 510)); puts_("\n");
    if (le16(sec + 510) != 0xAA55 || bps < 512 || bps > 4096 || !spc || !nfat || !spf) { fail(5, "no FAT boot sector at 0x400000"); return; }
    uint32_t fat0 = VOL_BASE + rsv * bps;
    uint32_t root = fat0 + nfat * spf * bps;
    uint32_t data = root + ((nroot * 32 + bps - 1) / bps) * bps;
    uint32_t csize = spc * bps;
    uint32_t c0 = 0, flen = 0;
    for (uint32_t e = 0; e < nroot && !c0; e++) {
        flash_read(root + e * 32, sec, 32);
        if (sec[0] == 0)
            break;
        if (mem_eq(sec, FILE_NAME, 11) && !(sec[11] & 0x18)) { c0 = le16(sec + 26); flen = le32(sec + 28); }
    }
    if (!c0) { fail(6, "file G315A50T.BIT not in the flash drive's root"); return; }
    uint32_t c = c0, count = 0;
    for (;;) {
        uint32_t off = c + c / 2;
        flash_read(fat0 + off, fat, 2);
        uint32_t next = (c & 1) ? (le16(fat) >> 4) : (le16(fat) & 0xFFF);
        count++;
        if (next >= 0xFF8)
            break;
        if (next != c + 1) { puts_("[legacy] cluster "); hex(c); puts_("-> "); hex(next); puts_("\n"); fail(7, "the file is fragmented"); return; }
        c = next;
    }
    uint32_t faddr = data + (c0 - 2) * csize;
    R_ADDR = faddr;
    R_FLEN = flen;
    puts_("[legacy] file: cluster "); hex(c0); puts_("count "); hex(count); puts_("len "); hex(flen);
    puts_("flash "); hex(faddr); puts_("\n");
    if (flen != bit_len || count * csize < flen) { fail(8, "file length differs from the .cfw's bitstream"); return; }
    for (uint32_t done = 0; done < flen; done += sizeof(chunk)) {
        uint32_t n = flen - done < sizeof(chunk) ? flen - done : sizeof(chunk);
        flash_read(faddr + done, chunk, n);
        for (uint32_t i = 0; i < n; i++)
            if (chunk[i] != bit[done + i]) { puts_("[legacy] differs at +"); hex(done + i); puts_("\n"); fail(9, "file bytes differ from the .cfw's bitstream"); return; }
    }
    puts_("[legacy] file equals the .cfw's 50T bitstream\n");

    if (!P_GO) {
        R_STATUS = 0;
        R_MAGIC = 0x1B0B0002;
        puts_("[legacy] dry run done: nothing copied, no IPROG\n");
        return;
    }

    /* mode 2 (DDR2 retention): instead of Gideon's application, the probe (staged by JTAG at
       P_PROBE, P_PROBE_LEN bytes) and 2 MB of xorshift pattern at 0x100000 (probe.c checks it) */
    if (P_GO == 2) {
        volatile uint32_t *pat = (volatile uint32_t *)0x00100000;
        uint32_t x = 0x1B0B5EED;
        for (uint32_t i = 0; i < 0x00200000u / 4; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            GW((uint32_t)&pat[i]) = x;
        }
        app = (const uint8_t *)P_PROBE - 12;
        app_len = P_PROBE_LEN;
        puts_("[legacy] mode 2: pattern 2 MB at 00100000, probe "); hex(app_len); puts_("\n");
    }

    /* 3. Gideon's application to its load address, the boot magic for his boot ROM */
    volatile uint32_t *dst = (volatile uint32_t *)APP_LOAD;
    const uint8_t *src = app + 12;
    for (uint32_t i = 0; i < (app_len + 3) / 4; i++)
        GW((uint32_t)&dst[i]) = le32(src + 4 * i);
    for (uint32_t i = 0; i < (app_len + 3) / 4; i++)
        if (GW((uint32_t)&dst[i]) != le32(src + 4 * i)) { fail(10, "application copy does not read back"); return; }
    GW(0xFFF8) = APP_LOAD;
    GW(0xFFFC) = BOOT_MAGIC_VAL;
    R_STATUS = 0;
    R_MAGIC = 0x1B0B0002;
    puts_("[legacy] app copied, magic set; IPROG to "); hex(faddr); puts_("\n");
    for (volatile int i = 0; i < 200000; i++)   /* let the console line out */
        ;

    iprog(faddr);
}

