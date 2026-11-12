#include "espflash.h"
#include <string.h>

#define OP_FLASH_BEGIN       0x02
#define OP_FLASH_DATA        0x03
#define OP_SYNC              0x08
#define OP_SET_FLASH_PARAMS  0x0B
#define OP_ATTACH_SPI        0x0D
#define OP_CHANGE_BAUDRATE   0x0F

static uint8_t req_buf[8 + 16 + ESPF_BLOCK];
static uint8_t rx_buf[64];

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// One request and its answer: the answers of other commands are skipped (as the vendor updater does) until the timeout. Ok = status 0.
// timeouts in milliseconds (the vendor updater counts 5 ms ticks: 200 ticks = 1 s)
static bool command(EspFlashLink &l, uint8_t op, uint16_t len, uint8_t chk, const uint8_t *data, int timeout_ms)
{
    memset(req_buf, 0, 8);
    req_buf[1] = op;
    req_buf[2] = (uint8_t)len;
    req_buf[3] = (uint8_t)(len >> 8);
    req_buf[4] = chk;
    memcpy(req_buf + 8, data, len);
    l.send_packet(req_buf, 8u + len);
    for (;;) {
        int n = l.recv_packet(rx_buf, sizeof(rx_buf), timeout_ms);
        if (n <= 0) {
            return false;
        }
        if (n >= 10 && rx_buf[0] == 1 && rx_buf[1] == op) {
            return (rx_buf[2] == 2 || rx_buf[2] == 4) && rx_buf[8] == 0;
        }
    }
}

bool esp_version_parse(const char *v, uint16_t *major, uint16_t *minor)
{
    if (!v || v[0] != 'V') {
        return false;
    }
    uint32_t a = 0, b = 0;
    const char *p = v + 1;
    int digits = 0;
    for (; *p >= '0' && *p <= '9'; p++, digits++) {
        a = a * 10 + (uint32_t)(*p - '0');
    }
    if (!digits || *p != '.') {
        return false;
    }
    digits = 0;
    for (p++; *p >= '0' && *p <= '9'; p++, digits++) {
        b = b * 10 + (uint32_t)(*p - '0');
    }
    if (!digits || *p || a > 0xFFFF || b > 0xFFFF) {
        return false;
    }
    *major = (uint16_t)a;
    *minor = (uint16_t)b;
    return true;
}

static bool sync_loader(EspFlashLink &l)
{
    uint8_t f[44];
    memset(f, 0x55, sizeof(f));
    memset(f, 0, 8);
    f[1] = OP_SYNC; f[2] = 0x24;
    f[8] = 0x07; f[9] = 0x07; f[10] = 0x12; f[11] = 0x20;
    for (int i = 0; i < 10; i++) {
        l.send_packet(f, sizeof(f));
        int n = l.recv_packet(rx_buf, sizeof(rx_buf), 1000);
        // the answer: 01 08 04 00, the value field (any), 00 00 00 00
        if (n == 12 && rx_buf[0] == 1 && rx_buf[1] == OP_SYNC && rx_buf[2] == 4 && rx_buf[3] == 0 && !rx_buf[8] && !rx_buf[9] && !rx_buf[10] && !rx_buf[11]) {
            while (l.recv_packet(rx_buf, sizeof(rx_buf), 100) > 0) {
            }                                                                    // the loader answers a sync more than once
            return true;
        }
    }
    return false;
}

struct Job {
    EspFlashLink &l;
    CorSource &src;
    EspFlashProgress cb;
    void *ctx;
    EspFlashResult *r;
    uint32_t done, total;
};

static bool fail(Job &j, int stage, const char *why)
{
    j.r->stage = stage;
    j.r->why = why;
    return false;
}

static bool write_image(Job &j, int stage, uint32_t file_off, uint32_t len, uint32_t addr)
{
    EspFlashLink &l = j.l;
    const uint32_t blocks = (len + ESPF_BLOCK - 1) / ESPF_BLOCK;
    uint8_t p[20];
    put32(p, blocks * ESPF_BLOCK);
    put32(p + 4, blocks);
    put32(p + 8, ESPF_BLOCK);
    put32(p + 12, addr);
    put32(p + 16, 0);
    j.r->started = true;                                                         // FLASH_BEGIN erases the range: from here on the ESP32's flash is changed
    if (!command(l, OP_FLASH_BEGIN, 20, 0, p, 15000)) {
        return fail(j, stage, "the ESP32 did not accept the start of an image (FLASH_BEGIN)");
    }
    static uint8_t blk[16 + ESPF_BLOCK];
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t now = len - i * ESPF_BLOCK > ESPF_BLOCK ? ESPF_BLOCK : len - i * ESPF_BLOCK;
        memset(blk + 16, 0xFF, ESPF_BLOCK);
        if (!j.src.read(file_off + i * ESPF_BLOCK, blk + 16, now)) {
            return fail(j, stage, "the file could not be read");
        }
        uint8_t chk = 0xEF;
        for (uint32_t m = 0; m < ESPF_BLOCK; m++) {
            chk ^= blk[16 + m];
        }
        put32(blk, ESPF_BLOCK);
        put32(blk + 4, i);
        put32(blk + 8, 0);
        put32(blk + 12, 0);
        if (!command(l, OP_FLASH_DATA, 16 + ESPF_BLOCK, chk, blk, 5000)) {
            return fail(j, stage, "the ESP32 did not accept a block of data (FLASH_DATA)");
        }
        j.done++;
        if (j.cb) {
            j.cb(j.ctx, stage, j.done, j.total);
        }
    }
    return true;
}

