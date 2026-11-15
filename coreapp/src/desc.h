// The per-core descriptor (T93 D3). One const CoreDesc per core, in cores/<name>.cc, next to the register constants of that
// core's gateware. A field that does not apply is null/zero, never a stub; desc_check.cc checks the mandatory
// ones. Everything the descriptor makes the machine do is an operation *by name* (T104.1 D1): a ROM's put, a setting's apply, a
// task, a file action, a slot's eject. The core's contributor registers the operations (register_ops); desc_check refuses a
// name that is not registered or a binding that does not fit its arguments. The handlers run against CoreIo / CoreFs only, so
// they run natively in tests/coreapp/ against a model of the core.
#ifndef COREAPP_DESC_H
#define COREAPP_DESC_H

#include <stdint.h>
#include <string.h>
#include "io.h"
#include "fsif.h"
#include "ops.h"

struct CoreCtx;
class OpRegistry;
struct RomSetSpec;
struct WindowMediaSpec;
struct SectorMediaSpec;
struct MouseSinkSpec;
struct RawKeySpec;
struct FbWinSpec;
struct PadSpec;

// The control hooks (init, release, warm_reset) are still functions: they are the core's own sequence, called by the boot code, never by name.
// A hook returns 0 or a reason in msg (at most msglen bytes, shown on screen and logged).
typedef int (*core_hook_t)(CoreCtx *c, char *msg, int msglen);

// ---- ROMs (D5) -----------------------------------------------------------------------------------------------------
#define ROM_NAMES_MAX 10
#define ROM_SIZES_MAX 3
#define ROMS_MAX 14                           // the most ROMs one descriptor lists: the scan's result arrays (RomResult[ROMS_MAX]) hold this many; desc_check refuses more
struct RomSpec {
    const char *role;                     // "XL OS", for the screen
    const char *names[ROM_NAMES_MAX];     // first match wins, case-insensitive, null-terminated list
    uint32_t sizes[ROM_SIZES_MAX];        // the allowed sizes, 0-terminated
    bool required;
    bool (*check)(const uint8_t *data, uint32_t size);   // optional: content check (e.g. a reset vector)
    // Where it goes: the operation called once the file is chosen and checked, the core held. It takes one argument, the bytes
    // (a blob), and returns OPE_OK or a reason in the message.
    const char *put_op;
    // T91/T92: ROMs that belong to one machine variant (the Spectrum 48K and 128K, the CPC 464, 664 and 6128). `set` is a bit mask, bit n =
    // a member of set n + 1 (a ROM two variants share has two bits). A ROM with a non-zero `set` is not required on its own, and the core may
    // start when ONE set is complete (every ROM that has its bit found). Zero: a plain ROM, required or not as `required` says. The put_op of
    // a member still runs for every file found.
    uint8_t set;
    // A ROM of a shared device (the 1541 drive: the C64 side keeps it in <root>/roms too, Alex 2026-10-23): searched in the core's own folder
    // first, then in the shared <root>/roms of every root. A machine ROM (KERNAL, BASIC ...) never sets it: the C64's file there is not
    // the C16's.
    bool shared;
};

// ---- settings (D10) --------------------------------------------------------------------------------------------------
enum { WHEN_LIVE = 0, WHEN_RESET = 1 };

