/*
 * TRX64 boot ROM for rvlite, made from Gideon's U64-II boot loader
 * (vendor/gideon/software/portable/riscv/bootloader_u64ii.c) for our bitstream
 * (docs/specs/T15-flash-boot.md):
 *
 *   - initializeDDR2() (his own DDR2 PHY) becomes a wait for the DDR2 controller's
 *     calibration (done in hardware, host/cal_seq.v), read at 0x10100100
 *     bit 0 (host/ultimate_sys.vhd); it waits for ever when that fails.
 *   - init_ext_pll() is left out: the clock synthesizers belong to our top
 *     level, and the app programs them itself later.
 *   - JTAG load first, as in the original: tools/trxdbg.py writes the app to
 *     DDR2, its entry to 0xFFF8 and 0x1571BABE to 0xFFFC, then lets the CPU
 *     out of reset.
 *   - Flash boot of the image at 0x220000 in Gideon's format ({dest, length,
 *     run} little endian, then the data), with the APPL partition as the
 *     length limit instead of his 0x180000, and only when the bitstream in
 *     flash carries the build stamp of the running one (T15 §4.2), so an
 *     SRAM-loaded test bitstream never starts the app that sits in flash.
 *   - The guard (T265 D3): a record in the sector below the recovery slot holds
 *     the length and CRC-32 of the application as the host wrote it. Without a
 *     record the application starts as before; with one, a length or CRC that
 *     differs starts the recovery program from its slot instead. A request
 *     over JTAG skips the guard.
 *   - Requests over JTAG at 0xFFF4/0xFFF0/0xFFEC (T15 §5): check the flash
 *     image, boot a flash-format image staged in DDR2, boot from flash without
 *     the stamp check. Results at 0xFFC0.
 *
 * The flash is only read: opcode 0x03 and the preamble's 0xFF.
 * Everything it prints goes to the console UART (and from there to the JTAG
 * console ring, `trxdbg.sh console`).
 */
#include <stdint.h>

/* T265 P1: the guard. 0 builds the same ROM without the check (the bench's
   negative control, host/sim/flash_boot): the application is started as before. */
#ifndef TRX_GUARD
#define TRX_GUARD 1
#endif

#define IO8(a)              (*(volatile uint8_t *)(a))
#define IO32(a)             (*(volatile uint32_t *)(a))
#define ITU_BASE            0x10000000
#define ITU_BUTTON_REG      IO8(ITU_BASE + 0x0A)
#define ITU_BUTTON2         0x80
#define CAPABILITIES(n)     IO8(ITU_BASE + 0x0C + (n))
#define CAPAB_FPGA_TYPE     0x30000000
#define CAPAB_BOOT_FPGA     0x40000000
#define UART_DATA           IO8(ITU_BASE + 0x10)
#define UART_FLAGS          IO8(ITU_BASE + 0x12)
#define UART_TX_FULL        0x10
#define SPI_FLASH_DATA      IO8(0x10060200)
#define SPI_FLASH_DATA_32   IO32(0x10060200)
#define SPI_FLASH_CTRL      IO8(0x10060208)
#define SPI_FORCE_SS        0x01
#define SPI_LEVEL_SS        0x02
#define U2PIO_BOARDREV      IO8(0x1010000C)
#define MIG_STATUS          IO8(0x10100100)
#define MIG_CALIBRATED      0x01
#define I2C_SCAN_ENABLE     IO8(0x10100708)
#define TRX_CAPS2           IO8(0x101A0005)
#define TRX_CAPS2_BUILD     0x02
#define TRX_BUILD(n)        IO8(0x101A0008 + (n))

#define DDR32(a)            (*(volatile uint32_t *)(a))
#define BOOT_MAGIC_LOCATION DDR32(0x0000FFFC)
#define BOOT_MAGIC_JUMPADDR DDR32(0x0000FFF8)
#define BOOT_MAGIC_VALUE    0x1571BABE
#define TRX_REQ_MAGIC       DDR32(0x0000FFF4)
#define TRX_REQ_MODE        DDR32(0x0000FFF0)
#define TRX_REQ_ADDR        DDR32(0x0000FFEC)
#define TRX_REQ_VALUE       0x7158B007
#define MODE_SRC_FLASH      1
#define MODE_SRC_DDR        2
#define MODE_CHECK          0x100