static bool run_set(Job &j, const EspInfo &info)
{
    EspFlashLink &l = j.l;
    bool entered = false;
    for (int tries = 0; tries < 3 && !entered; tries++) {
        entered = l.enter_loader();
    }
    if (!entered) {
        return fail(j, EFS_ENTER, "the ESP32 did not enter its ROM loader (no DOWNLOAD message)");
    }
    if (j.cb) {
        j.cb(j.ctx, EFS_SYNC, 0, j.total);
    }
    if (!sync_loader(l)) {
        return fail(j, EFS_SYNC, "the ESP32 did not answer the sync");
    }
    if (j.cb) {
        j.cb(j.ctx, EFS_SETUP, 0, j.total);
    }
    uint8_t p[24];
    put32(p, 0); put32(p + 4, 0);                                                  // the default SPI flash pins
    if (!command(l, OP_ATTACH_SPI, 8, 0, p, 2500)) {
        return fail(j, EFS_SETUP, "the ESP32 did not attach its flash (ATTACH_SPI)");
    }
    put32(p, ESPF_BAUD); put32(p + 4, 0);
    if (!command(l, OP_CHANGE_BAUDRATE, 8, 0, p, 1000)) {
        return fail(j, EFS_SETUP, "the ESP32 did not take the faster baud rate (CHANGE_BAUDRATE)");
    }
    l.set_baud(ESPF_BAUD);
    put32(p, 0); put32(p + 4, 0x200000); put32(p + 8, 0x10000); put32(p + 12, 0x1000); put32(p + 16, 0x100); put32(p + 20, 0xFFFF);
    if (!command(l, OP_SET_FLASH_PARAMS, 24, 0, p, 1000)) {
        return fail(j, EFS_SETUP, "the ESP32 did not take the flash parameters (SET_FLASH_PARAMS)");
    }
    return write_image(j, EFS_BOOT, info.boot_off, info.boot_len, ESPF_ADDR_BOOT) &&
           write_image(j, EFS_PTABLE, info.ptable_off, info.ptable_len, ESPF_ADDR_PTABLE) &&
           write_image(j, EFS_APP, info.app_off, info.app_len, ESPF_ADDR_APP);
}

bool esp_flash(EspFlashLink &link, CorSource &src, const EspInfo &info, EspFlashProgress cb, void *ctx, EspFlashResult *res)
{
    memset(res, 0, sizeof(*res));
    res->why = "";
    const uint32_t size = src.size();
    if (!info.boot || !info.ptable || !info.app || !info.digest_ok || !info.boot_len || !info.ptable_len || !info.app_len ||
        (uint64_t)info.boot_off + info.boot_len > size || (uint64_t)info.ptable_off + info.ptable_len > size || (uint64_t)info.app_off + info.app_len > size) {
        res->why = "the file does not hold a whole, intact ESP32 image set";           // before anything is armed or sent
        return false;
    }
    uint32_t blocks = 0;
    blocks += (info.boot_len + ESPF_BLOCK - 1) / ESPF_BLOCK;
    blocks += (info.ptable_len + ESPF_BLOCK - 1) / ESPF_BLOCK;
    blocks += (info.app_len + ESPF_BLOCK - 1) / ESPF_BLOCK;
    Job j = { link, src, cb, ctx, res, 0, blocks };
    link.arm(true);
    bool ok = run_set(j, info);
    link.leave_loader();
    link.arm(false);
    if (!ok) {
        return false;
    }
    res->stage = EFS_RUN;
    uint16_t want_major, want_minor;
    if (!esp_version_parse(info.version, &want_major, &want_minor)) {
        res->ident = EFI_NOT_ASKED;                                                 // the file's application carries no version to compare with
        res->ok = true;
        return true;
    }
    if (cb) {
        cb(ctx, EFS_RUN, blocks, blocks);
    }
    if (!link.identify(&res->major, &res->minor)) {
        res->ident = EFI_NO_ANSWER;
        res->ok = true;
        res->why = "written, but the ESP32 did not report its version yet";
        return true;
    }
    if (res->major != want_major || res->minor != want_minor) {
        res->ident = EFI_DIFFERS;
        res->why = "after the write the ESP32 reports another version than the file's";
        return false;
    }
    res->ident = EFI_MATCH;
    res->ok = true;
    return true;
}
