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
 *   - Requests over JTAG at 0xFFF4/0xFFF0/0xFFEC (T15 §5): check the flash
 *     image, boot a flash-format image staged in DDR2, boot from flash without
 *     the stamp check. Results at 0xFFC0.
 *
 * The flash is only read: opcode 0x03 and the preamble's 0xFF.
 * Everything it prints goes to the console UART (and from there to the JTAG
 * console ring, `trxdbg.sh console`).
 */
#include <stdint.h>

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
       ST_BAD_REQUEST, ST_OVERLAP };
enum { DO_BOOT, DO_CHECK, DO_HEADER };

#define FLASH_APPL          0x220000            /* 0x3C0000 on a 100T (type 3) */
#define MAX_APPL_SIZE       (0x1E0000 - 12)     /* the APPL partition less the header */
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

static int header_ok(uint32_t dest, uint32_t length, uint32_t run)
{
    if (length == 0 || length > MAX_APPL_SIZE)
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
        }
        RES(R_SUM) = sum;
        RES(R_XROT) = x;
        my_puts("sum ");
        hexword(sum);
        my_puts("xrot ");
        hexword(x);
        outbyte('\n');
        status = (what == DO_CHECK) ? ST_CHECKED : ST_BOOTING;
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
        RES(R_STATUS) = ST_BOOTING;
        my_puts("Running\n");
        jump_run(run);
    }
    RES(R_STATUS) = status;
}

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
            boot(src, src == MODE_SRC_FLASH ? appl : TRX_REQ_ADDR,
                 (mode & MODE_CHECK) ? DO_CHECK : DO_BOOT);
        } else {
            RES(R_STATUS) = ST_BAD_REQUEST;
            my_puts("bad request\n");
        }
    } else if (!(caps & CAPAB_BOOT_FPGA)) {
        if (has_build && found && axss == build) {
            boot(MODE_SRC_FLASH, appl, DO_BOOT);
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
