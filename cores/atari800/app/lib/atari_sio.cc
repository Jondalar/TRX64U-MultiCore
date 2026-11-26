#include "atari_sio.h"
#include "atari800_regs.h"
#include <stdio.h>
#include <string.h>

uint8_t sio_checksum(const uint8_t *p, int n)
{
    unsigned sum = 0;
    for (int i = 0; i < n; i++) {
        sum += p[i];
        if (sum > 255) {
            sum = (sum & 0xFF) + 1;
        }
    }
    return (uint8_t)sum;
}

// ---- the register window ---------------------------------------------------------------------------------------------------

bool SioLink::wait_idle()
{
    // a bridge access takes one machine tick (about a microsecond with the core running); 200 reads of the status register is
    // far more than that and still short when the core is held
    for (int i = 0; i < 200; i++) {
        if (!(io_->rd(A8_SIO_STATUS) & 1)) {
            return true;
        }
    }
    failed_++;
    return false;
}

bool SioLink::read(int reg, uint16_t *v)
{
    if (!wait_idle()) {
        return false;
    }
    io_->wr(A8_SIO_ADDR, (uint8_t)reg);
    io_->wr(A8_SIO_GO, 1);
    if (!wait_idle()) {
        return false;
    }
    *v = (uint16_t)(io_->rd(A8_SIO_RDATA) | ((io_->rd(A8_SIO_RDATA + 1) & 0x7F) << 8));
    return true;
}

bool SioLink::write(int reg, uint8_t v)
{
    if (!wait_idle()) {
        return false;
    }
    io_->wr(A8_SIO_ADDR, (uint8_t)reg);
    io_->wr(A8_SIO_WDATA, v);
    io_->wr(A8_SIO_GO, 2);
    return wait_idle();
}

// ---- transmit ---------------------------------------------------------------------------------------------------------------

// The transmit FIFO holds 256 bytes; a 256-byte sector with COMPLETE and checksum is 258, so the sender watches the fill level.
void SioEngine::send(const uint8_t *p, int n)
{
    for (int i = 0; i < n;) {
        uint16_t st = 0;
        if (!link_->read(SIO_TX_STATUS, &st)) {
            return;                                    // the core is held: nothing goes out
        }
        int used = st & 0xFF;
        if ((st & 0x200) || used >= 240) {
            link_->io()->delay_us(1000);               // two bytes leave per millisecond at 19200 baud
            continue;
        }
        int room = 240 - used;
        for (; room > 0 && i < n; room--, i++) {
            if (!link_->write(SIO_TX, p[i])) {
                return;
            }
        }
    }
}

void SioEngine::send_byte(uint8_t b)
{
    send(&b, 1);
}

void SioEngine::send_with_checksum(const uint8_t *p, int n)
{
    send(p, n);
    send_byte(sio_checksum(p, n));
}

// ---- the drive's answers -----------------------------------------------------------------------------------------------------

void SioEngine::status(AtariDisk *d, uint8_t out[4])
{
    // drive status: b4 motor on, b5 double density (256 byte sectors), b3 write protected; controller status $FF; the format
    // timeout $E0 (the value an Atari 1050 reports); unused 0
    out[0] = 0x10;
    if (d->image_sector_size() == 256) {
        out[0] |= 0x20;
    }
    if (!d->writable()) {
        out[0] |= 0x08;
    }
    out[1] = 0xFF;
    out[2] = 0xE0;
    out[3] = 0x00;
}

void SioEngine::percom(AtariDisk *d, uint8_t out[12])
{
    bool dd = d->image_sector_size() == 256;
    uint32_t n = d->sectors();
    // 40 tracks, step rate 1, sectors per track (18 / 26 for enhanced density), one side, FM or MFM, bytes per sector, drive present
    uint16_t spt = 18;
    if (n >= 1040) {
        spt = 26;                                      // enhanced density: 1040 sectors of 128 bytes
    }
    out[0] = 40;
    out[1] = 1;
    out[2] = (uint8_t)(spt >> 8);
    out[3] = (uint8_t)spt;
    out[4] = 0;
    out[5] = (n >= 1040 || dd) ? 4 : 0;
    out[6] = dd ? 1 : 0;
    out[7] = dd ? 0 : 128;
    out[8] = 0xFF;
    out[9] = out[10] = out[11] = 0;
}

void SioEngine::fail_write(int drive)
{
    writes_failed_++;
    if (!told_[drive] && say_) {
        char m[120];
        snprintf(m, sizeof(m), "D%d: writing a sector back to its file failed (medium read only or gone). The Atari got an error.", drive + 1);
        say_(say_user_, m);
    }
    told_[drive] = true;
}

