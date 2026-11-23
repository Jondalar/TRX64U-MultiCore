// The C64 descriptor on the C64 model with the monolith's profile: the pieces every C64 bench needs.
#ifndef TESTS_COREAPP_C64_RIG_H
#define TESTS_COREAPP_C64_RIG_H

#include "c64_model.h"
#include "boot.h"
#include "board.h"
#include "cores.h"
#include "settings.h"
#include "ops_glue.h"
#include "trx64u_menu.h"

struct RigProbe : BoardProbe {
    uint8_t icap = 0;
    uint8_t icap_id() { return icap; }
    uint32_t fpga_caps() { return BOARD_FPGA_DRIVE_1541_1 | BOARD_FPGA_COMMAND_INTF; }
    uint8_t trx_caps() { return BOARD_TRX_CAPS_GATE | BOARD_TRX_CAPS_STATEBUS | BOARD_TRX_CAPS_ENGINE | BOARD_TRX_CAPS_CART; }
    uint8_t iec_version() { return BOARD_IEC_VERSION; }
    bool handover_valid() { return false; }
};

// ROM images with a pattern per role, so a byte at the wrong offset is a wrong byte
static inline std::vector<uint8_t> c64_rom(int role, size_t n)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + role * 61 + (i >> 8));
    return v;
}

// A boot that waits for a ROM that never comes would hang a bench whose product source was broken on purpose; this one says so and stops.
struct BoundedUi : BootUi {
    int n = 0;
    void message(const char *, const char *, bool) { }
    void clear() { }
    void tick()
    {
        if (++n > 300) {
            printf("FAIL boot_start was still waiting for a ROM after %d passes\n", n);
            exit(1);
        }
    }
};

static inline const char *ev_first(const std::vector<std::string> &e) { return e.empty() ? "(none)" : e.front().c_str(); }
static inline const char *ev_at(const std::vector<std::string> &e, size_t i) { return i < e.size() ? e[i].c_str() : "(none)"; }
static inline const char *ev_last(const std::vector<std::string> &e) { return e.empty() ? "(none)" : e.back().c_str(); }

struct C64Rig {
    C64Model io;
    FakeFs fs;
    PxRec px;
    RigProbe probe;
    BoardProfile board;
    CoreCtx c;
    Settings *st;
    Video *vid;
    std::vector<uint8_t> basic, kernal, chars;

    explicit C64Rig(bool monolith = true, const char *cfg = "/flash/trx64u/c64.cfg") : basic(c64_rom(1, 8192)), kernal(c64_rom(2, 8192)), chars(c64_rom(3, 4096))
    {
        probe.icap = monolith ? 0 : 0x7A;
        board = board_detect(&probe);
        board_set_core(&board, c64_desc.core_id, c64_desc.folder);
        memset(&c, 0, sizeof(c));
        c.desc = trx64u::desc();                    // the C64's menu is its record (T104.2 D10): tasks, file types, media and settings come from there
        c.merge = trx64u::merge();
        c.io = &io;
        c.fs = &fs;
        c.board = &board;
        st = new Settings(c.desc->settings, c.desc->nsettings, c.desc->store, &fs, cfg, 0);
        st->load();
        c.settings = st;
        vid = new Video(c64_desc.video, &px, &io);
        c.video = vid;
    }
    void put_roms(const char *dir = "/flash/roms", const char *b = "basic.bin", const char *k = "kernal.bin", const char *ch = "chars.bin")
    {
        fs.put(std::string(dir) + "/" + b, basic);
        fs.put(std::string(dir) + "/" + k, kernal);
        fs.put(std::string(dir) + "/" + ch, chars);
    }
    // The menu holds the machine (C64_STOP) while an operation of it runs and lets it go when it closes; the service the descriptor names is
    // polled from then on, every 50 ms of the machine's time (service.h), until the job has been carried out.
    void menu_op_begin() { io.stop(true); }
    void menu_closed(int max_ms = 15000)
    {
        io.stop(false);
        for (int t = 0; t < max_ms && ops_state(&c, "c64.pending"); t += 50) {
            io.delay_ms(50);
            OpResult x;
            ops_call(&c, "c64.service", 0, 0, &x);
        }
    }
    // the machine up: ROMs in, released, and the KERNAL given the time it needs
    bool boot()
    {
        BoundedUi ui;
        BootResult r = boot_start(&c, &ui);
        io.delay_ms(io.boot_ms + 50);
        return r == BOOT_RUNNING && io.booted;
    }
};

#endif