/* result block 0xFFC0 (T15 §5) */
#define RES(n)              DDR32(0x0000FFC0 + 4 * (n))
#define RES_VALID           0x54313552
enum { R_MAGIC, R_STATUS, R_SOURCE, R_DEST, R_LENGTH, R_RUN, R_SUM, R_XROT,
       R_BUILD, R_AXSS, R_FLAGS };
enum { ST_BOOTING = 1, ST_CHECKED, ST_EMPTY, ST_INVALID, ST_MISMATCH, ST_LOCKED,
       ST_BAD_REQUEST, ST_OVERLAP, ST_GUARD, ST_GUARD_STOP };
enum { DO_BOOT, DO_CHECK, DO_HEADER };

#define FLASH_APPL          0x220000            /* 0x3C0000 on a 100T (type 3) */
#define MAX_APPL_SIZE       (0x1E0000 - 12)     /* the APPL partition less the header */
/* behind the bitstream slot, below the application, at the same distances on a 100T */
#define RECOVERY_BELOW      0x40000             /* recovery slot = application - this */
#define RECORD_BELOW        0x1000              /* guard record sector = application - this */
#define MAX_REC_SIZE        (RECOVERY_BELOW - RECORD_BELOW - 12 - 4)   /* header and CRC trailer */
#define HOST_IFACE          1                   /* host-app interface of this bitstream (txu.h TXU_IFACE_PROVIDED) */
#define REC_MAGIC           0x47585254          /* "TRXG" */

/* guard block 0xFF80 (written here), recovery block 0xFF60 (written by the recovery program) */
#define GUARD(n)            DDR32(0x0000FF80 + 4 * (n))
#define GUARD_VALID         0x54323635
#define RECOV(n)            DDR32(0x0000FF60 + 4 * (n))
enum { G_MAGIC, G_VERDICT, G_REC_LEN, G_REC_CRC, G_LEN, G_CRC, G_IFACE, G_RECOVERY };
enum { V_NONE, V_MATCH, V_EMPTY, V_HEADER, V_CRC, V_NOT_ENFORCED };
enum { RV_NOT_TRIED, RV_STARTED, RV_EMPTY, RV_INVALID, RV_CRC };
#define DDR_SIZE            0x04000000          /* what the CPU sees */

/* bitstream packets (UG470 ch. 5) */
#define SYNC_WORD           0xAA995566
#define REG_CMD             0x04
#define REG_AXSS            0x0D
#define CMD_DESYNC          0x0D

static void outbyte(int c)
{
    while (UART_FLAGS & UART_TX_FULL)
        ;
    UART_DATA = c;
}

static void my_puts(const char *str)
{
    while (*str)
        outbyte(*(str++));
}

static const char hexchars[] = "0123456789ABCDEF";

static void hexbyte(uint8_t val)
{
    outbyte(hexchars[val >> 4]);
    outbyte(hexchars[val & 15]);
}

static void hexword(uint32_t val)
{
    hexbyte(val >> 24);
    hexbyte(val >> 16);
    hexbyte(val >> 8);
    hexbyte(val);
    outbyte(' ');
}

static void jump_run(uint32_t a)
{
    void (*function)(void) = (void (*)(void))a;
    function();
}

static uint32_t capabilities(void)
{
    return ((uint32_t)CAPABILITIES(0) << 24) | ((uint32_t)CAPABILITIES(1) << 16) |
           ((uint32_t)CAPABILITIES(2) << 8) | CAPABILITIES(3);
}

/* As test_access in the original: word, byte and halfword accesses to the
   DDR2 through the memory bridge. Expected for 0x12345678 at 0x10000:
   12345678 00000078 00000056 00000034 00000012 00005678 00001234 1234AA78 6969AA78 */
