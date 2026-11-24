#include "d1541_wd.h"
#include "drvtab.h"
#include <string.h>

#ifdef DRV_HAS_drive_wd        // the WD177x is a block of the TOML A; a drive without it does not link this file

#define WD_CMD_RESTORE      0
#define WD_CMD_SEEK         1
#define WD_CMD_STEP         2
#define WD_CMD_STEP_IN      4
#define WD_CMD_STEP_OUT     6
#define WD_CMD_READ_SECTOR  8
#define WD_CMD_WRITE_SECTOR 10
#define WD_CMD_READ_ADDR    12
#define WD_CMD_ABORT        13
#define WD_CMD_READ_TRACK   14
#define WD_CMD_WRITE_TRACK  15
#define WD_CMD_UPDATE_BIT   0x10
#define WD_CMD_SPINUP_BIT   0x08
#define WD_CMD_DMA_DONE     0x100

// the drive block's registers the controller looks at (DRIVE_A_BASE + n)
#define DRV_SIDE       6
#define DRV_TRACK      8
#define DRV_DISKCHANGE 12
#define WD_BASE        (DRV_drive_wd_ADDR - DRV_drive_a_ADDR)      // the drive window's offset of the block

// ---- the disk's layout --------------------------------------------------------------------------------------------------------
MfmDisk::MfmDisk()
{
    memset(side0_, 0, sizeof(side0_));
    memset(side1_, 0, sizeof(side1_));
}

MfmTrack *MfmDisk::track(int t, int side)
{
    if (t < 0 || t >= WD_MAX_TRACKS_PER_SIDE || side < 0 || side > 1) {
        return 0;
    }
    return side ? &side1_[t] : &side0_[t];
}

void MfmDisk::init_d81(int tracks)
{
    memset(side0_, 0, sizeof(side0_));
    memset(side1_, 0, sizeof(side1_));
    if (tracks < 0) {
        tracks = 0;
    }
    if (tracks > WD_MAX_TRACKS_PER_SIDE) {
        tracks = WD_MAX_TRACKS_PER_SIDE;
    }
    uint32_t offset = 0;
    for (int t = 0; t < tracks; t++) {
        for (int side = 0; side < 2; side++) {
            MfmTrack &m = side ? side1_[t] : side0_[t];
            m.actualDataSize = m.reservedSpace = WD_BYTES_PER_TRACK;
            m.numSectors = 10;
            m.offsetInFile = offset;
            offset += WD_BYTES_PER_TRACK;
            for (int s = 0; s < 10; s++) {
                m.sectors[s].sector = (uint8_t)(s + 1);
                m.sectors[s].sector_size = 2;
                m.sectors[s].side = (uint8_t)side;
                m.sectors[s].track = (uint8_t)t;
            }
        }
    }
}

int MfmDisk::get_sector(int physTrack, int physSide, const MfmSector &want, uint32_t &pos, uint32_t &size)
{
    MfmTrack *tr = track(physTrack, physSide);
    if (!tr) {
        return -1;
    }
    uint32_t cur = tr->offsetInFile;
    for (int i = 0; i < tr->numSectors; i++) {
        const MfmSector &s = tr->sectors[i];
        // the side is not compared: the WD177x has no side register
        if (s.sector == want.sector && s.track == want.track) {
            pos = cur;
            size = s.sector_size < 4 ? (128u << s.sector_size) : 512u;
            return i;
        }
        cur += 128u << (s.sector_size & 3);
    }
    return -2;
}

int MfmDisk::get_address(int physTrack, int physSide, int lastIndex, MfmSector &addr)
{
    MfmTrack *tr = track(physTrack, physSide);
    if (!tr || !tr->numSectors) {
        return -1;
    }
    int idx = lastIndex + 1;
    if (idx >= tr->numSectors) {
        idx = 0;
    }
    addr = tr->sectors[idx];
    return idx;
}