#define T2_US 300      // command line high to ACK: 250 us at the least
#define T5_US 300      // the end of the ACK byte to COMPLETE: 250 us at the least
#define BYTE_US 540    // one byte on the wire at 19200 baud (measured 536 us: sio_handler sends a bit per 95 machine cycles)

// The pause between ACK and COMPLETE. The FIFO only says when the ACK byte began (it is empty once the byte is in the shifter),
// so: wait for that, one byte time, then T5. Counted from the moment the ACK was queued, as this code did before, COMPLETE
// followed the ACK with no gap at all; the XL OS boots from that as well (T108 5.2), so this is the protocol's spacing and not
// something the OS was seen to need.
void SioEngine::after_ack()
{
    for (int i = 0; i < 2000; i++) {
        uint16_t st = 0;
        if (!link_->read(SIO_TX_STATUS, &st) || (st & 0x100)) {
            break;
        }
        link_->io()->delay_us(100);
    }
    link_->io()->delay_us(BYTE_US + T5_US);
}

// The handler measures the bit time of the bytes it receives (receive divisor, register 4 read) and sends at the transmit
// divisor (register 4 write), which is 0 after the reset: a bit then leaves every machine cycle instead of every 94, a byte in
// 6 us, and POKEY cannot read it. So the answer goes out at the speed the Atari just spoke at.
void SioEngine::match_baud()
{
    uint16_t div = 0;
    if (link_->read(SIO_DIVISOR, &div)) {
        link_->write(SIO_DIVISOR, (uint8_t)div);
    }
}

void SioEngine::command()
{
    frames_++;
    int dev = cmd_[0];
    int drive = dev - 0x31;
    if (drive < 0 || drive >= SIO_DRIVES || !drives_[drive]) {
        return;                                        // not ours: a drive that is not there does not answer
    }
    match_baud();
    AtariDisk *d = drives_[drive];
    if (sio_checksum(cmd_, 4) != cmd_[4]) {
        bad_chk_++;
        link_->io()->delay_us(T2_US);
        send_byte('N');
        return;
    }
    uint8_t cmd = cmd_[1];
    uint32_t sec = (uint32_t)cmd_[2] | ((uint32_t)cmd_[3] << 8);
    uint8_t buf[260];
    link_->io()->delay_us(T2_US);
    switch (cmd) {
    case 0x52: {                                       // 'R' read sector
        if (sec < 1 || sec > d->sectors()) {
            send_byte('N');
            return;
        }
        send_byte('A');
        uint16_t ss = d->sector_size(sec);
        buf[0] = 'C';
        if (!d->read(sec, buf + 1)) {
            buf[0] = 'E';
            after_ack();
            send_byte('E');
            return;
        }
        after_ack();
        send(buf, 1 + ss);
        send_byte(sio_checksum(buf + 1, ss));
        return;
    }
    case 0x53:                                         // 'S' status
        send_byte('A');
        status(d, buf + 1);
        buf[0] = 'C';
        after_ack();
        send_byte('C');
        send_with_checksum(buf + 1, 4);
        return;
    case 0x4E:                                         // 'N' read the drive configuration
        send_byte('A');
        percom(d, buf + 1);
        after_ack();
        send_byte('C');
        send_with_checksum(buf + 1, 12);
        return;
    case 0x57:                                         // 'W' write sector with verify
    case 0x50:                                         // 'P' put sector
        if (sec < 1 || sec > d->sectors() || !d->writable()) {
            send_byte('N');
            return;
        }
        send_byte('A');
        state_ = DATA;
        ndata_ = 0;
        want_ = d->sector_size(sec) + 1;               // the data and its checksum
        pend_drive_ = drive;
        pend_sector_ = sec;
        data_deadline_ = link_->io()->now_ms() + 100;
        return;
    case 0x4F:                                         // 'O' write the drive configuration: taken and ignored
        send_byte('A');
        state_ = DATA;
        ndata_ = 0;
        want_ = 13;
        pend_drive_ = drive;
        pend_sector_ = 0;                              // 0: not a sector
        data_deadline_ = link_->io()->now_ms() + 100;
        return;
    default:                                           // format, high speed index, anything else: NAK
        unknown_++;
        send_byte('N');
        return;
    }
}