static void test_access(uint32_t addr, uint32_t pattern)
{
    volatile uint32_t *dw = (volatile uint32_t *)addr;
    volatile uint16_t *w  = (volatile uint16_t *)addr;
    volatile uint8_t  *b  = (volatile uint8_t *)addr;

    *dw = pattern;
    hexword(*dw);
    hexword(b[0]);
    hexword(b[1]);
    hexword(b[2]);
    hexword(b[3]);
    hexword(w[0]);
    hexword(w[1]);
    b[1] = 0xAA;
    hexword(*dw);
    w[1] = 0x6969;
    hexword(*dw);
    my_puts(".\n");
}

/* ---- flash, read only ---------------------------------------------------- */

static uint32_t flash_pos;          /* flash address of the next byte */

static void flash_stop(void)
{
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;   /* CSn high */
}

static void flash_read_at(uint32_t addr)
{
    flash_stop();
    SPI_FLASH_CTRL = SPI_FORCE_SS;                  /* CSn low */
    SPI_FLASH_DATA = 0x03;                          /* read, 3-byte address */
    SPI_FLASH_DATA = (uint8_t)(addr >> 16);
    SPI_FLASH_DATA = (uint8_t)(addr >> 8);
    SPI_FLASH_DATA = (uint8_t)addr;
    flash_pos = addr;
}

static uint32_t flash_word_be(void)
{
    uint32_t w = 0;
    for (int i = 0; i < 4; i++)
        w = (w << 8) | SPI_FLASH_DATA;
    flash_pos += 4;
    return w;
}

/* The AXSS (USR_ACCESS) value the bitstream in flash writes, if any: find the
   sync word in the first 4 KB, walk the packets, skip long data by a new read
   behind it. 1 when found. */
static int flash_axss(uint32_t *val)
{
    uint32_t sh = 0;
    int found = 0;

    flash_read_at(0);
    while (sh != SYNC_WORD) {
        if (flash_pos >= 4096)
            goto out;
        sh = (sh << 8) | SPI_FLASH_DATA;
        flash_pos++;
    }
    /* a compressed 50T bitstream has some 9000 packets (MFWR) before DESYNC */
    for (int32_t n = 0; n < 65536 && flash_pos < FLASH_APPL; n++) {
        uint32_t w = flash_word_be();
        uint32_t count;
        if ((w >> 29) == 1) {
            uint32_t op  = (w >> 27) & 3;
            uint32_t reg = (w >> 13) & 0x3FFF;
            count = w & 0x7FF;
            if (op == 2 && count == 1) {
                uint32_t v = flash_word_be();
                if (reg == REG_AXSS) {
                    *val = v;
                    found = 1;
                    break;
                }
                if (reg == REG_CMD && v == CMD_DESYNC)
                    break;
                continue;
            }
        } else if ((w >> 29) == 2) {
            count = w & 0x07FFFFFF;
        } else {
            break;
        }
        if (count >= (FLASH_APPL >> 2))
            break;
        if (count < 8) {
            while (count--)
                flash_word_be();
        } else {
            flash_read_at(flash_pos + 4 * count);
        }
    }
out:
    flash_stop();
    return found;
}

/* ---- loading ------------------------------------------------------------- */

static int      src_flash;
static uint32_t src_addr;           /* DDR2 source: address of the next word */

static uint32_t src_word(void)
{
    uint32_t w;
    if (src_flash)
        return SPI_FLASH_DATA_32;
    w = DDR32(src_addr);
    src_addr += 4;
    return w;
}

static uint32_t max_len = MAX_APPL_SIZE;    /* header limit; the recovery slot is smaller */
static uint32_t appl_addr;                  /* the application's flash address: its CRC goes to the guard block */
static uint32_t crc;                        /* CRC-32 of the words read by load(), as flashfw.cc and zlib */
static uint32_t crc_tab[256];
static int      crc_expect_on;              /* compare the CRC with crc_expect before jumping */
static uint32_t crc_expect;
static int      want_trailer;               /* a CRC word follows the data (the recovery image) */

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : c >> 1;
        crc_tab[i] = c;
    }
}

/* the reflected CRC of a little endian word is the byte CRC of its 4 bytes in flash order */
static void crc_word(uint32_t w)
{
    crc ^= w;
    for (int k = 0; k < 4; k++)
        crc = crc_tab[crc & 0xFF] ^ (crc >> 8);
}