// T104 5.2 "setting, extended": a setting that is bits of a register byte instead of an operation of its own (regset.h). The Amiga's MEM_CFG
// holds two settings (chip [1:0], slow [3:2]) and FLOPPY_CFG two more; the byte is the OR of what every bound setting contributes. A value
// contributes `bytes[value]` when the table is given, else `value`, masked with `mask` and shifted left by `shift`.
struct RegBind {
    uint16_t reg;                         // the core's register (offset in the register block)
    uint8_t shift;
    uint8_t mask;                         // applied before the shift
    const uint8_t *bytes;                 // value -> byte, nvalues entries; null: the value itself
};
// Fixed bytes written with every settings commit that are not settings (T104 5.2 `reset_write`): CPU_CFG = 0, IDE_CFG = 0x21.
struct RegWrite {
    uint16_t reg;
    uint8_t value;
};
// The one key a descriptor may not declare: the application adds the item itself (picture.h, T107).
#define PICTURE_KEY "Picture"
// T160: the setting the menu's SHIFT+ / SHIFT- step by one value (menuctl.cc): the item with this key and a register binding. A core without it ignores the keys.
#define VOLUME_KEY "Volume"
#define SCALE_KEY   "Scale"                // T107 D8: added with Picture to every frame-buffer core on a host whose gateware scales (T164 G14)
#define SMOOTH_KEY  "Smoothing"            // T164 Q3: added with Scale
struct Setting {
    const char *key;                      // the item text, also the key in <name>.cfg
    const char *const *values;            // labels, also the values in <name>.cfg
    int nvalues;
    int def;                              // index of the default (the layer below the flash and the .cfg)
    uint8_t when;                         // WHEN_LIVE: applied at once; WHEN_RESET: written while the core is held
    const char *apply;                    // the operation that puts a value into the core: one int or enum argument "value"
    // Or the register bits it is (regset.h): then `apply` is null. A third form has neither: the item that chooses the member of the
    // core's rom_set (RomSetSpec::choice_key); it is applied by the reload, not by a register.
    const RegBind *bind;
    // T104.1 D4 (a contributed range setting, 8 .. 11): the operation gets `base + index`, the menu shows base + index too. 0 for every list.
    int32_t base;
    // T104.2 D10: the group of a setting that came from the core's description record, which the tab lists by `groups`; null: the tabs that list none
    // (a descriptor of C++ names none, so a core without a record lists every setting where it always did).
    const char *group;
    // T104.2 UI: 0 a choice among `values`; 1 a text the person types (values[0] is its default, `base` its longest length); 2 the same, shown as stars;
    // 3 (T185 D4) a file the person selects in a file selector (RETURN on the row): the text is the file's path (values[0] "" = none selected), `apply` is
    // the operation that checks a chosen path and keeps it (one text argument "path", "" clears), it is never applied by a reset
    uint8_t kind;
    // T104.2 UI: the row is listed only while this condition holds (contrib.h cond_holds over the state values); null: always
    const char *show_state;
    // T104.2 UI: the name the menu shows; null: the key. The key is what the .cfg, REST and the operations name and never changes with a label.
    const char *label;
};

// ---- tasks, file types, media (D8) -------------------------------------------------------------------------------------
// An entry that calls an operation fills its arguments from `bind` (T104.1 D4): the file the menu is on, or constants. Every
// argument of the operation is bound exactly once.
struct Task {
    const char *label;
    const char *op;
    const OpBind *bind; int nbind;
    bool closes_menu;                     // Reset, Reload: the menu closes and the machine runs (S4)
    // T104.1 D15 scope: a task that asks for a name (Create, Save). `ask` is the prompt; the text goes to the argument that a B_TEXT binding names.
    // `ask_state` is a state value whose text prefills the prompt (a free name); null: it starts empty. Null `ask`: the task asks nothing.
    const char *ask;
    const char *ask_state;
    // T122 §5: a task that makes a new file (Create blank ...) asks for the folder first (a folder browser, "Create here"), then the name, and
    // runs in steps with a progress display. The operation has to take CoreCtx::job_step (newdisk.cc is the pattern).
    bool pick_dir;
    // The first row of that folder browser: null "Create here"; a task that mounts a folder says "Use this folder" (T128).
    const char *pick_label;
    // T104.2 D10: the group (a tasks tab lists `groups`) of a task from the description record; null as above.
    const char *group;
};

struct FileAction {
    const char *label;                    // "Mount D1:"
    const char *op;
    const OpBind *bind; int nbind;
    bool closes_menu;
    // T104.2 D6: an action that asks the person for a 16 bit address (hex) before it runs. `ask` is the prompt ("Start address"), the
    // value goes to the argument that a B_ASK binding names. The field starts on the little endian word at file offset `ask_off`
    // (the PRG's own load address: 0, or 0x1A behind a .P00 header) so RETURN alone takes it. Null: the action asks nothing.
    const char *ask;
    int16_t ask_off;
    // T104.1 D4 `size_in`: the action is offered for a file of exactly one of these sizes (a contributed "Set as 1541 ROM"); null: any size.
    const uint32_t *size_in;
    int nsize_in;
    // T104.2 UI: the letter that runs the item inside the action sheet (0: none). Uniform browser: the same letters in the host and in every core.
    char key;
};

struct FileType {
    const char *ext;                      // ".atr", lower case with the dot
    const FileAction *actions;
    int nactions;
    // T104.1 D15 scope: a disk image is a folder for the browser (cores/cbm1541_desc.h, d1541_fs.h). `inner` are the actions of the files inside it
    // whose name ends in `inner_ext` (".prg"); the browser lists only those. An action with the operation "menu.browse" enters the image.
    const char *inner_ext;
    const FileAction *inner; int ninner;
};

struct MediaSlot {
    const char *name;                     // "D1:", for the status page and the golden list
    const char *state;                    // the state value that says what is in it right now (text), empty: nothing
    const char *eject_op;                 // optional; filled from eject_bind like a task
    const OpBind *eject_bind; int neject_bind;
    const char *group;                    // T104.2 D10: as Task::group
};