int MfmDisk::update_track(int physTrack, int physSide, MfmTrack &nt, uint32_t &pos)
{
    MfmTrack *tr = track(physTrack, physSide);
    if (!tr) {
        return -1;
    }
    uint32_t required = 0;
    for (int i = 0; i < nt.numSectors; i++) {
        required += 128u << (nt.sectors[i].sector_size & 3);
    }
    if (required > tr->reservedSpace) {
        return -2;
    }
    pos = tr->offsetInFile;
    tr->actualDataSize = required;
    tr->numSectors = nt.numSectors;
    for (int i = 0; i < nt.numSectors; i++) {
        tr->sectors[i] = nt.sectors[i];
    }
    return 0;
}

// ---- the controller -------------------------------------------------------------------------------------------------------------
static const uint16_t crc16tab[256] = {
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
    0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
    0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
    0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
    0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
    0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
    0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
    0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
    0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
    0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
    0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
    0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
    0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
    0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
    0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};

uint16_t wd_crc16(uint16_t crc, const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        crc = (uint16_t)((crc << 8) ^ crc16tab[(crc >> 8) ^ buf[i]]);
    }
    return crc;
}

static const uint8_t c_step_times[4] = { 6, 12, 20, 30 };      // ms per step

WdController::WdController(CoreIo *io, uint8_t *buffer, uint32_t phys)
    : io_(io), buf_(buffer), phys_(phys), file_(0), tracks_(0), step_dir_(0), sect_index_(0), sect_size_(128), offset_(0),
      write_errors_(0), commands_(0)
{
}

uint8_t WdController::rd(uint32_t r) { return io_->drv_rd(WD_BASE + r); }
void WdController::wr(uint32_t r, uint8_t v) { io_->drv_wr(WD_BASE + r, v); }

void WdController::init()
{
    wr(WDR_COMMAND, 3);                                        // enable the index pulse; inverted
    for (int i = 0; i < 64 && (rd(WDR_IRQ_ACK) & 0x80); i++) {
        wr(WDR_IRQ_ACK, 1);
    }
}

void WdController::mount(CoreFile *f, uint64_t size)
{
    file_ = f;
    tracks_ = f ? (int)(size / 10240) : 0;
    disk_.init_d81(tracks_);
    sect_index_ = 0;
}

bool WdController::poll()
{
    bool did = false;
    for (int n = 0; n < 8 && (rd(WDR_IRQ_ACK) & 0x80); n++) {
        uint16_t cmd = rd(WDR_COMMAND);
        cmd = (uint16_t)(cmd | ((rd(WDR_IRQ_ACK) & 1) << 8));   // the 9th bit: the DMA of a write is done
        wr(WDR_IRQ_ACK, 1);
        command(cmd);
        did = true;
        commands_++;
    }
    return did;
}

void WdController::wait_head_settle()
{
    for (int i = 0; i < 500; i++) {
        if (!rd(WDR_STEPPER)) {
            break;
        }
        io_->delay_ms(1);
    }
}

void WdController::do_step(uint16_t cmd)
{
    uint8_t track = io_->drv_rd(DRV_TRACK);
    if (step_dir_ == 0) {
        if (track != 0) {
            track--;
        }
    } else if (track < 83) {
        track++;
    }
    wr(WDR_STEP_TIME, c_step_times[cmd & 3]);
    wr(WDR_STEPPER, track);
    if (cmd & WD_CMD_UPDATE_BIT) {
        uint8_t t = rd(WDR_TRACK);
        if (step_dir_ == 0) {
            if (t != 0) {
                wr(WDR_TRACK, (uint8_t)(t - 1));
            }
        } else if (t != 0xFF) {
            wr(WDR_TRACK, (uint8_t)(t + 1));
        }
    }
    io_->drv_wr(DRV_DISKCHANGE, 0);
}