static int header_ok(uint32_t dest, uint32_t length, uint32_t run)
{
    if (length == 0 || length > max_len)
        return 0;
    if ((dest & 3) || dest < 0x10000 || dest > DDR_SIZE - length)
        return 0;
    return run >= dest && run - dest < length;
}

/* The image at addr in flash or DDR2: header, checks, then (DO_BOOT) copy
   the data to dest or (DO_CHECK) only read it, with the sums on the way.
   Returns the status; the caller jumps. */
static int load(int source, uint32_t addr, int what, uint32_t *run_out)
{
    uint32_t dest, length, run, sum = 0, x = 0;
    int status;

    crc = 0xFFFFFFFF;

    src_flash = (source == MODE_SRC_FLASH);
    RES(R_SOURCE) = source;
    RES(R_SUM) = 0;
    RES(R_XROT) = 0;
    my_puts(src_flash ? "Flash " : "DDR2 ");
    hexword(addr);
    if (src_flash) {
        flash_read_at(addr);
    } else {
        if ((addr & 3) || addr > DDR_SIZE - 12) {
            my_puts("bad address\n");
            return ST_BAD_REQUEST;
        }
        src_addr = addr;
    }
    dest   = src_word();
    length = src_word();
    run    = src_word();
    crc_word(dest);
    crc_word(length);
    crc_word(run);
    RES(R_DEST) = dest;
    RES(R_LENGTH) = length;
    RES(R_RUN) = run;
    my_puts("app: ");
    hexword(dest);
    hexword(length);
    hexword(run);
    if (length == 0xFFFFFFFF) {
        status = ST_EMPTY;
        my_puts("empty\n");
    } else if (!header_ok(dest, length, run)) {
        status = ST_INVALID;
        my_puts("invalid\n");
    } else if (!src_flash && addr > DDR_SIZE - 12 - length) {
        status = ST_INVALID;
        my_puts("beyond the DDR2\n");
    } else if (!src_flash && addr < dest + length && dest < addr + 12 + length) {
        status = ST_OVERLAP;
        my_puts("overlaps its destination\n");
    } else if (what == DO_HEADER) {
        status = ST_CHECKED;
        my_puts("valid\n");
    } else {
        volatile uint32_t *d = (volatile uint32_t *)dest;
        my_puts(what == DO_CHECK ? "checking\n" : "loading\n");
        for (int32_t left = (int32_t)length; left > 0; left -= 4) {
            uint32_t w = src_word();
            if (what == DO_BOOT)
                *(d++) = w;
            sum += w;
            x = ((x << 1) | (x >> 31)) ^ w;
            crc_word(w);
        }
        crc = ~crc;
        if (src_flash && addr == appl_addr) {
            GUARD(G_LEN) = 12 + ((length + 3) & ~(uint32_t)3);  /* with no record too: the host app provisions from both */
            GUARD(G_CRC) = crc;
        }
        RES(R_SUM) = sum;
        RES(R_XROT) = x;
        my_puts("sum ");
        hexword(sum);
        my_puts("xrot ");
        hexword(x);
        outbyte('\n');
        status = (what == DO_CHECK) ? ST_CHECKED : ST_BOOTING;
        if (what == DO_BOOT && crc_expect_on && crc != crc_expect) {
            status = ST_GUARD;
            my_puts("CRC differs\n");
        } else if (want_trailer && src_word() != crc) {
            status = ST_GUARD_STOP;
            my_puts("bad CRC trailer\n");
        }
    }
    if (src_flash)
        SPI_FLASH_CTRL = 0;     /* as the original: CS back to idle */
    *run_out = run;
    return status;
}

static void boot(int source, uint32_t addr, int what)
{
    uint32_t run;
    int status = load(source, addr, what, &run);
    if (status == ST_BOOTING) {
        if (ITU_BUTTON_REG & ITU_BUTTON2) {   /* right button: as the original */
            RES(R_STATUS) = ST_LOCKED;
            my_puts("Lock\n");
            return;
        }
        if (!want_trailer)      /* the recovery program leaves ST_GUARD for the application */
            RES(R_STATUS) = ST_BOOTING;
        my_puts("Running\n");
        jump_run(run);
    }
    RES(R_STATUS) = status;
}

