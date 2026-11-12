// Host test of the SD data CRC16 check (host-app/software/io/sd_card, T139): sd_card.cc and sdio.cc are built unchanged against a fake SPI card.
// The card flips a byte on the wire after it computed the CRC (a bit error on the bus): the read must notice, read the block again and return the
// right data. Built with -DSDIO_NO_CRC_CHECK (the control) the corrupted data passes and the checks below have to fail.
#include "sdio_fake.h"
#include "sd_card.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <deque>
#include <string>

FakeData8 f_data; FakeData32 f_data32; FakeReg8 f_speed, f_ctrl, f_crc, f_switch;

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// reference CRC16-CCITT, bit by bit (independent of the table in sdio.cc)
static uint16_t ref_crc(const uint8_t *p, int n)
{
    uint16_t c = 0;
    for (int i = 0; i < n; i++) { c ^= (uint16_t)p[i] << 8; for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); }
    return c;
}
static void sector_data(uint32_t s, uint8_t *d) { for (int i = 0; i < 512; i++) d[i] = (uint8_t)(s * 37 + i * 7 + (i >> 8)); }
static const uint8_t CSD[16] = { 0x40, 0x0E, 0x00, 0x32, 0x5B, 0x59, 0x00, 0x00, 0x12, 0x34, 0x7F, 0x80, 0x0A, 0x40, 0x40, 0x01 };
static const uint8_t CID[16] = { 0x03, 0x53, 0x44, 0x53, 0x4C, 0x31, 0x36, 0x47, 0x80, 0x12, 0x34, 0x56, 0x78, 0x01, 0x23, 0x00 };

// ---- the fake card ----
static std::deque<uint8_t> out;
static uint8_t cmd[6]; static int cmd_n;
static int corrupt_left;       // how many transmissions of corrupt_sector are damaged
static uint32_t corrupt_sector = 0xFFFFFFFF;
static int corrupt_mode;       // 0 = a data byte flips, 1 = a CRC byte flips
static int corrupt_csd, corrupt_cid;
static int reads_of[64];       // CMD17 per sector (SDHC addressing, sector < 64)
static int n_cmd9, n_cmd10;

static void push_block(const uint8_t *d, int n, bool damage, int mode)
{
    uint16_t crc = ref_crc(d, n);
    out.push_back(0xFF); out.push_back(0x00); out.push_back(0xFF); out.push_back(0xFF); out.push_back(0xFE);
    for (int i = 0; i < n; i++) out.push_back((damage && mode == 0 && i == (n > 16 ? 100 : 3)) ? (uint8_t)(d[i] ^ 0x80) : d[i]);
    out.push_back((uint8_t)(crc >> 8) ^ ((damage && mode == 1) ? 0x01 : 0)); out.push_back((uint8_t)crc);
}

uint8_t fake_spi_xfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    if (!out.empty()) { rx = out.front(); out.pop_front(); }
    if (cmd_n == 0 && (tx & 0xC0) != 0x40) return rx;
    cmd[cmd_n++] = tx;
    if (cmd_n < 6) return rx;
    cmd_n = 0;
    uint32_t arg = ((uint32_t)cmd[1] << 24) | ((uint32_t)cmd[2] << 16) | ((uint32_t)cmd[3] << 8) | cmd[4];
    out.clear();
    switch (cmd[0] & 0x3F) {
    case 17: {
        uint8_t d[512]; sector_data(arg >> 9, d);          // sdhc is false in the class: byte addressing
        uint32_t s = arg >> 9;
        if (s < 64) reads_of[s]++;
        bool dmg = (s == corrupt_sector && corrupt_left > 0);
        if (dmg) corrupt_left--;
        push_block(d, 512, dmg, corrupt_mode);
        break; }
    case 9:  { n_cmd9++;  bool dmg = corrupt_csd > 0; if (dmg) corrupt_csd--; push_block(CSD, 16, dmg, 0); break; }
    case 10: { n_cmd10++; bool dmg = corrupt_cid > 0; if (dmg) corrupt_cid--; push_block(CID, 16, dmg, 0); break; }
    default: out.push_back(0xFF); out.push_back(0x04); break;
    }
    return rx;
}