void SioEngine::get_data_frame_done()
{
    AtariDisk *d = drives_[pend_drive_];
    state_ = IDLE;
    link_->io()->delay_us(T2_US);
    if (!d || sio_checksum(data_, want_ - 1) != data_[want_ - 1]) {
        bad_chk_++;
        send_byte('N');
        return;
    }
    send_byte('A');
    after_ack();
    if (pend_sector_ == 0) {                           // the configuration block: nothing to store
        send_byte('C');
        return;
    }
    if (d->write(pend_sector_, data_)) {
        told_[pend_drive_] = false;
        dirty_[pend_drive_] = true;
        last_write_[pend_drive_] = link_->io()->now_ms();
        send_byte('C');
    } else {
        fail_write(pend_drive_);
        send_byte('E');
    }
}

// ---- receive -----------------------------------------------------------------------------------------------------------------

bool SioEngine::pop_word(uint16_t *w, bool *empty)
{
    uint16_t st;
    *empty = true;
    if (!link_->read(SIO_RX_STATUS, &st)) {
        return false;
    }
    if (st & 0x100) {
        return true;
    }
    *empty = false;
    return link_->read(SIO_RX, w);
}

// The receive FIFO's word is {7 bits: how many bytes of the current command frame came so far, 8 bits: the byte}. Bytes 1..5 of
// a command frame carry counts 1..5; when the command line rises one more word is written with the final count and a stale byte
// (sio_handler.vhdl). So a frame is complete when a word that is not "byte n+1" follows five bytes with count 5; data the
// Atari sends after our ACK arrives with count 0.
bool SioEngine::poll()
{
    bool did = false;
    for (int guard = 0; guard < 300; guard++) {
        uint16_t w = 0;
        bool empty = true;
        if (!pop_word(&w, &empty) || empty) {
            break;
        }
        did = true;
        int count = (w >> 8) & 0x7F;
        uint8_t byte = (uint8_t)w;
        if (state_ == DATA) {
            if (count == 0) {
                if (ndata_ < (int)sizeof(data_)) {
                    data_[ndata_++] = byte;
                }
                if (ndata_ == want_) {
                    get_data_frame_done();
                }
                continue;
            }
            state_ = IDLE;                             // a command frame began: the data never came
            ncmd_ = 0;
        }
        if (count == 0) {
            continue;                                  // a stray byte
        }
        if (ncmd_ < 5 && count == ncmd_ + 1) {
            cmd_[ncmd_++] = byte;
        } else {
            if (ncmd_ == 5 && count == 5) {
                command();
            }
            ncmd_ = 0;
        }
    }
    if (state_ == DATA && (int32_t)(link_->io()->now_ms() - data_deadline_) > 0) {
        state_ = IDLE;                                 // the Atari gave up on its write; so do we
    }
    return did;
}

// A sync that the medium refused: said once per drive, the drive stays dirty and is tried again.
void SioEngine::fail_sync(int drive)
{
    if (!told_[drive] && say_) {
        char m[120];
        snprintf(m, sizeof(m), "D%d: writing the disk back to its file failed (medium read only or gone). It stays mounted.", drive + 1);
        say_(say_user_, m);
    }
    told_[drive] = true;
}

void SioEngine::flush_idle(uint32_t idle_ms)
{
    uint32_t now = link_->io()->now_ms();
    for (int i = 0; i < SIO_DRIVES; i++) {
        if (dirty_[i] && drives_[i] && now - last_write_[i] >= idle_ms) {
            if (drives_[i]->flush()) {
                dirty_[i] = false;
            } else {
                fail_sync(i);
                last_write_[i] = now;                  // T180 D4: not forgotten; the next try is idle_ms away
            }
        }
    }
}

bool SioEngine::flush_drive(int drive, char *msg, int msglen)
{
    if (!dirty_[drive] || !drives_[drive]) {
        return true;
    }
    if (drives_[drive]->flush()) {
        dirty_[drive] = false;
        return true;
    }
    fail_sync(drive);
    if (msg && msglen) {
        snprintf(msg, msglen, "D%d: writing the disk back to its file failed (medium read only or gone). It stays mounted.", drive + 1);
    }
    return false;
}

bool SioEngine::flush_all(char *msg, int msglen)
{
    bool all = true;
    for (int i = 0; i < SIO_DRIVES; i++) {
        if (!flush_drive(i, all ? msg : 0, msglen)) {
            all = false;
        }
    }
    return all;
}

bool SioEngine::pending() const
{
    for (int i = 0; i < SIO_DRIVES; i++) {
        if (dirty_[i] && drives_[i]) {
            return true;
        }
    }
    return false;
}
