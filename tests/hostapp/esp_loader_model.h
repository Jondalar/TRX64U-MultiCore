// T152 S8: a model of the ESP32-S3 ROM loader behind espflash.h's link, for test_espflash.cc and the render bench. It answers like the chip: SYNC (several
// answers), ATTACH_SPI, CHANGE_BAUDRATE (the answer still comes at the old rate), SET_FLASH_PARAMS, FLASH_BEGIN (erases its range), FLASH_DATA (checks the
// checksum, the sequence number and the order) - every request that comes out of order or at the wrong baud rate is answered with an error status or not at all.
// It also holds the module's pins (armed or not) and runs the application it was given: identify() reports the version string found in the flash image at
// 0x10000, only when a bootloader (0xE9) is at 0 and a partition table (0xAA 0x50) at 0x8000.
#ifndef TEST_ESP_LOADER_MODEL_H
#define TEST_ESP_LOADER_MODEL_H

#include "espflash.h"
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

struct EspLoaderModel : EspFlashLink {
    std::vector<uint8_t> flash;                  // the ESP32's flash, 8 MiB, erased 0xFF
    bool armed, run_mode, app_started;
    int arm_on, arm_off, leave_calls, enter_calls, identify_calls;
    bool no_download_msg;                        // enter_loader() never sees "DOWNLOAD"
    bool no_sync;                               // the loader does not answer a sync
    int fail_cmd_op, fail_cmd_nth;              // this command's n-th occurrence (1-based) is answered with an error status
    int drop_data_seq;                          // the FLASH_DATA with this sequence number gets no answer at all
    bool corrupt_flash_data;                    // the loader's flash takes a wrong byte in one block (a failure no status shows: only identify finds it)
    int enter_fail_first;                        // the first n enter_loader() calls see no DOWNLOAD message (a retry gets in)
    int force_major, force_minor;               // >= 0: identify() reports this instead of the flashed application's version
    bool silent_identify;                       // the application does not answer CMD_IDENTIFY
    int sync_answers;                           // a sync is answered this many times
    uint32_t baud;                              // the rate the link was set to
    // the loader's state
    bool in_loader, synced, attached, baud_cmd_done, baud_applied, params_set, begun;
    uint32_t begin_addr, begin_total, begin_blocks, expect_seq;
    std::vector<std::vector<uint8_t>> rxq;
    std::vector<uint32_t> begin_addrs;           // every FLASH_BEGIN address, in order
    std::vector<std::string> log;
    int op_count[32];
    uint32_t blocks_ok;
    bool saw_bad_order;

    EspLoaderModel() : flash(8u << 20, 0xFF), armed(false), run_mode(false), app_started(false), arm_on(0), arm_off(0), leave_calls(0), enter_calls(0), identify_calls(0),
                       no_download_msg(false), no_sync(false), fail_cmd_op(-1), fail_cmd_nth(0), drop_data_seq(-1), corrupt_flash_data(false), enter_fail_first(0), force_major(-1), force_minor(-1), silent_identify(false),
                       sync_answers(2), baud(115200), in_loader(false), synced(false), attached(false), baud_cmd_done(false), baud_applied(false), params_set(false),
                       begun(false), begin_addr(0), begin_total(0), begin_blocks(0), expect_seq(0), blocks_ok(0), saw_bad_order(false)
    {
        memset(op_count, 0, sizeof(op_count));
    }