void WdController::command(uint16_t cmd)
{
    if (cmd & WD_CMD_DMA_DONE) {
        completion(cmd);
        return;
    }
    MfmSector sa;
    memset(&sa, 0, sizeof(sa));
    wr(WDR_STATUS_CLEAR, WD_STATUS_RNF);
    switch ((cmd >> 4) & 0x0F) {
    case WD_CMD_RESTORE:
        wr(WDR_TRACK, 0);
        wr(WDR_STEP_TIME, c_step_times[cmd & 3]);
        wr(WDR_STEPPER, 0);
        step_dir_ = 1;
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) {
            wr(WDR_STATUS_SET, WD_STATUS_SPINUP_OK);
        }
        io_->drv_wr(DRV_DISKCHANGE, 0);
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        break;
    case WD_CMD_SEEK: {
        uint8_t t = rd(WDR_DATAREG);
        wr(WDR_TRACK, t);
        wr(WDR_STEP_TIME, c_step_times[cmd & 3]);
        wr(WDR_STEPPER, t);
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) {
            wr(WDR_STATUS_SET, WD_STATUS_SPINUP_OK);
        }
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        break;
    }
    case WD_CMD_STEP:
    case WD_CMD_STEP + 1:
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) {
            wr(WDR_STATUS_SET, WD_STATUS_SPINUP_OK);
        }
        do_step(cmd);
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        break;
    case WD_CMD_STEP_IN:
    case WD_CMD_STEP_IN + 1:
        if ((cmd & WD_CMD_SPINUP_BIT) == 0) {
            wr(WDR_STATUS_SET, WD_STATUS_SPINUP_OK);
        }
        step_dir_ = 1;
        do_step(cmd);
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        break;
    case WD_CMD_STEP_OUT:
    case WD_CMD_STEP_OUT + 1:
        if (cmd) {
            wr(WDR_STATUS_SET, WD_STATUS_SPINUP_OK);
        }
        step_dir_ = 0;
        do_step(cmd & WD_CMD_UPDATE_BIT);
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        break;
    case WD_CMD_READ_SECTOR:
    case WD_CMD_READ_SECTOR + 1: {
        wait_head_settle();
        sa.sector = rd(WDR_SECTOR);
        sa.track = rd(WDR_TRACK);
        sa.side = io_->drv_rd(DRV_SIDE);
        sect_index_ = disk_.get_sector(io_->drv_rd(DRV_TRACK), io_->drv_rd(DRV_SIDE), sa, offset_, sect_size_);
        if (sect_index_ < 0) {
            wr(WDR_STATUS_SET, WD_STATUS_RNF);
            wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
            break;
        }
        // a sector that cannot be read must not reach the 1581 as the bytes of the one before: the controller reports a CRC error
        if (!file_ || sect_size_ > WD_BUFFER_BYTES || !file_->read(offset_, buf_, sect_size_)) {
            wr(WDR_STATUS_SET, WD_STATUS_CRC_ERROR);
            wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
            break;
        }
        wr(WDR_DMA_LEN, (uint8_t)sect_size_);
        wr(WDR_DMA_LEN + 1, (uint8_t)(sect_size_ >> 8));
        io_->drv_wr32(WD_BASE + WDR_DMA_ADDR, phys_);
        wr(WDR_DMA_MODE, 1);
        break;
    }
    case WD_CMD_WRITE_SECTOR:
    case WD_CMD_WRITE_SECTOR + 1:
        wait_head_settle();
        sa.sector = rd(WDR_SECTOR);
        sa.track = rd(WDR_TRACK);
        sa.side = io_->drv_rd(DRV_SIDE);
        sect_index_ = disk_.get_sector(io_->drv_rd(DRV_TRACK), io_->drv_rd(DRV_SIDE), sa, offset_, sect_size_);
        if (sect_index_ >= 0) {
            memset(buf_, 0x77, sect_size_ < WD_BUFFER_BYTES ? sect_size_ : WD_BUFFER_BYTES);
            wr(WDR_DMA_LEN, (uint8_t)sect_size_);
            wr(WDR_DMA_LEN + 1, (uint8_t)(sect_size_ >> 8));
            io_->drv_wr32(WD_BASE + WDR_DMA_ADDR, phys_);
            wr(WDR_DMA_MODE, 2);
        } else {
            wr(WDR_STATUS_SET, WD_STATUS_RNF);
            wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        }
        break;
    case WD_CMD_READ_ADDR: {
        wait_head_settle();
        sect_index_ = disk_.get_address(io_->drv_rd(DRV_TRACK), io_->drv_rd(DRV_SIDE), sect_index_, sa);
        if (sect_index_ < 0) {
            wr(WDR_STATUS_SET, WD_STATUS_RNF);
            wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
            break;
        }
        buf_[0] = sa.track;
        buf_[1] = sa.side;
        buf_[2] = sa.sector;
        buf_[3] = sa.sector_size;
        uint16_t crc = wd_crc16(0xb230, buf_, 4);
        buf_[4] = (uint8_t)(crc >> 8);
        buf_[5] = (uint8_t)crc;
        wr(WDR_DMA_LEN, 6);
        wr(WDR_DMA_LEN + 1, 0);
        io_->drv_wr32(WD_BASE + WDR_DMA_ADDR, phys_);
        wr(WDR_DMA_MODE, 1);
        break;
    }
    case WD_CMD_READ_TRACK:
        wr(WDR_STATUS_CLEAR, 1);                                // not implemented in the firmware either
        break;
    case WD_CMD_WRITE_TRACK:
        wr(WDR_DMA_LEN, (uint8_t)WD_RAW_TRACK_BYTES);
        wr(WDR_DMA_LEN + 1, (uint8_t)(WD_RAW_TRACK_BYTES >> 8));
        io_->drv_wr32(WD_BASE + WDR_DMA_ADDR, phys_);
        wr(WDR_DMA_MODE, 2);
        break;
    case WD_CMD_ABORT:
        wr(WDR_DMA_MODE, 0);
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
        break;
    }
}

