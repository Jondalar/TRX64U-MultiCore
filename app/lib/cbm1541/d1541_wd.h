// The 1581's floppy controller, in software (T104.1 D15 extended to the 1581: Alex, 2026-10-22, "and .d81 right away too!").
//
// The mm_drive of the C64U gateware (cores/c64u, CAPAB_MM_DRIVE) runs the 1581's own CPU and DOS and puts a WD1770 look-alike in front of
// it: the DOS writes a command, the gateware queues it and raises an interrupt, and the application does what the disk drive's
// controller would do - seeks, sector reads and writes and a track format (the 1581's NEW) - by DMA between the 1581's RAM-less
// controller and a buffer in DDR2, with the sectors in the .d81 file. This is Gideon's wd177x.cc and mfmdisk.cc, with the interrupt turned
// into a poll of the FIFO flag (irq_ack bit 7: the handler's own test of "a command is waiting") and the file behind CoreFile.
//
// A .d81 is 80 tracks of 2 sides of 10 sectors of 512 bytes, in the order of the file: track t side 0, then track t side 1.
#ifndef CBM1541_WD_H
#define CBM1541_WD_H

#include <stdint.h>
#include "fsif.h"
#include "io.h"

// the registers of the block drive_wd of the TOML A (map wd177x; shared.toml puts the block at 0x10021800)
#define WDR_COMMAND   0
#define WDR_TRACK     1
#define WDR_SECTOR    2
#define WDR_DATAREG   3
#define WDR_STATUS_CLEAR 4
#define WDR_STATUS_SET   5
#define WDR_IRQ_ACK   6          // read: b7 a command waits, b0 the DMA of the last command is done; write 1: take it
#define WDR_DMA_MODE  7          // 1 read (to the 1581), 2 write (from it), 0 stop
#define WDR_DMA_ADDR  8          // 32 bit
#define WDR_DMA_LEN   12         // 16 bit
#define WDR_STEPPER   14
#define WDR_STEP_TIME 15

#define WD_STATUS_BUSY      0x01
#define WD_STATUS_CRC_ERROR 0x08
#define WD_STATUS_RNF       0x10
#define WD_STATUS_SPINUP_OK 0x20

#define WD_MAX_SECTORS_PER_TRACK 32
#define WD_MAX_TRACKS_PER_SIDE   84
#define WD_RAW_TRACK_BYTES       6250
#define WD_BYTES_PER_TRACK       5120
#define WD_BUFFER_BYTES          (WD_RAW_TRACK_BYTES + 2)

struct MfmSector {
    uint8_t track, side, sector, sector_size, error_byte;     // sector_size 0 .. 3 = 128 .. 1024
};

struct MfmTrack {
    int numSectors;
    uint32_t offsetInFile;
    uint32_t reservedSpace;
    uint32_t actualDataSize;
    MfmSector sectors[WD_MAX_SECTORS_PER_TRACK];
};

class MfmDisk {
public:
    MfmDisk();
    void init_d81(int tracks);                                // 10 sectors of 512 bytes per side, numbered 1 .. 10
    int get_sector(int physTrack, int physSide, const MfmSector &want, uint32_t &pos, uint32_t &size);
    int get_address(int physTrack, int physSide, int lastIndex, MfmSector &addr);
    int update_track(int physTrack, int physSide, MfmTrack &newTrack, uint32_t &pos);
    MfmTrack *track(int t, int side);
private:
    MfmTrack side0_[WD_MAX_TRACKS_PER_SIDE];
    MfmTrack side1_[WD_MAX_TRACKS_PER_SIDE];
};

// CRC-16 CCITT as the controller computes it for an address mark
uint16_t wd_crc16(uint16_t crc, const uint8_t *buf, int len);

class WdController {
public:
    // `buffer` is WD_BUFFER_BYTES the hardware reads and writes at `phys` (drv_buffer).
    WdController(CoreIo *io, uint8_t *buffer, uint32_t phys);

    void init();                                               // enable the index pulse, drain the FIFO
    void mount(CoreFile *f, uint64_t size);                    // a .d81 behind the controller (null: none)
    void unmount() { file_ = 0; }
    // Handles every command the FIFO holds. Returns true when it did something.
    bool poll();
    // The number of sector writes and track formats that failed to reach the file since the last call.
    int take_write_errors() { int n = write_errors_; write_errors_ = 0; return n; }
    int tracks() const { return tracks_; }
    uint32_t commands() const { return commands_; }

private:
    CoreIo *io_;
    uint8_t *buf_;
    uint32_t phys_;
    CoreFile *file_;
    MfmDisk disk_;
    int tracks_;
    uint8_t step_dir_;
    int sect_index_;
    uint32_t sect_size_;
    uint32_t offset_;
    int write_errors_;
    uint32_t commands_;

    uint8_t rd(uint32_t r);
    void wr(uint32_t r, uint8_t v);
    void do_step(uint16_t cmd);
    void wait_head_settle();
    void command(uint16_t cmd);
    void completion(uint16_t cmd);
    int decode_write_track(const uint8_t *in, uint8_t *out, MfmTrack &t);
};

#endif