    void arm(bool on) { armed = on; if (on) arm_on++; else arm_off++; }
    bool enter_loader()
    {
        enter_calls++;
        baud = 115200;
        rxq.clear();
        in_loader = armed && !no_download_msg && enter_calls > enter_fail_first;               // without the pins driven nothing can put the chip into its ROM loader
        run_mode = false;
        synced = attached = baud_cmd_done = baud_applied = params_set = begun = false;
        return in_loader;
    }
    void set_baud(uint32_t bps)
    {
        baud = bps;
        if (baud_cmd_done && bps == ESPF_BAUD) baud_applied = true;
    }
    void leave_loader()
    {
        leave_calls++;
        in_loader = false;
        run_mode = true;
        baud = 115200;
        app_started = flash[0] == 0xE9 && flash[0x8000] == 0xAA && flash[0x8001] == 0x50 && flash[0x10000] == 0xE9;
    }
    bool identify(uint16_t *major, uint16_t *minor)
    {
        identify_calls++;
        if (!run_mode || !app_started || silent_identify) return false;
        if (force_major >= 0) { *major = (uint16_t)force_major; *minor = (uint16_t)force_minor; return true; }
        static const char id[] = "ESP32 WiFi Bridge V";
        const size_t n = sizeof(id) - 1;
        for (size_t i = 0x10000; i + n + 8 < flash.size() && i < 0x10000 + (2u << 20); i++) {
            if (!memcmp(&flash[i], id, n)) {
                uint32_t a = 0, b = 0;
                size_t k = i + n;
                while (flash[k] >= '0' && flash[k] <= '9') a = a * 10 + flash[k++] - '0';
                if (flash[k] != '.') return false;
                for (k++; flash[k] >= '0' && flash[k] <= '9'; k++) b = b * 10 + flash[k] - '0';
                *major = (uint16_t)a;
                *minor = (uint16_t)b;
                return true;
            }
        }
        return false;
    }
    int recv_packet(uint8_t *p, uint32_t cap, int)
    {
        if (rxq.empty()) return 0;
        std::vector<uint8_t> v = rxq.front();
        rxq.erase(rxq.begin());
        uint32_t n = (uint32_t)v.size() < cap ? (uint32_t)v.size() : cap;
        memcpy(p, v.data(), n);
        return (int)n;
    }
    void answer(uint8_t op, bool ok, uint8_t err = 0)
    {
        std::vector<uint8_t> a = { 1, op, 4, 0, 0xEE, 0xEE, 0xEE, 0xEE, (uint8_t)(ok ? 0 : 1), err, 0, 0 };
        rxq.push_back(a);
    }
    void send_packet(const uint8_t *p, uint32_t n)
    {
        if (!in_loader || n < 8 || p[0] != 0) { saw_bad_order = true; return; }
        const uint8_t op = p[1];
        const uint32_t len = p[2] | (p[3] << 8);
        if (n != 8 + len) { saw_bad_order = true; return; }
        const uint8_t *d = p + 8;
        if (op < 32) op_count[op]++;
        // the loader hears a request at its own rate only: before the new rate is applied nothing at 2 Mbit/s is understood, after it nothing at 115200
        const bool rate_ok = baud_applied ? baud == ESPF_BAUD : baud == 115200;
        if (!rate_ok) { log.push_back("request at the wrong baud rate"); return; }
        if (op == 0x08) {
            if (no_sync || len != 36) return;
            synced = true;
            for (int i = 0; i < sync_answers; i++) answer(op, true);
            return;
        }
        bool inject = fail_cmd_op == op && op < 32 && op_count[op] == fail_cmd_nth;
        if (!synced) { saw_bad_order = true; answer(op, false, 0x05); return; }
        auto word = [&](uint32_t i) { return (uint32_t)d[4 * i] | ((uint32_t)d[4 * i + 1] << 8) | ((uint32_t)d[4 * i + 2] << 16) | ((uint32_t)d[4 * i + 3] << 24); };
        switch (op) {
        case 0x0D:
            if (len != 8) { answer(op, false, 0x05); return; }
            attached = !inject;
            answer(op, !inject, 0x05);
            return;
        case 0x0F:
            if (!attached || len != 8 || word(0) != ESPF_BAUD) { saw_bad_order = true; answer(op, false, 0x05); return; }
            baud_cmd_done = !inject;
            answer(op, !inject, 0x05);                    // the answer comes at the old rate; the link then sets the new one
            return;
        case 0x0B:
            if (!attached || !baud_applied || len != 24) { saw_bad_order = true; answer(op, false, 0x05); return; }
            params_set = !inject;
            answer(op, !inject, 0x05);
            return;
        case 0x02:
            if (!params_set || len != 20 || word(2) != ESPF_BLOCK || word(0) != word(1) * word(2) || (word(3) & 0xFFF)) { saw_bad_order = true; answer(op, false, 0x05); return; }
            if (inject) { answer(op, false, 0x05); return; }
            begun = true;
            begin_total = word(0); begin_blocks = word(1); begin_addr = word(3); expect_seq = 0;
            begin_addrs.push_back(begin_addr);
            if ((uint64_t)begin_addr + begin_total > flash.size()) { answer(op, false, 0x06); return; }
            memset(&flash[begin_addr], 0xFF, begin_total);
            answer(op, true);
            return;
        case 0x03: {
            if (!begun || len != 16 + ESPF_BLOCK || word(0) != ESPF_BLOCK) { saw_bad_order = true; answer(op, false, 0x05); return; }
            uint32_t seq = word(1);
            if ((int)seq == drop_data_seq) return;
            uint8_t chk = 0xEF;
            for (uint32_t i = 0; i < ESPF_BLOCK; i++) chk ^= d[16 + i];
            if (inject || chk != p[4] || seq != expect_seq || seq >= begin_blocks) { answer(op, false, chk != p[4] ? 0x07 : 0x05); return; }
            memcpy(&flash[begin_addr + seq * ESPF_BLOCK], d + 16, ESPF_BLOCK);
            if (corrupt_flash_data && seq == 3) flash[begin_addr + seq * ESPF_BLOCK] ^= 0x10;
            expect_seq++;
            blocks_ok++;
            answer(op, true);
            return;
        }
        default:
            answer(op, false, 0x05);
        }
    }
};

#endif
