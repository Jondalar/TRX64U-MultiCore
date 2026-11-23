// The board the application finds itself on (T104.2 D2, D3). The same image runs on the MultiCore host and on the TRX64 Ultimate
// monolith; what differs is answered by the hardware, once, before the descriptor is chosen. Pure: the reads are a BoardProbe, so
// tests/coreapp/ flips them.
#ifndef COREAPP_BOARD_H
#define COREAPP_BOARD_H

#include <stdint.h>

enum BoardKind { BOARD_MULTICORE = 0, BOARD_MONOLITH = 1 };

// The ID byte the ICAP loader of the MultiCore host answers at ICAP_BASE (0x10060600). The monolith decodes nothing there and reads 0.
#define BOARD_ICAP_ID 0x7A
// The longest core id a standalone installation's flash directory is named after ("/flash/<id>", board_set_core).
#define BOARD_CORE_ID_MAX 31
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
    virtual uint8_t icap_id() = 0;           // the ID register of the block icap of the driver table (hw.cc); 0 when the app is linked against a TOML A without that block: standalone by its link
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
    char cfg_dir[8 + BOARD_CORE_ID_MAX + 1];   // where <folder>.cfg lives when there is no hand-over path: "/flash/<core id>", set by board_set_core when flash_cfg; empty: in memory only
    char boot_cfg[8 + BOARD_CORE_ID_MAX + 10]; // the file that holds the boot-attempt counter (libguard.h): "/flash/<core id>/boot.cfg" (board_set_core) or the host's "/Flash/boot.cfg"; empty until the core is known
    const char *rom_dir;         // searched first for the ROM files, as a directory; null: only <root>/roms/<folder>
    uint32_t probes;             // BC_* the hardware answered for
    char why[96];                // "ICAP absent, no hand-over", for the log line
};

BoardProfile board_detect(BoardProbe *p);

// T239.3 P1-5: the standalone paths come from the core's description record. Called once the core is chosen, with its id (CoreDesc::core_id, the .cor id of
// core.toml) and its settings folder (CoreDesc::folder). On the monolith: cfg_dir "/flash/<id>", boot_cfg "/flash/<id>/boot.cfg", and those two files, "<cfg_dir>/<folder>.cfg"
// and boot_cfg, are what fs_flash_exception lets through the flash guard (fsif.h). A null, empty or malformed id (anything but A-Z a-z 0-9 _ -, 1 .. BOARD_CORE_ID_MAX) leaves
// the settings in memory and registers nothing; the result is false for a malformed one. A folder that is null or malformed registers no settings file. The MultiCore profile is
// not touched (its settings live beside the .cor, its counter in the host's file).
bool board_set_core(BoardProfile *b, const char *core_id, const char *folder);

const char *board_name(const BoardProfile &b);

// The descriptor's capability names that this board keeps: a name with a probe stays only when the probe holds, any other name
// stays. At most outmax entries go to out (pointers into `in`); returns the count. `struck`, when given, gets the names that went,
// separated by spaces.
int board_effective_caps(const BoardProfile &b, const char *const *in, int nin, const char **out, int outmax, char *struck, int struck_len);

#endif