#if TRX_GUARD
/* The record below the application: valid when magic and check word agree. */
static int record_read(uint32_t appl, uint32_t *len, uint32_t *rcrc)
{
    uint32_t m, l, c, k;
    flash_read_at(appl - RECORD_BELOW);
    src_flash = 1;
    m = src_word();
    l = src_word();
    c = src_word();
    k = src_word();
    SPI_FLASH_CTRL = 0;
    *len = l;
    *rcrc = c;
    return m == REC_MAGIC && k == ~(m ^ l ^ c);
}

/* Before the application is loaded: 1 = load it (the CRC check is armed when
   there is a record), 0 = it is not the host's own (status ST_GUARD). The
   length is compared first, so a foreign application of another length is
   never read beyond its header. */
static int guard_decide(uint32_t appl)
{
    uint32_t rlen, rcrc, run, flen, status;

    GUARD(G_MAGIC) = GUARD_VALID;
    GUARD(G_IFACE) = HOST_IFACE;
    if (!record_read(appl, &rlen, &rcrc)) {
        GUARD(G_VERDICT) = V_NONE;
        my_puts("Guard: no record\n");
        return 1;
    }
    GUARD(G_REC_LEN) = rlen;
    GUARD(G_REC_CRC) = rcrc;
    status = load(MODE_SRC_FLASH, appl, DO_HEADER, &run);
    flen = (status == ST_CHECKED) ? 12 + ((RES(R_LENGTH) + 3) & ~(uint32_t)3) : 0;
    GUARD(G_LEN) = flen;
    if (status == ST_CHECKED && flen == rlen) {
        GUARD(G_VERDICT) = V_MATCH;
        crc_expect_on = 1;
        crc_expect = rcrc;
        return 1;
    }
    GUARD(G_VERDICT) = (status == ST_EMPTY) ? V_EMPTY : V_HEADER;
    RES(R_STATUS) = ST_GUARD;
    return 0;
}

/* A JTAG request: no guard, only the record next to the flash, for `trxdbg boot check`. */
static void guard_info(uint32_t appl)
{
    uint32_t len, rcrc;

    GUARD(G_MAGIC) = GUARD_VALID;
    GUARD(G_IFACE) = HOST_IFACE;
    GUARD(G_VERDICT) = V_NOT_ENFORCED;
    if (record_read(appl, &len, &rcrc)) {
        GUARD(G_REC_LEN) = len;
        GUARD(G_REC_CRC) = rcrc;
    }
}

/* The flash application is not the host's own: start the recovery program. */
static void guard_recover(uint32_t appl)
{
    int st;

    if (GUARD(G_VERDICT) == V_MATCH)        /* length equal, so the CRC load found the difference */
        GUARD(G_VERDICT) = V_CRC;
    my_puts("Guard: the flash application is not the host's own, verdict ");
    hexword(GUARD(G_VERDICT));
    outbyte('\n');
    crc_expect_on = 0;
    want_trailer = 1;
    max_len = MAX_REC_SIZE;
    RES(R_STATUS) = ST_GUARD;
    GUARD(G_RECOVERY) = RV_STARTED;
    boot(MODE_SRC_FLASH, appl - RECOVERY_BELOW, DO_BOOT);
    /* only a refused recovery returns */
    st = RES(R_STATUS);
    if (st == ST_LOCKED)
        return;
    GUARD(G_RECOVERY) = (st == ST_EMPTY) ? RV_EMPTY : (st == ST_GUARD_STOP) ? RV_CRC : RV_INVALID;
    RES(R_STATUS) = ST_GUARD_STOP;
    my_puts("No usable recovery program.\n");
}
#endif

