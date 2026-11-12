// A model of the Atari core's SIO bridge (a8_regs.vhd 0D0-0D5) in front of Watson's sio_handler.vhdl FIFOs, as far as the
// application can see them: a receive FIFO of 15-bit words {command count, byte}, a 256-byte transmit FIFO that empties at
// 19200 baud (one byte per 521 us of virtual time), and a bridge that can be stuck (the core is held).
#ifndef TESTS_COREAPP_SIO_MODEL_H
#define TESTS_COREAPP_SIO_MODEL_H

#include <deque>
#include "fakes.h"
#include "atari800_regs.h"
#include "atari_sio.h"

struct SioModel : FakeIo {
    std::deque<uint16_t> rx;
    std::vector<uint8_t> wire;             // everything the application sent, in order
    std::deque<uint8_t> txq;
    uint64_t next_tx_us = 0;
    bool stuck = false;                    // the bridge never finishes (core held)
    int rx_pops_when_empty = 0;
    std::vector<uint64_t> wire_us;         // virtual time each byte left the FIFO
    uint16_t result = 0;
    uint8_t rx_div = 93;                   // the bit time the handler measured on the Atari's last bytes (19200 baud: 94 machine cycles)
    uint8_t tx_div = 0;                    // the transmit divisor; 0 after the reset, and then a bit leaves every machine cycle
    std::vector<uint8_t> wire_div;         // the transmit divisor in force when each byte went into the FIFO

    SioModel() { rom_sel_reg = A8_ROM_SEL; }

    void advance(uint32_t u)
    {
        FakeIo::advance(u);
        while (!txq.empty() && next_tx_us <= us) {
            wire.push_back(txq.front()); txq.pop_front();
            wire_us.push_back(next_tx_us);
            next_tx_us += 521;
        }
    }
    void on_wr(uint32_t reg, uint8_t v)
    {
        if (reg != A8_SIO_GO || stuck) return;
        int addr = regs[A8_SIO_ADDR] & 31;
        if (v & 2) {
            if (addr == SIO_TX) {
                if (txq.size() < 256) {
                    if (txq.empty()) next_tx_us = us > next_tx_us ? us : next_tx_us;
                    txq.push_back(regs[A8_SIO_WDATA]);
                    wire_div.push_back(tx_div);
                }
            } else if (addr == SIO_DIVISOR) {
                tx_div = regs[A8_SIO_WDATA];
            }
        } else if (v & 1) {
            if (addr == SIO_DIVISOR) {
                result = rx_div;
            } else if (addr == SIO_TX_STATUS) {
                result = (uint16_t)((txq.size() == 256 ? 0x200 : 0) | (txq.empty() ? 0x100 : 0) | (txq.size() & 0xFF));
            } else if (addr == SIO_RX_STATUS) {
                result = (uint16_t)((rx.empty() ? 0x100 : 0) | (rx.size() & 0xFF));
            } else if (addr == SIO_RX) {
                if (!rx.empty()) { result = rx.front(); rx.pop_front(); } else rx_pops_when_empty++;
            }
        }
        regs[A8_SIO_RDATA] = (uint8_t)result;
        regs[A8_SIO_RDATA + 1] = (uint8_t)(result >> 8) & 0x7F;
    }
    uint8_t rd(uint32_t reg)
    {
        if (reg == A8_SIO_STATUS) return stuck ? 1 : 0;
        return FakeIo::rd(reg);
    }

    // The Atari sends a command frame: five bytes with counts 1..5, then the word the command line's rising edge writes
    // (count 5, a stale byte). good_marker = false leaves the marker out (a frame that never ended).
    void command(uint8_t dev, uint8_t cmd, uint8_t aux1, uint8_t aux2, int chk_err = 0, bool marker = true)
    {
        uint8_t f[5] = { dev, cmd, aux1, aux2, 0 };
        f[4] = (uint8_t)(sio_checksum(f, 4) + chk_err);
        for (int i = 0; i < 5; i++) rx.push_back((uint16_t)(((i + 1) << 8) | f[i]));
        if (marker) rx.push_back((uint16_t)((5 << 8) | 0xFF));
    }
    // data the Atari sends after our ACK (count 0), with its checksum
    void data(const std::vector<uint8_t> &d, int chk_err = 0)
    {
        for (auto b : d) rx.push_back(b);
        rx.push_back((uint8_t)(sio_checksum(d.data(), (int)d.size()) + chk_err));
    }
    // every byte the application sent went out at the speed the Atari spoke at (a byte sent at another divisor is not a byte to POKEY)
    bool sent_at_atari_speed() const
    {
        for (auto d : wire_div) if (d != rx_div) return false;
        return !wire_div.empty();
    }
    void settle() { advance(400000); }       // let the transmit FIFO empty
    std::vector<uint8_t> take() { settle(); auto w = wire; wire.clear(); wire_us.clear(); wire_div.clear(); return w; }
};

#endif
