// The board the application finds itself on (T104.2 D2, D3). The same image runs on the MultiCore host and on the TRX64 Ultimate
// monolith; what differs is answered by the hardware, once, before the descriptor is chosen. Pure: the reads are a BoardProbe, so
// tests/coreapp/ flips them.
#ifndef COREAPP_BOARD_H
#define COREAPP_BOARD_H

#include <stdint.h>

enum BoardKind { BOARD_MULTICORE = 0, BOARD_MONOLITH = 1 };

// The ID byte the ICAP loader of the MultiCore host answers at ICAP_BASE (0x10060600). The monolith decodes nothing there and reads 0.
#define BOARD_ICAP_ID 0x7A
// The directory of the settings file when there is no .cor beside which it could live (Alex, 2026-10-18, T104.2 Q1), and the only
// place of the flash the application may write (fsif.h: fs_flash_exception).
#define BOARD_MONOLITH_CFG_DIR "/flash/trx64u"
// Gideon's layout: the ROM files lie in this directory itself, not in a roms/<folder> below it.
#define BOARD_MONOLITH_ROM_DIR "/flash/roms"

// What the machine may have, by the names of a descriptor's capability list (T104.2 D3).
enum BoardCap {
    BC_DRIVE_A     = 1u << 0,
    BC_IEC         = 1u << 1,
    BC_CARTRIDGE   = 1u << 2,
    BC_REU         = 1u << 3,
    BC_RING        = 1u << 4,
    BC_UCI         = 1u << 5,
    BC_SID_SOCKETS = 1u << 6
};

// The CAPS bits of the TRX register block (trx_io.h) and the ITU capability word (itu.h) the probes read. Repeated here so the bench
// does not need the board headers; hw.cc includes both and the numbers are the same.
#define BOARD_TRX_CAPS_GATE     0x01
#define BOARD_TRX_CAPS_STATEBUS 0x02
#define BOARD_TRX_CAPS_ENGINE   0x04
#define BOARD_TRX_CAPS_CART     0x10
#define BOARD_FPGA_DRIVE_1541_1 0x00000002u
#define BOARD_FPGA_COMMAND_INTF 0x00040000u
#define BOARD_IEC_VERSION       0x25

// Every read the profile is made of. The board's implementation is hw.cc.
class BoardProbe {
public:
    virtual ~BoardProbe() { }
    virtual uint8_t icap_id() = 0;           // ICAP_BASE + 0
    virtual uint32_t fpga_caps() = 0;        // the ITU capability word
    virtual uint8_t trx_caps() = 0;          // TRX_CAPS
    virtual uint8_t iec_version() = 0;       // IEC_BASE + 0
    virtual bool handover_valid() = 0;       // the page at HANDOVER_ADDR holds a block that handover_valid() accepts
};

struct BoardProfile {
    BoardKind kind;
    bool has_handover;           // the page at HANDOVER_ADDR is valid (sum and magic). On the monolith only the legacy start of a large .cor writes
                                 // one (T112 Q1); a .cfw start wipes it, so a block that passes is ours and carries a .cor path.
    bool can_return_host;        // the ICAP loader is there; "Return to host menu" exists only then
    bool flash_cfg;              // T228 DC13: a start without a .cor keeps its settings in a cfg file in the flash. Off under the MultiCore host (D11: the menu writes and
                                 // erases nothing in the flash); on for a standalone installation, where nothing else could persist them
    const char *cfg_dir;         // where <folder>.cfg lives when there is no hand-over path (the flash directory when flash_cfg); null: in memory only
    const char *boot_cfg;        // the file that holds the boot-attempt counter (libguard.h); in the flash, one of the two paths fs_flash_exception lets through
    const char *rom_dir;         // searched first for the ROM files, as a directory; null: only <root>/roms/<folder>
    uint32_t probes;             // BC_* the hardware answered for
    char why[96];                // "ICAP absent, no hand-over", for the log line
};

BoardProfile board_detect(BoardProbe *p);

const char *board_name(const BoardProfile &b);

// The descriptor's capability names that this board keeps: a name with a probe stays only when the probe holds, any other name
// stays. At most outmax entries go to out (pointers into `in`); returns the count. `struck`, when given, gets the names that went,
// separated by spaces.
int board_effective_caps(const BoardProfile &b, const char *const *in, int nin, const char **out, int outmax, char *struck, int struck_len);

#endif