// ---- video, freeze (D9) -------------------------------------------------------------------------------------------------
struct VideoDesc {
    uint32_t w, h;                        // source pixels, rows of w * bytes per pixel (bpp below)
    uint8_t scale;                        // 1..3
    int32_t x0, y0;                       // position of the layer on the 1920 x 1080 output
    uint32_t fb_base;                     // DDR2 byte address of buffer 0 (the core's FB_BASE register value)
    uint32_t fb_stride;                   // bytes between the buffers
    uint8_t nbuf;                         // 4
    uint16_t reg_latest, reg_seq;         // FB_LATEST (b7 valid, b1:0 buffer), FRAME_SEQ
    uint16_t reg_fb_base, reg_fb_stride;  // FB_BASE, FB_STRIDE (32 bit, little endian): the app writes the two values above
    uint8_t bpp;                          // bytes per pixel: 0 or 2 = RGB565 (the layer's format 1), 4 = 32 bit {x, R, G, B} (format 2:
                                          // only the A1200 build's own pixel layer has it, T136 D1)
    uint32_t pixel_bytes() const { return bpp == 4 ? 4 : 2; }
    // T194: a picture that is not a frame buffer but the gateware's own capture. pxfmt is the layer's format (3 = the VIC's capture, 4 bit indexed, rows of
    // 13 words for 400 pixels: stride 16 * ceil(w / 32)); no buffers (nbuf 0), nothing to flip. h60 is the height in the 60 Hz family when it differs (0: h).
    // Last in the record, so the positional initialisers stay as they are.
    uint8_t pxfmt;
    uint16_t h60;
    uint32_t stride_bytes() const { return pxfmt == 3 ? 16 * ((w + 31) / 32) : w * pixel_bytes(); }
};

struct FreezeDesc {
    uint16_t reg_status;                  // the register with the PAUSED bit
    uint8_t mask;                         // ... and its bit
    uint16_t timeout_ms;                  // 200 (S4)
    // T104.2: the register is a byte of a 32 bit word of the TRX block (trx_sys.v): bytes 1-3 come from the latch that a read of byte 0
    // fills, so a byte read alone sees whatever the last read of that word left. Then the status is read as the word is: byte 0 first.
    bool word;
};

// ---- the slots of a core's own media engine (T180 D2, D5) ----------------------------------------------------------------------
// The window drives and sector units of the base are mounted by the base and know their slots; an engine with a mount state of its own (the Apple's
// floppies and slot 7, the Atari's drives and cartridge, the CPC's drives and tape, the drive library, a cartridge port, a tape deck, a game
// cartridge) lists its slots here, so "Persist medium mounts" and the media sets reach them through the same keys ("Mount <name>=<spec>"). A spec is
// the words the file uses for what is in the slot: the path of the medium, with a word in front when the engine needs one ("ro /usb0/x.d64",
// "folder <dir>/<set>"); a path always starts with '/'. The engine also calls mount_notify (ctx.h) whenever one of its slots changes.
struct MountSlots {
    int n;
    const char *const *names;             // n key words, 1..9 characters ("Mount " and the word are one key of 15 at most), as the menu names the slot without its colon ("D1", "S7.1", "Cartridge")
    // What is in slot i, null when empty. The text stays valid until the next call.
    const char *(*spec)(CoreCtx *c, int i);
    // Mounts `spec` into slot i, replacing what is there (D1: the old medium is written back and let go first). 0, or 1 and the reason in msg; a slot that
    // cannot take the medium keeps what it held. While c->media_restoring is set the machine is held and is released later by the caller: no reset, no release.
    int (*mount)(CoreCtx *c, int i, const char *spec, char *msg, int msglen);
    // Empties slot i (written back first). 0, or 1 and the reason in msg (the medium stays).
    int (*eject)(CoreCtx *c, int i, char *msg, int msglen);
};

// ---- the core ---------------------------------------------------------------------------------------------------------------
// T162 D3: one register of the core's state block in the bug report - a name and a place, nothing else. Only registers whose read changes nothing
// belong in the list (status, configuration, counters); never a FIFO, a pop or a mailbox. `bytes` is 1, 2 or 4, little endian, at the core window.
struct ReportReg {
    const char *name;
    uint16_t reg;
    uint8_t bytes;
};
#define REPORT_REGS_MAX 24

