// The SIO bus of the Atari 800 XL core as a software drive (T93 D6 "sector service", T88 D8): the core exposes Watson's
// sio_handler as a register window (a8_regs.vhd 0D0-0D5: transmit/receive FIFOs of a UART), the application is the drive.
// It assembles command frames from the receive FIFO, answers D1:-D4: from AtariDisk objects (ACK, COMPLETE, data, checksum)
// and writes sectors back. The frame logic is pure code on top of SioLink, so the tests drive it with a model of the FIFOs.
#ifndef COREAPP_ATARI_SIO_H
#define COREAPP_ATARI_SIO_H

#include "atari_disk.h"
#include "io.h"

// The Atari's checksum: the bytes added with the carry brought round (a sum above 255 loses 256 and gains 1).
uint8_t sio_checksum(const uint8_t *p, int n);

// One access to the handler's registers at a time, over the core window (a8_regs.vhd): write SIO_ADDR, SIO_GO, wait for the
// bridge, read the 15-bit result. An access needs machine ticks, so it never finishes while the core is held: every call has a
// timeout and says false then.
class SioLink {
public:
    explicit SioLink(CoreIo *io) : io_(io), failed_(0) { }
    bool read(int reg, uint16_t *v);
    bool write(int reg, uint8_t v);
    CoreIo *io() { return io_; }
    int failures() const { return failed_; }
private:
    bool wait_idle();
    CoreIo *io_;
    int failed_;
};

#define SIO_DRIVES 4

class SioEngine {
public:
    SioEngine(SioLink *link, AtariDisk *const *drives) : link_(link), drives_(drives), state_(IDLE), ncmd_(0), ndata_(0),
        want_(0), frames_(0), bad_chk_(0), unknown_(0), writes_failed_(0)
    {
        say_ = 0; say_user_ = 0; data_deadline_ = 0; pend_drive_ = 0; pend_sector_ = 0;
        for (int i = 0; i < SIO_DRIVES; i++) { told_[i] = dirty_[i] = false; last_write_[i] = 0; }
    }

    // Something the user should read (a write that failed): called at most once per drive until a write succeeds again.
    void on_message(void (*say)(void *user, const char *text), void *user) { say_ = say; say_user_ = user; }

    // Handles what the receive FIFO holds. Returns true when it did something (the caller polls again at once instead of
    // sleeping). Call only while the machine runs: a held core answers nothing.
    bool poll();

    // Write-back policy (T93 D6): sectors are written through at once; the file is synced when no write came for idle_ms, and
    // on every flush_all / flush_drive (eject, replace, reset). A drive whose sync fails says so once, stays dirty and is tried again (T180 D4):
    // flush_drive / flush_all answer false with the reason in msg, and the caller keeps the disk mounted and stops what it was about to do.
    void flush_idle(uint32_t idle_ms);
    bool flush_drive(int drive, char *msg = 0, int msglen = 0);
    bool flush_all(char *msg = 0, int msglen = 0);
    bool pending() const;                                   // a written sector whose file is not synced yet

    int frames() const { return frames_; }
    int bad_checksums() const { return bad_chk_; }
    int unknown_commands() const { return unknown_; }

    // The 12-byte drive configuration the 'N' command returns for a disk (the Atari "PERCOM" block).
    static void percom(AtariDisk *d, uint8_t out[12]);
    // The 4-byte status the 'S' command returns.
    static void status(AtariDisk *d, uint8_t out[4]);

private:
    enum State { IDLE, DATA };
    bool pop_word(uint16_t *w, bool *empty);
    void command();
    void match_baud();
    void after_ack();
    void send(const uint8_t *p, int n);
    void send_byte(uint8_t b);
    void send_with_checksum(const uint8_t *p, int n);
    void get_data_frame_done();
    void fail_write(int drive);
    void fail_sync(int drive);

    SioLink *link_;
    AtariDisk *const *drives_;
    State state_;
    uint8_t cmd_[5];
    int ncmd_;
    uint8_t data_[300];
    int ndata_, want_;
    uint32_t data_deadline_;
    int pend_drive_;
    uint32_t pend_sector_;
    bool told_[SIO_DRIVES];
    int frames_, bad_chk_, unknown_, writes_failed_;
    bool dirty_[SIO_DRIVES];
    uint32_t last_write_[SIO_DRIVES];
    void (*say_)(void *, const char *);
    void *say_user_;
};

#endif
