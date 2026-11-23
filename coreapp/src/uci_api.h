// The export "uci" (major 1): the Ultimate Command Interface at $DF1C-$DF1F, the shared UCI library of T239.3 D12 (today coreapp/src/uci.cc and uci.h). A library of this
// repository puts the table into LibRecord::exports (base 3.2); a core's library finds it with SvcLib::find("uci", U2A3_VERSION(1, 0), &size). Not a member of LibApi.
//
// Who owns the $DF1C handling: this library. The registers are the UCI's own (CMD_IF_BASE + 0 SLOT_BASE, + 1 SLOT_ENABLE), so the three slot calls are here and nobody else
// writes them. trx64u_uci_df1c of the old code (called by c64_cart.cc when a cartridge asks for the command interface) is gone: the cartridge library calls
// set_slot_enable(1) and set_slot_base(0x47) of this table, and the ring library reads and sets the slot for a .c64re with the same calls. No call into the TRX64-Ultimate
// libraries remains. The test the old function made (`desc_has_capability("uci")` and the board probe BC_UCI) is made here: see the slot calls.
//
// Why no CoreCtx: the functions of uci.h take one; the library behind this table has the api groups and uses them. The callbacks of the host table (UciHooks) follow: they lose it too.
//
// Rules: `size` first; members only appended inside a major. Task: "lock" = a task holding the core lock.
#ifndef COREAPP_UCI_API_H
#define COREAPP_UCI_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DRVTYPE_MP3_DNP
#define DRVTYPE_MP3_DNP 31                    // io/c64/c64.h, the RAM drive type of a MegaPatch 3 native drive
#endif

// The machine's side of the interface: what only the core knows. The members are those of UciHost in uci.h without the context argument and with `size` first; the type has the
// name UciHooks because an export header's types carry the prefix of their table and never reuse a name of an internal header (uci.h keeps UciHost until 3.3 converts it).
// The table stays valid for as long as the library is loaded (a static const of the caller). Every callback runs on the UCI's own task. A null callback answers as "the machine has none" (reu: -2; drive_info: false; ef_clear: false; palette, ramdrive: nothing).
typedef struct UciHooks {
    uint32_t size;                        // sizeof(UciHooks) of the caller; members past it count as null
    // The REU, for CONTROL LOAD_REU / SAVE_REU: 0 and *size, *offset (bytes) and the image path in img; -2 the REU is not enabled; -4 its memory is in use (busy has the reason).
    int (*reu)(uint32_t *size, uint32_t *offset, char *img, int imgn, char *busy, int busyn);
    // Drive idx (0 = A, 1 = B): false when there is none. type in the encoding of the drive's type register (0 1541, 2 1581), addr the IEC address.
    bool (*drive_info)(int idx, uint8_t *type, uint8_t *addr, bool *power);
    // what: 0 mount path, 1 eject, 2 swap, 3 power off, 4 power on. 0 = done, else the reason is in msg.
    int (*drive_do)(int idx, int what, const char *path, char *msg, int msglen);
    // CONTROL GET_HWINFO device 1 (the SID map): fills data (at most 16 bytes) as the firmware does and returns its length, 0 when the machine has none.
    int (*sid_map)(uint8_t *data);
    // CONTROL U64_SAVEMEM: the file the 64 KB go to when the command names none; null: the library's default.
    const char *memory_file;
    // CONTROL EASYFLASH 0: clears the 8 banks of 8 KB of the cartridge in the port from `bank` (a multiple of 8), the ROMH half when high. False: no cartridge. The caller usually
    // forwards to c64cart.ef_clear.
    bool (*ef_clear)(uint8_t bank, bool high);
    // The MegaPatch 3 RAM drive n (0..3) in the REU: type 0 none, 1541, 1571, 1581 or DRVTYPE_MP3_DNP; size in bytes; base the REU offset of its first byte.
    void (*ramdrive)(int n, int *type, uint32_t *size, uint32_t *base);
    // The palette (CONTROL 0x51-0x54): what 0 reads all 16 colours into rgb, 1 sets all, 2 sets colour idx from rgb[0], 3 resets to the default colours.
    void (*palette)(int what, uint8_t idx, uint8_t (*rgb)[3]);
} UciHooks;

typedef struct UciApi {
    uint32_t size;
    // Starts the command interface once, with the machine's side (`hooks`). It does nothing when the bitstream lacks it (the UCI and cartridge capability bits of the ITU) and on a second
    // call. Call it from the core's start hook (core_hooks.h), never from lib_register. Task: lock.
    void (*start)(const UciHooks *hooks);
    // The slot of the interface: whether it answers at $DF1C (SLOT_ENABLE, 0 or 1) and the base address it answers at (SLOT_BASE; 0x47 is $DF1C, the cartridge library's
    // value). slot_enable reads the register; set_slot_enable and set_slot_base write it. The rule of this export: the writes are ignored and slot_enable reads 0 unless
    // sys.has_capability("uci") is true. That call tests the running core's descriptor after the board struck what its probes deny (run.cc, board_effective_caps), which is the
    // test of trx64u_ops.cc (desc_has_capability(desc, "uci") and the probe BC_UCI) on every core that lists "uci" in its capabilities and sets board_caps. That is the one C64
    // descriptor today (c64.cc), and the run loop always has a board profile, so the old test's case of no profile never arises. A descriptor that lists "uci" without board_caps
    // would skip the probe, so such a descriptor sets board_caps. Task: lock.
    uint8_t (*slot_enable)(void);
    void (*set_slot_enable)(uint8_t v);
    void (*set_slot_base)(uint8_t v);
} UciApi;

#ifdef __cplusplus
}
#endif

#endif