// capture what the driver prints
static std::string slurp_log(int fd_saved, const char *path)
{
    fflush(stdout); dup2(fd_saved, 1);
    std::string s; FILE *f = fopen(path, "r"); char b[256];
    while (f && fgets(b, sizeof b, f)) s += b;
    if (f) fclose(f);
    return s;
}
static int start_log(const char *path) { fflush(stdout); int saved = dup(1); int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600); dup2(fd, 1); close(fd); return saved; }

int main(int argc, char **argv)
{
    const char *logp = argc > 1 ? argv[1] : "/tmp/test_sdcrc.log";
    SdCard sd;
    alignas(4) static uint8_t buf[3 * 512 + 8];
    uint8_t want[512];
    CHECK(sdio_crc16((const uint8_t *)"123456789", 9) == 0x31C3);           // the CRC16-CCITT (XModem) check value

    // 1. clean read, three sectors, aligned (32-bit path) and unaligned (byte path)
    CHECK(sd.read(buf, 5, 3) == RES_OK);
    for (int s = 0; s < 3; s++) { sector_data(5 + s, want); CHECK(!memcmp(buf + 512 * s, want, 512)); }
    CHECK(reads_of[5] == 1 && reads_of[6] == 1 && reads_of[7] == 1 && sdio_crc_errors == 0);
    memset(buf, 0, sizeof buf);
    CHECK(sd.read(buf + 1, 9, 1) == RES_OK);
    sector_data(9, want); CHECK(!memcmp(buf + 1, want, 512));

    // 2. one flipped data byte in sector 6: read again, right data, the log names the block and the try
    corrupt_sector = 6; corrupt_left = 1; corrupt_mode = 0; memset(buf, 0, sizeof buf);
    int sv = start_log(logp);
    DRESULT r = sd.read(buf, 5, 3);
    std::string log = slurp_log(sv, logp);
    CHECK(r == RES_OK);
    for (int s = 0; s < 3; s++) { sector_data(5 + s, want); CHECK(!memcmp(buf + 512 * s, want, 512)); }
    CHECK(reads_of[6] == 3);                                  // 1 (test 1) + the damaged try + the retry
    CHECK(reads_of[5] == 2 && reads_of[7] == 2);              // the other sectors are not read again
    CHECK(sdio_crc_errors == 1 && sdio_crc_giveups == 0);
    CHECK(log.find("[sd] block 6: data CRC error, read again (try 1)") != std::string::npos);

    // 3. a flipped CRC byte on the wire counts as well
    corrupt_sector = 8; corrupt_left = 1; corrupt_mode = 1; memset(buf, 0, sizeof buf);
    CHECK(sd.read(buf, 8, 1) == RES_OK);
    sector_data(8, want); CHECK(!memcmp(buf, want, 512));
    CHECK(reads_of[8] == 2 && sdio_crc_errors == 2);

    // 4. a sector that stays damaged: three more tries, then an error, and the data is not reported as read
    corrupt_sector = 10; corrupt_left = 100; corrupt_mode = 0;
    sv = start_log(logp);
    r = sd.read(buf, 10, 1);
    log = slurp_log(sv, logp);
    CHECK(r == RES_ERROR && reads_of[10] == 1 + SD_READ_RETRIES);
    CHECK(sdio_crc_giveups == 1 && sdio_crc_errors == 2 + 1 + SD_READ_RETRIES);
    CHECK(log.find("block 10: data CRC error, read again (try 3)") != std::string::npos && log.find("giving up") != std::string::npos);
    corrupt_left = 0;

    // 5. CID and CSD: damaged once each, read again; the size comes out of the second CSD
    corrupt_csd = 1; corrupt_cid = 1;
    uint32_t sectors = 0;
    sv = start_log(logp);
    r = sd.ioctl(GET_SECTOR_COUNT, &sectors);
    log = slurp_log(sv, logp);
    CHECK(r == RES_OK && sectors == (0x1234u + 1) << 10);
    CHECK(n_cmd9 == 2 && n_cmd10 == 2);
    CHECK(log.find("[sd] CSD: data CRC error, read again (try 1)") != std::string::npos && log.find("[sd] CID: data CRC error, read again (try 1)") != std::string::npos);

    // 6. CSD damaged for good: an error, drive size 0
    corrupt_csd = 100; sectors = 77;
    r = sd.ioctl(GET_SECTOR_COUNT, &sectors);
    CHECK(r == RES_ERROR && sectors == 0);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