void WdController::completion(uint16_t cmd)
{
    switch ((cmd >> 4) & 0x0F) {
    case WD_CMD_WRITE_SECTOR:
    case WD_CMD_WRITE_SECTOR + 1:
        wr(WDR_DMA_MODE, 0);
        if (!file_ || !file_->write(offset_, buf_, sect_size_) || !file_->sync()) {
            write_errors_++;
        }
        break;
    case WD_CMD_WRITE_TRACK: {
        static uint8_t binbuf[WD_BYTES_PER_TRACK];
        MfmTrack nt;
        decode_write_track(buf_, binbuf, nt);
        wait_head_settle();
        int t = io_->drv_rd(DRV_TRACK), side = io_->drv_rd(DRV_SIDE);
        if (disk_.update_track(t, side, nt, offset_) != 0 || !file_ || !file_->write(offset_, binbuf, nt.actualDataSize) || !file_->sync()) {
            write_errors_++;
        }
        wr(WDR_DMA_MODE, 0);
        break;
    }
    default:
        wr(WDR_STATUS_CLEAR, WD_STATUS_BUSY);
    }
}

// The raw bytes a track format wrote: 0xFE headers (track, side, sector, size, 0xF7 for the CRC) and 0xFB data blocks, the sector data
// collected in the order of the blocks.
int WdController::decode_write_track(const uint8_t *in, uint8_t *out, MfmTrack &nt)
{
    int rpos = 0;
    int wpos = 0;
    int sec_size = 0;
    memset(out, 0x55, WD_BYTES_PER_TRACK);
    memset(&nt, 0, sizeof(nt));
    while (rpos < WD_RAW_TRACK_BYTES) {
        if (in[rpos] == 0xFE) {
            if (in[rpos + 4] >= 4) {
                return -100;
            }
            sec_size = 1 << (7 + in[rpos + 4]);
            if (in[rpos + 5] != 0xF7 || nt.numSectors >= WD_MAX_SECTORS_PER_TRACK) {
                return -101;
            }
            nt.sectors[nt.numSectors].track = in[rpos + 1];
            nt.sectors[nt.numSectors].side = in[rpos + 2];
            nt.sectors[nt.numSectors].sector = in[rpos + 3];
            nt.sectors[nt.numSectors].sector_size = in[rpos + 4];
            rpos += 16;
        } else if (in[rpos] == 0xFB) {
            if (rpos + 1 + sec_size >= WD_RAW_TRACK_BYTES + 2 || in[rpos + 1 + sec_size] != 0xF7) {
                return -102;
            }
            if (wpos + sec_size > WD_BYTES_PER_TRACK) {
                return -103;
            }
            memcpy(out, &in[rpos + 1], (size_t)sec_size);
            out += sec_size;
            wpos += sec_size;
            nt.actualDataSize = (uint32_t)wpos;
            nt.numSectors++;
            rpos += sec_size;
            rpos += 16;
        } else {
            rpos++;
        }
    }
    return 0;
}

#endif
