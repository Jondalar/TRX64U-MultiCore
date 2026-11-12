#include "flashlock.h"
#include <string.h>

#define CMD_WRITE_ENABLE_VOLATILE 0x50u
#define CMD_WRITE_SR1             0x01u
#define CMD_WRITE_SR2             0x31u
#define CMD_READ_SR1              0x05u
#define CMD_READ_SR2              0x35u
#define CMD_JEDEC                 0x9Fu

uint32_t flashlock_jedec(FlashSpi &spi)
{
    spi.begin();
    spi.send(CMD_JEDEC);
    uint32_t id = (uint32_t)spi.recv() << 16;
    id |= (uint32_t)spi.recv() << 8;
    id |= spi.recv();
    spi.end();
    return id;
}

uint8_t flashlock_read_sr1(FlashSpi &spi)
{
    spi.begin();
    spi.send(CMD_READ_SR1);
    uint8_t v = spi.recv();
    spi.end();
    return v;
}

uint8_t flashlock_read_sr2(FlashSpi &spi)
{
    spi.begin();
    spi.send(CMD_READ_SR2);
    uint8_t v = spi.recv();
    spi.end();
    return v;
}

// The only status register 1 write of this file. SRP (bit 7) is cleared in the byte that goes out, whatever the caller passed: SRP = 1 with SRL = 1
// is the one-time program lock (flashlock.h).
static void fl_write_sr1(FlashSpi &spi, uint8_t v)
{
    v &= (uint8_t)~FL_SR1_SRP;
    spi.begin();
    spi.send(CMD_WRITE_ENABLE_VOLATILE);
    spi.end();
    spi.begin();
    spi.send(CMD_WRITE_SR1);
    spi.send(v);
    spi.end();
    spi.wait_idle(50);
}

static void fl_write_sr2(FlashSpi &spi, uint8_t v)
{
    spi.begin();
    spi.send(CMD_WRITE_ENABLE_VOLATILE);
    spi.end();
    spi.begin();
    spi.send(CMD_WRITE_SR2);
    spi.send(v);
    spi.end();
    spi.wait_idle(50);
}

bool flashlock_is_set(uint8_t sr1, uint8_t sr2)
{
    return (sr1 & (FL_SR1_SRP | FL_SR1_SEC | FL_SR1_TB | FL_SR1_BP)) == FL_SR1_WANT && (sr2 & FL_SR2_CMP) == 0 && (sr2 & FL_SR2_SRL) != 0;
}

bool flashlock_status(FlashSpi &spi, FlashLockInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->jedec = flashlock_jedec(spi);
    if (info->jedec != FL_JEDEC_W25Q128) {
        info->msg = "not a W25Q128JV: status registers not read";
        return false;
    }
    info->sr1 = flashlock_read_sr1(spi);
    info->sr2 = flashlock_read_sr2(spi);
    info->msg = "status registers read";
    return true;
}

static FlashLockResult done(FlashLockInfo *info, FlashLockResult r, const char *msg)
{
    info->msg = msg;
    return r;
}

FlashLockResult flashlock_apply(FlashSpi &spi, bool enabled, FlashLockInfo *info)
{
    memset(info, 0, sizeof(*info));
    if (!enabled) {
        return done(info, FLR_OFF, "flash protection is off");
    }
    info->jedec = flashlock_jedec(spi);
    if (info->jedec != FL_JEDEC_W25Q128) {
        return done(info, FLR_OTHER_CHIP, "not a W25Q128JV: the flash is not protected");
    }
    // status register 1: top/bottom = bottom, sector = off, BP = 101; the other bits as they are, SRP cleared by fl_write_sr1
    uint8_t cur1 = flashlock_read_sr1(spi);
    fl_write_sr1(spi, (uint8_t)((cur1 & ~(FL_SR1_SEC | FL_SR1_TB | FL_SR1_BP)) | FL_SR1_WANT));
    uint8_t sr1 = flashlock_read_sr1(spi);
    if (sr1 & FL_SR1_SRP) {                                                       // guard 2: never set SRL while SRP reads 1
        info->sr1 = sr1;
        return done(info, FLR_FAILED, "status register protect bit would not clear: lock-down not attempted");
    }
    // status register 2: CMP clear (it inverts the range), SRL set
    uint8_t cur2 = flashlock_read_sr2(spi);
    fl_write_sr2(spi, (uint8_t)((cur2 & ~FL_SR2_CMP) | FL_SR2_SRL));
    info->sr1 = flashlock_read_sr1(spi);
    info->sr2 = flashlock_read_sr2(spi);
    if (!flashlock_is_set(info->sr1, info->sr2)) {
        return done(info, FLR_FAILED, "the flash status registers do not read back as set");
    }
    return done(info, FLR_LOCKED, "flash 0x000000-0x3FFFFF protected, lock-down set");
}