int main(void)
{
    uint32_t caps, build = 0, axss = 0, appl;
    int has_build, found;

    my_puts("\nTRX64 boot ROM (rvlite)\n");

    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; /* CSn high */
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS;                /* CSn low */
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; /* CSn high */

    caps = capabilities();
    hexword(caps);
    hexbyte(U2PIO_BOARDREV);
    outbyte('\n');

    I2C_SCAN_ENABLE = 0;

    my_puts("DDR2: ");
    while (!(MIG_STATUS & MIG_CALIBRATED))
        ;
    my_puts("calibrated\n");
    /* the blocks of an earlier boot stay in DDR2 over a JTAG magic load: no stale result */
    RES(R_MAGIC) = 0;
    for (int i = 0; i < 8; i++)
        GUARD(i) = 0;
    RECOV(0) = 0;
    crc_init();
    test_access(0x10000, 0x12345678);

    if (BOOT_MAGIC_LOCATION == BOOT_MAGIC_VALUE) {
        uint32_t a = BOOT_MAGIC_JUMPADDR;
        BOOT_MAGIC_LOCATION = 0;
        my_puts("Magic: ");
        hexword(a);
        my_puts("\n");
        jump_run(a);
    }

    appl = ((caps & CAPAB_FPGA_TYPE) == CAPAB_FPGA_TYPE) ? 0x3C0000 : FLASH_APPL;
    has_build = (TRX_CAPS2 & TRX_CAPS2_BUILD) != 0;
    if (has_build)
        build = TRX_BUILD(0) | ((uint32_t)TRX_BUILD(1) << 8) |
                ((uint32_t)TRX_BUILD(2) << 16) | ((uint32_t)TRX_BUILD(3) << 24);
    appl_addr = appl;
    found = flash_axss(&axss);
    RES(R_STATUS) = 0;
    RES(R_SOURCE) = 0;
    RES(R_BUILD) = build;
    RES(R_AXSS) = axss;
    RES(R_FLAGS) = (found ? 1 : 0) | (has_build ? 2 : 0);
    RES(R_MAGIC) = RES_VALID;
    my_puts("Build ");
    hexword(build);
    my_puts("flash bitstream ");
    if (found)
        hexword(axss);
    else
        my_puts("(no stamp) ");
    outbyte('\n');

    if (TRX_REQ_MAGIC == TRX_REQ_VALUE) {
        uint32_t mode = TRX_REQ_MODE;
        uint32_t src = mode & 3;
        TRX_REQ_MAGIC = 0;
        my_puts("Request ");
        hexword(mode);
        outbyte('\n');
        if ((mode & ~(uint32_t)(MODE_CHECK | 3)) == 0 &&
            (src == MODE_SRC_FLASH || src == MODE_SRC_DDR)) {
#if TRX_GUARD
            if (src == MODE_SRC_FLASH)
                guard_info(appl);
#endif
            boot(src, src == MODE_SRC_FLASH ? appl : TRX_REQ_ADDR,
                 (mode & MODE_CHECK) ? DO_CHECK : DO_BOOT);
        } else {
            RES(R_STATUS) = ST_BAD_REQUEST;
            my_puts("bad request\n");
        }
    } else if (!(caps & CAPAB_BOOT_FPGA)) {
        if (has_build && found && axss == build) {
#if TRX_GUARD
            if (guard_decide(appl))
                boot(MODE_SRC_FLASH, appl, DO_BOOT);
            if (RES(R_STATUS) == ST_GUARD)
                guard_recover(appl);
#else
            boot(MODE_SRC_FLASH, appl, DO_BOOT);
#endif
        } else {
            uint32_t run;
            my_puts("Not this bitstream's flash: no flash boot.\n");
            load(MODE_SRC_FLASH, appl, DO_HEADER, &run);    /* the header, for the console */
            RES(R_STATUS) = ST_MISMATCH;
        }
    }

    my_puts("Waiting for JTAG image.\n");
    while (1) {
        if (BOOT_MAGIC_LOCATION == BOOT_MAGIC_VALUE) {
            uint32_t addr = BOOT_MAGIC_JUMPADDR;
            BOOT_MAGIC_LOCATION = 0;
            my_puts("Magic: ");
            hexword(addr);
            my_puts("\n");
            jump_run(addr);
        }
    }
    return 0;
}