struct CoreDesc {
    char id[4];                           // the ID block at the core window: "A8XL", "NES_"
    const char *cor_name;                 // the manifest's name (core.toml name); the handover must agree (S1)
    const char *title;                    // menu title
    const char *short_title;              // the name on the navigation rail, where 148 px is all there is ("NES", "ATARI")
    const char *folder;                   // roms/<folder> on the three roots, and the base of <name>.cfg's store name
    const char *store;                    // the [section] of <name>.cfg
    uint32_t min_contract;                // 1
    const RomSpec *roms; int nroms;
    const Setting *settings; int nsettings;
    const Task *tasks; int ntasks;
    const FileType *filetypes; int nfiletypes;
    const MediaSlot *media; int nmedia;
    VideoDesc video;
    FreezeDesc freeze;
    // control (D4). init holds the machine for the app (the host's power-up hold stays until release); release lets it go
    // and is the only place the app's own hold is taken away (and C64_MODE written). A cold reset is the shared sequence
    // hold, settings, wait, release (boot_cold_reset); a warm reset keeps the RAM and so is the core's own sequence.
    core_hook_t init;
    core_hook_t release;
    core_hook_t warm_reset;               // optional: a reset that keeps RAM
    uint16_t reset_ms;                    // how long the core is held in a reset the app makes (at least its RAM sweep)
    // service (T93 D3, T104.1 D6): an operation without arguments, called every period_ms by a poll task of its own (service.h), with the
    // menu open or closed, under the core lock. It sets "more" when it had work and wants to be called again at once (SIO frames come in
    // bursts). poll_when_frozen: it also runs while the machine is stopped for the menu; otherwise it waits for the menu to close.
    const char *service_op;
    uint16_t service_period_ms;
    bool poll_when_frozen;
    // what the start screen says when no program is loaded yet (the NES waits for a .nes)
    const char *needs_program;            // null: the machine boots by itself
    const char *program_ext;              // file extension the first-start file picker shows
    // the core's contributor: registers the operations and states the names above refer to (null: the core needs none)
    void (*register_ops)(OpRegistry &r);
    // what the machine has, by name (T104.1 Q10): "sio", "iec", "drive_a", "cartridge". A library whose capability is not in this list is
    // refused at the load, with a message that names the library and the capability; null/0: the core claims none. A name is 1..15 of
    // [a-z0-9_], as the field of a .u2a is; desc_check refuses the rest.
    const char *const *caps; int ncaps;
    // T104.2 D3: the names in `caps` are what the machine may have; the board strikes those its probes deny (board.h). Only the C64
    // descriptor sets it, whose hardware differs per bitstream; the Atari and the NES list what they have.
    bool board_caps;
    // ---- T104 5.2: the kinds of entry the Amiga needed, each one a data record the base serves (none of them is core code) ----
    // The ID block is hard-wired in the gateware that two bundles share (amiga and amiga1200): both descriptors set id_shared and the
    // bundle's name tells them apart (start.cc). Two descriptors with the same ID and no flag are still refused (desc_check_table).
    bool id_shared;
    const RegWrite *reset_writes; int nreset_writes;      // written with every settings commit (regset.h)
    const RomSetSpec *romset;                             // a ROM chosen out of a directory's files (romset.h)
    const WindowMediaSpec *window_media;                  // raw images in windows of the DDR2 with a dirty log (winslot.h)
    const SectorMediaSpec *sector_media;                  // images served sector by sector through a mailbox (sectors.h)
    const MouseSinkSpec *mouse;                           // a USB mouse delivered to the core as deltas (mousesink.h)
    const FbWinSpec *fbwin;                               // the picture window follows the chipset's display window (fbwin.h, T97 5.10)
    // T104.1 D15: the operation that resets the machine and types a script at READY - `text` is the pieces separated by '|', each at most ten
    // keys (the keyboard buffer), CR is '\r'. The drive library's Mount & Run is "mount, then this" (cbm1541.cc). Null: the core cannot type.
    const char *type_op;
    // The operation that resets the machine and runs the program at `path` (one argument "path"): "Mount & Run" of a PRG inside a disk image mounts
    // the image and then calls this with the path of the file inside it. Null: the core cannot.
    const char *run_op;
    // "Restart core" (F8, a cold boot of the core without the host): the machine RAM, in bytes from address 0, that the application clears
    // through the RAM window while the core is held, as a power-on leaves it. 0: the application cannot clear this machine's RAM and the
    // restart leaves it as it is.
    uint32_t cold_ram_bytes;
    // T104 5.14: the cold start of a core that has no partial bitstream to load again (a flat build, the C128's .cfw): "Restart core" calls it
    // before the restart sequence, so what the registers cannot reach is brought back by the core's own means. 0, or a reason in msg: the
    // restart then stops and the machine keeps running. Null: nothing to do (every core of today).
    core_hook_t cold_start;
    // T115 D9 (with id_shared): when no bundle names the machine (a firmware image started as .cfw), the descriptor whose register
    // id_feat_reg, masked with id_feat_mask, reads id_feat_val, if exactly one does (the A1200's gateware has AM_FEAT_FBWIN, the A500's
    // has not). Mask 0: not used. Last in the record, so the positional initialisers of the other descriptors stay as they are.
    uint16_t id_feat_reg; uint8_t id_feat_mask, id_feat_val;
    // T104.2 D10: the menu record the application carries for a start that has no .cor (JTAG, ue2emu): the bytes tools/descrec.py makes from the core's
    // menu.toml, the same ones the packer puts into the .cor. A core with one has no menu tables above - the menu comes from the record alone.
    const uint8_t *menu_rec; uint32_t menu_rec_n;
    // T115 7 H2: the core's id as core.toml names it ("amiga", "amiga1200"), which a .cor's manifest carries (large: "id") and the hand-over hands
    // on. start_identify compares it, case-insensitive, when both the hand-over and the descriptor have one; the display name (cor_name)
    // is the fallback. Null: the descriptor is chosen by name only.
    const char *core_id;
    // T162 D3: the registers the bug report names (desc_check: names, widths, at most REPORT_REGS_MAX). Last in the record. Empty: the report says
    // that this core declares none.
    const ReportReg *report_regs; int nreport_regs;
    // T141: a USB keyboard delivered to the core as its own key events (rawkeys.h). Last in the record, so the positional initialisers stay as they are.
    const RawKeySpec *rawkeys;
    // T160 D1 / T164 WP11: the core has a master volume of its own (the C64's mixer, P4 of T160), so the application adds no generic "Volume" item and the
    // volume keys do nothing. Last in the record, so the positional initialisers stay as they are.
    bool own_volume;
    // T156: USB gamepads drive the machine's own controllers: the registers of the two players and the core's buttons with their default
    // map (pad.h). Null: the core has no pad support; reports of a pad are taken and dropped.
    const PadSpec *pad;
    // T175: a core with no machine and no picture (the diag core): its application's screens are all there is. `menu_first` opens the menu after the start and
    // RUN/STOP does not close it; `live_ms` redraws the open menu that often, so state rows (the pins of the ports) are live. Last in the record, so the positional
    // initialisers of the other descriptors stay as they are.
    uint16_t live_ms;
    bool menu_first;
    // T180 D4: a core whose media engine keeps its own write-back (not the window drives / sector units of the base, not the cartridge): `media_flush` writes
    // everything pending back, completely, and answers 0, or 1 with the reason in msg (the medium stays mounted); `media_pending` says whether there is
    // something to write (the "Saving media..." display). media_writeback (engines.h) calls both, so every exit that lets a medium go (reset, reload,
    // restart, return to host, power) reaches them without code of its own. Last in the record, so the positional initialisers stay as they are.
    core_hook_t media_flush;
    bool (*media_pending)(CoreCtx *c);
    // T180 D2, D5: the slots of this core's own media engine (MountSlots above), for "Persist medium mounts" and the media sets. Null: the core has none
    // (the base's window drives and sector units need no entry). Last in the record, so the positional initialisers stay as they are.
    const MountSlots *mount_slots;
    // T181: the hotkeys while the core runs (hotkeys.h): the default map, the keys the core's volume and joystick items are named by, the keys that are
    // the core's own functions. Null: the core has no hotkeys. Last in the record, so the positional initialisers stay as they are.
    const struct HkSpec *hotkeys;
    // T194: a second picture of the core on the same layer (the C128's VIC-IIe next to the VDC): Video::select_picture() switches to it; the Picture, HDMI mode and
    // Smoothing settings reach both. Null: one picture. Last in the record, so the positional initialisers stay as they are.
    const VideoDesc *alt_video;
    // T189: the core's build has no tone filter (host/trx_tone.v is not in it: neogeo until its flat build places), so the application adds no "Tone" item.
    // Last in the record, so the positional initialisers stay as they are.
    bool no_tone;
};

static inline bool desc_has_capability(const CoreDesc *d, const char *cap)
{
    for (int i = 0; d->caps && i < d->ncaps; i++) {
        if (!strcmp(d->caps[i], cap)) {
            return true;
        }
    }
    return false;
}

// The table (cores/table.cc): null-terminated.
extern const CoreDesc *const core_table[];
const CoreDesc *core_find(const char id[4]);
const CoreDesc *core_find_by_name(const char *cor_name);

#endif
