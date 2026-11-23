// The description record of a .cor (T104.1 D9, D12): the TOML description compiled at pack time (tools/cor_pack.py, tools/descrec.py) into a flat,
// pointer-free record that is read by offsets and nothing else - the board does not parse text. It carries what a core says about its menu
// without being code: the route (areas, tabs, widgets), its own nodes, file actions and settings, and what it hides or renames of other
// contributors' nodes. The reader checks every offset, count and string before it hands out a pointer, so a damaged or hostile record is
// refused with a sentence, never followed.
//
// Layout, little endian, every entry fixed size, strings in one table (NUL terminated, offsets into it, 0xFFFFFFFF = none):
//
//   0   u32 magic "TRXD"        4  u16 version (7 TOML A; 6 with data sections; 5 with a pad or a restart, else 4; 2 and 3 are read too)   6  u16 header size (176; version 5: 208)
//   8   u32 total size         12  u32 flags (bit 0: the record gives the route; bit 1: it carries the core's own menu tables)
//   16  u32 string table offset, u32 string table size
//   24  (offset, count) of the tables, in this order, 8 bytes each:
//       area 20 | tab 24 | node 52 | file action 40 | setting 32 | hide 4 | rename 8 | bind 16 | ext 4 | size 4 | value 4 | contributed tab 24 |
//       task 36 | media 44 (24 before version 4) | file type 24 | file type action 36 | core setting 36
//   160 poll: u32 operation (string offset, none = no poll), u32 period in ms, u32 merged libraries (string offset of their names, comma separated, none = no
//       library menu was merged: the menus of those libraries are in this record, the application does not add them again), version 4: the machine-info
//       line (string offset of the settings whose values make Home's machine line, none = no line; before version 4: 0)
//   176 version 5 only (T228 R10, R17: a record with a pad or a restart; the writer keeps version 4 and 176 bytes for one without): the pad (pad.h PadSpec):
//       u32 kind (bits 7:0 the output, PAD_OUT_REGS / PAD_OUT_JOY, bit 8 by_id; none = no pad), u32 reg 0, u32 reg 1 (registers, or the ports 0 / 1), u32 the
//       first of its button names in the value table, u32 how many (1..16), u32 its default map (string offset: two hex digits per button, a source 00-23, a
//       role 40-47, FF none)
//   200 version 5 only: the restart (T228 R17, CoreDesc::cold_ram_bytes): u32 the machine RAM F8 "Restart core" clears, in bytes from address 0 (0 = the
//       core's own value; else a multiple of 256, at most 16 MiB), u32 flags (bit 0: the RAM window in its memory-only mode while it clears)
//
//   208 version 6 only (T239.3 D17: a record with the core's data sections; the writer keeps version 5 or 4 for one without): u32 offset and u32 size of the sections. They lie
//       behind the string table to the end of the record (flags bit 2). A section is u32 id, u32 length (a multiple of 4), the length's bytes; each at most once, an id
//       this reader does not know is refused with its number. Words are u32, strings offsets into the string table (none = 0xFFFFFFFF):
//         1 core       18 words, or 19: id (4 bytes), name, title, short_title, folder, store, min_contract, reset_ms, service_op, service_period_ms, flags (b0 poll_when_frozen, b1 board_caps,
//                      b2 id_shared, b3 own_volume, b4 no_tone, b5 menu_first), needs_program, program_ext, type_op, run_op, core_id, live_ms, caps (comma separated), and the 19th: hooks, the name of
//                      the library of the bundle whose export "corehooks" is the core's code (1 to 15 characters of a-z 0-9 _; none = 0xFFFFFFFF, and the writer then leaves the word out)
//         2 video      19 words: w h scale x0 y0 fb_base fb_stride nbuf reg_latest reg_seq reg_fb_base reg_fb_stride bpp pxfmt h60, then the freeze: reg_status mask timeout_ms word
//                      (scale 0 with every other video word 0: no picture of the core's own, the freeze alone)
//         3 roms       u32 count, then per ROM 16 words: role, put_op, flags (b0 required, b1 shared, bits 15:8 set), 3 sizes (0 = none), 10 file names (none = end)
//         4 settings   u32 count, then per entry: the index of the core setting, reg, shift, mask, number of bytes, the bytes padded to a multiple of 4 (RegBind: that setting is those
//                      register bits and has no operation)
//         5 hotkeys    22 words: mod_a mod_b, 7 action keys, task count, 4 task keys, vol_key, swap_key, own_keys (low, high), core_op, core_key, core_label, pc_keys
//         6 mount      u32 count, then that many slot names
//         7 reset writes   u32 count (1..8), then per entry reg, value (CoreDesc::reset_writes)
//         8 id feature     3 words: reg, mask (1..255), value (CoreDesc::id_feat_reg, id_feat_mask, id_feat_val)
//         9 report regs    u32 count (1..24), then per entry name, reg, bytes (1, 2 or 4) (CoreDesc::report_regs)
//        10 raw keys       6 words: flags (b0 the core's code takes the keys that are down, DescCode::rawkeys_held: then reg, code and both tables are 0), reg, caps_usage, caps_code, nhid, nmod,
//                          then the nhid key codes and the nmod (8) modifier codes, each table padded to a multiple of 4 (CoreDesc::rawkeys)
//        11 alt video      15 words of section 2's video part (CoreDesc::alt_video)
//        12 picture window 23 words: reg_feat feat_bit reg_ctrl ctrl_video_off reg_diw_h reg_diw_v reg_skip_x reg_skip_y reg_cols reg_lines x_per_unit x_origin y_origin src_w src_h
//                          def_skip_x def_skip_y def_cols def_lines interval_ms settle_ms frames_ms min_apply_ms (fbwin.h FbWinSpec; the signed ones as 32 bit two's complement)
//        13 mouse          7 words: kind (0 counters, 1 pot1351), reg_dx reg_dy reg_btn clamp step interval_ms (mousesink.h MouseSinkSpec)
//        14 rom set        17 words: flags (b0 DescCode::romset_version, b1 DescCode::romset_pick), 3 sizes (0 = none), role ext size_text example max choice_key auto_label want head_bytes
//                          window_off mirror_size mirror_stride clear_below (romset.h RomSetSpec; role, ext, size_text, example, choice_key, auto_label are strings)
//        15 window media   15 words: prefix nslots base stride max_image mount_reg mount_step track_bytes ntracks dlog_status dlog_track dlog_drive dlog_pop idle_flush_ms max_dirty_ms (winslot.h)
//        16 sector media   12 words: prefix nunits mount_reg mount_step feat_reg feat_sectors mbox_status mbox_lba mbox_done stage_off idle_sync_ms done_tag_mask (sectors.h)
//        17 media replace  u32 count, then per entry the index of the media slot (ascending) and the label (MediaSlot::replace)
//                          (a ROM's flag bit 2 in section 3 says that the core's code checks its content, DescCode::rom_check[i])
//        18 key rows       T239.3 base 3.3, the key table of a core's UI TOML ([[key]]); u32 count (1 .. DR_KEYS), then per row 9 words: layout (string, none), node (string, none) - exactly
//                          one of them -, lo, hi (both 0xFFFFFFFF: every key; else 0 <= lo <= hi <= 0x1FF), do (0 op, 1 decline, 2 swallow), op (string, none; present exactly for do 0), value
//                          (32 bit two's complement; 0 unless flags bit 0), flags (menuintent.h KF_ARG 1, KF_KEYARG 2, KF_STATUS 4, KF_STATUS_OK 8, KF_CLOSE 32, KF_DIRECTIVE 64,
//                          KF_DIRECT 128; none for a decline or a swallow; KF_ARG and KF_KEYARG not together), when (string, none: a state name, "!" in front: while it has none).
//                          The rows are CoreKey (menuintent.h), stage KS_CORE, tried in the record's order where an application's screens end and the menu's grammar begins.
//        19 windows        the register windows of a core's API TOML ([[window]]); u32 count (1 .. DR_WINDOWS), then per window 3 words: name (string, a-z 0-9 _, once), address, size (1 .. 0x100000,
//                          the block ends below 4 GiB). A window that covers a byte of a register the base drives itself (basereg.def) is refused with the register's name and code reference.
//                          descrec_window finds one by name (io.window).
//        20 routes         T239.3 D32, the blocks of rows in a tab (a core's UI TOML [[group_route]]); u32 count (1 .. UI_ROUTES_MAX), then per route 6 words: group (a-z 0-9 _, once), area (F1 .. F8),
//                          tab (an id, a-z 0-9 _), heading (1 to 31 characters), label (string, none; required for a page), flags (b0 page). They are MenuGroupRoute (menutree.h); a route of the group of a base
//                          route replaces it (menu_place_merge).
//        21 places         where a row the application adds stands (a core's UI TOML [[place]]); u32 count (1 .. UI_PLACES_MAX), then per place 5 words: op (string), key (string, none), route (string, none),
//                          group (string, none), order (32 bit two's complement, 0x80000000 none); at least one of route, group and order; an operation and key once. They are UiPlaceRow (menutree.h): a place
//                          changes only what it names of the base's place of the same operation and key (coreapp/base_ui.toml).
//                          Sections 18 to 21 need no core section (a menu.toml carries the key table, the routes and the places, a desc.toml the windows); sections 1 .. 17 need it.
//
// Version 7 is TOML A (T239.3 P1-1, D30, D34): the gateware as capabilities. Same header as version 6, flags exactly 4, every table empty, and the three sections 20 .. 22, each once
// (a record of versions 2 .. 6 has none of them, a version 7 record none of 1 .. 19). tools/descrec.py holds the same rules and its comment "TOML A" says what the TOML keys are.
//        20 io             2 words: iobase, u2p_io_base
//        21 identity       4 words: id (string, 4 printable ASCII characters), build flag (0 or 1), build (0 without the flag), block (string: the base-owned block that has the readable
//                          properties "id" and "build")
//        22 blocks         4 words nblocks (1 .. DR_BLOCKS) nprops (DR_PROPS) nfields (DR_FIELDS) nstates (DR_STATES); then the blocks (7 words: name, module, owner 0 base / 1 core, address,
//                          size 1 .. 0x100000, first property, count), the properties (8 words: name, offset, width 8 / 16 / 32, dir 1 r / 2 w / 3 rw, first field, count, first state, count), the
//                          fields (5 words: name, shift, bits, first state, count) and the states (2 words: name, value). Names are a-z 0-9 _ (1 .. 31). An owner's entries are the next run of
//                          their table, in order. Blocks do not overlap, a core's block covers no register of basereg.def, a register lies inside its block, two properties share a byte only when
//                          their directions share no bit, fields lie inside the width and share no bit, state names and values are once per owner and fit the field's bits or the register's width.
//
//   area   key name flags first_tab ntabs            (flags bit 0: shifted)
//   tab    id name n widget groups layout      (layout: string offset, 0 = none: "name;key=value;..." how the tab is drawn, contrib.h ui_attr; version 2: reserved, 0)
//   node   anchor kind label op state order when_state capability id first_bind nbind flags attr      (flags bit 0: closes the menu; attr: string offset, 0 = none,
//          "key;key=value;..." how it is shown: primary, link, goto, sub, chip, tone, off - libapi.h LibNodeDecl::attr; the anchor's group is its slot in the layout)
//   action label op first_ext next first_size nsize first_bind nbind flags reserved                    (flags bit 0: closes the menu)
//   setting group key first_value nvalues lo hi def apply     (nvalues 0: a range lo .. hi)
//   ctab   area id name widget groups n     (a tab added to an area of the route, T104.1 D8)
//   The core's own menu (T104.2 D10), the five tables a C++ descriptor carried as Task / MediaSlot / FileType / Setting arrays:
//   task   label op first_bind nbind flags ask ask_state pick_label group       (flags bit 0: closes the menu, bit 1: a folder is picked first, bit 2 (base 3.3): a job, a stepped
//                                                                               operation with the progress line and a cancel (Task::job; not with a hotkey or a folder),
//                                                                               bits 15:8 the default key of its hotkey, T228 R1: 'A'-'Z', '0'-'9', 0 none;
//                                                                               bits 31:16 the offset + 1 of its show_state text, T228 R21, 0 none)
//   media  name state eject_op first_bind nbind group [version 4: exts mount empty_tasks mounted_tasks flags(bit 0: eject closes the menu)]
//   ftype  ext inner_ext first_action nactions first_inner ninner                (runs into the file type action table)
//   fact   label op first_bind nbind flags ask ask_off first_size nsize          (flags bit 0: closes the menu, bit 1: repeat - the ask word names the operation that says how many
//                                                                       entries there are, T228 R21, bit 2: the data the operation returns is shown as text;
//                                                                       bits 15:8 the sheet letter; ask_off a signed 16 bit word)
//   cset   key group first_value nvalues def when apply base                     (when 0: live, 1: at the next reset, 2: a text, 3: a secret text; bit 2 lock_state, bit 3 a file the person selects, bits 15:8 show_state)
//   A tab's `groups` string is, for an information tab, the comma separated lines it shows (the application's fixed ones); a tasks, media or settings
//   tab lists the groups its entries name; a system tab lists the groups its layout's `pages` shows (T228 R6).
//   hide   id         rename id label
//   bind   arg kind i s
//
// The version is the record's own: a reader refuses a version it does not know, a writer never changes a field's place within one.
#ifndef COREAPP_DESCREC_H
#define COREAPP_DESCREC_H

#include <stdint.h>
#include "contrib.h"
#include "pad.h"
#include "hotkeys.h"
#include "romsetspec.h"
#include "winslot.h"
#include "sectors.h"
#include "mousesink.h"
#include "fbwin.h"
#include "rawkeys.h"

#define DESCREC_MAGIC 0x44585254U                 /* "TRXD" */
#define DESCREC_VERSION 7
#define DESCREC_VERSION_MIN 2                     /* a version 2 record has 0 in the two words version 3 uses (a tab's layout, a node's attributes) */
#define DESCREC_HEADER 176                        /* versions 2 to 4, and the least a record has */
#define DESCREC_HEADER_V5 208                     /* version 5: the pad's six words and the restart's two follow */
#define DESCREC_HEADER_V6 216                     /* version 6: + the offset and size of the data sections */
#define DESCREC_RESTART_MAX (1u << 24)            /* the most RAM a restart clears: 16 MiB */
#define DESCREC_NONE 0xFFFFFFFFU
#ifndef DESCREC_MAX_SIZE
#define DESCREC_MAX_SIZE 0x20000U                 // 128 KiB: more is not a menu description
#endif

// The macros under #ifndef below (and DESCREC_MAX_SIZE) are the shared app's maxima: an app linked from the SDK sets them in the [sizes] table of its TOML B (tools/app_build.py), and
// tools/descrec.py compiles the core's record against the same table (--sizes).
#define DR_AREAS 8
#define DR_TABS 32
#define DR_NODES MENU_NODES_MAX
#define DR_ACTIONS 24
#define DR_SETTINGS 16
#define DR_HIDE 16
#define DR_RENAME 16
#ifndef DR_BINDS
#define DR_BINDS 700
#endif
#ifndef DR_EXTS
#define DR_EXTS 48
#endif
#define DR_SIZES 16
#ifndef DR_VALUES
#define DR_VALUES 8192                            // T228 R21: the reDIP:SID filter ranges (FW: base 0..511, offset -1024..1023) and the socket pages
#endif
#define DR_CTABS 16
#ifndef DR_TASKS
#define DR_TASKS 64
#endif
#define DR_MEDIA 6
#ifndef DR_FTYPES
#define DR_FTYPES 32
#endif
#ifndef DR_FACTS
#define DR_FACTS 200
#endif
#ifndef DR_CSETS
#define DR_CSETS 330
#endif
#define DR_TABLES 17
#define DR_SET_FILE 8                             // T239.3 D17: bit 3 of a core setting's time code: a file the person selects (Setting::kind 3); bit 2 is the lock flag, bits 15:8 the show_state
#define DR_SETBINDS 16                            // T239.3 D17: core settings that are register bits
#define DR_BIND_BYTES 64                          // ... and the bytes one of them maps its values to
#define DR_CAPS 8
#define DR_MOUNT 6
#define DR_SECTIONS 24                            // the ids 1 .. 24 (20 .. 22: TOML A, version 7 only; 23, 24: the routes and the places, version 6)
#ifndef DR_KEYS
#define DR_KEYS 64                                // base 3.3: rows of the key table
#endif
#ifndef DR_WINDOWS
#define DR_WINDOWS 32                             // base 3.3: register windows
#endif
#define DR_WINDOW_MAX 0x100000u                   // the largest window, bytes
#ifndef DR_BLOCKS
#define DR_BLOCKS 64                              // TOML A (version 7): blocks, and in all their properties, fields and states
#endif
#ifndef DR_PROPS
#define DR_PROPS 512
#endif
#ifndef DR_FIELDS
#define DR_FIELDS 512
#endif
#ifndef DR_STATES
#define DR_STATES 1024
#endif
#define DR_PROPS_PER_BLOCK 64
#define DR_FIELDS_PER_PROP 32
#define DR_STATES_PER_OWNER 64
#define DESCREC_KEY_MAX 0x1FF                     // the largest key code of a key row
#define DR_RESETW 8                               // reset writes
#define DR_RAWKEY_HID 255
#define DR_RAWKEY_MOD 8

// T239.3 D17, version 6: the core's data sections (sections 1 .. 17). Only a record that has them carries this: DescRecord::sec, allocated by the reader, is null for the menu
// records of libraries and applications, which do not need the room. descrec_core makes the descriptor of it.
struct DescSections {
    struct {
        char id[4];
        const char *name, *title, *short_title, *folder, *store;
        uint32_t min_contract, reset_ms;
        const char *service_op;
        uint32_t service_period_ms, flags;
        const char *needs_program, *program_ext, *type_op, *run_op, *core_id;
        uint32_t live_ms;
        const char *hooks;                        // the 19th word, null for a record of 18: the library whose export corehooks is the code of the core (hookhost.h)
    } ident;
    char cap_names[DR_CAPS][16];                  // the comma separated list of the core section, one name each
    const char *caps[DR_CAPS]; int ncaps;
    bool has_video;
    VideoDesc video;
    FreezeDesc freeze;
    RomSpec roms[ROMS_MAX]; int nroms;
    RegBind setbind[DR_SETBINDS]; uint8_t setbind_bytes[DR_SETBINDS][DR_BIND_BYTES]; int nsetbind;
    bool has_hot;
    HkSpec hot;
    const char *mount_names[DR_MOUNT]; int nmount;
    MountSlots mount;                             // made by descrec_core: the names above and the three functions of the core's code
    // the remaining data tables of CoreDesc (sections 7 .. 17); the records' strings point into the record, the tables live here
    bool rom_check[ROMS_MAX];                     // section 3 flag bit 2: the core's code checks this ROM's content
    RegWrite reset_writes[DR_RESETW]; int nreset_writes;
    bool has_idfeat; uint16_t idf_reg; uint8_t idf_mask, idf_val;
    ReportReg report_regs[REPORT_REGS_MAX]; int nreport_regs;
    bool has_rawkeys, rawkeys_held;
    RawKeySpec rawkeys; uint8_t rk_hid[DR_RAWKEY_HID], rk_mod[DR_RAWKEY_MOD];
    bool has_alt_video; VideoDesc alt_video;
    bool has_fbwin; FbWinSpec fbwin;
    bool has_mouse; MouseSinkSpec mouse;
    bool has_romset, romset_version, romset_pick;
    RomSetSpec romset; RomSetLabels romset_labels;
    bool has_winmedia; WindowMediaSpec winmedia;
    bool has_secmedia; SectorMediaSpec secmedia;
};

// A register window of a core's API TOML (section 19): io.window hands out `address` for `name`; `size` is the block's length in bytes. The name points into the record.
struct DescWindow {
    const char *name;
    uint32_t address, size;
};

// TOML A (version 7, sections 20 .. 22): a block is a capability, its map the properties (registers) with their fields and the states each can take. The names point into the record.
// Properties, fields and states are tables of DescMap; an owner's entries are the run first .. first + count of its table.
struct DescState {
    const char *name;
    uint32_t value;
};
struct DescField {
    const char *name;
    uint32_t shift, bits;
    uint32_t first_state, nstate;
};
#define DR_DIR_R 1
#define DR_DIR_W 2
struct DescProp {
    const char *name;
    uint32_t offset, width, dir;                  // bytes from the block; 8, 16 or 32 bits; DR_DIR_R | DR_DIR_W
    uint32_t first_field, nfield, first_state, nstate;
};
struct DescBlock {
    DescWindow win;                               // name, address and size: io.window hands the block out like a window
    const char *module;                           // the gateware module, and so the driver library
    bool core;                                    // owner: the core (else the base)
    uint32_t first_prop, nprop;
};
struct DescMap {
    uint32_t iobase, u2p_io_base;
    const char *ident_id;                         // the standard block: the ID, optionally the build word (as cor_pack wrote it), and the block that holds the registers
    bool has_build;
    uint32_t build;
    const char *ident_block;
    int nblocks, nprops, nfields, nstates;
    DescBlock blocks[DR_BLOCKS];
    DescProp props[DR_PROPS];
    DescField fields[DR_FIELDS];
    DescState states[DR_STATES];
};

// What the reader makes of a record. The strings and the sizes point into the record's own bytes: keep them while this is used.
struct DescRecord {
    bool has_route;                               // the record gives the route; else the default one stays
    Skeleton skel;
    SkelArea areas[DR_AREAS];
    SkelTab tabs[DR_TABS];
    LibRecord rec;                                // the description's own nodes, file actions and settings (rank CR_DESC)
    LibNodeDecl nodes[DR_NODES];
    LibFileActionDecl actions[DR_ACTIONS];
    LibSettingDecl settings[DR_SETTINGS];
    OpBind binds[DR_BINDS];
    const char *exts[DR_EXTS];
    uint32_t sizes[DR_SIZES];
    const char *values[DR_VALUES];
    const char *hide[DR_HIDE];
    NodeRename rename[DR_RENAME];
    LibTabDecl ctabs[DR_CTABS];
    Overrides ov;
    // the core's own menu (flag bit 1): what descrec_apply puts into the descriptor in place of its C++ tables
    const char *machine_info;                     // version 4: the settings (comma separated) whose values make Home's machine line (CoreDesc::machine_info), or null
    const char *merged;                           // the names of the libraries whose menus were merged in at pack time (comma separated), or null
    bool has_pad;                                 // version 5 (T228 R10): the record declares the core's pad; descrec_apply makes `pad` the descriptor's
    PadSpec pad;                                  // its names point into `values`, its def_map into pad_def
    uint8_t pad_def[PAD_BUTTONS_MAX];
    uint32_t restart_clear;                       // version 5 (T228 R17): the bytes "Restart core" clears (CoreDesc::cold_ram_bytes), 0: the core's own value
    bool restart_ram_only;                        // ... through the RAM window in its memory-only mode (CoreDesc::cold_ram_only)
    bool has_own;
    Task tasks[DR_TASKS]; int ntasks;
    MediaSlot media[DR_MEDIA]; int nmedia;
    FileType ftypes[DR_FTYPES]; int nftypes;
    FileAction facts[DR_FACTS];
    Setting csettings[DR_CSETS]; int ncsettings;
    // T239.3 base 3.3 (sections 18 and 19): the core's key table and register windows
    CoreKey keys[DR_KEYS]; int nkeys;
    DescWindow windows[DR_WINDOWS]; int nwindows;
    // T239.3 D32 (sections 23 and 24): the core's routes and places over the base's (coreapp/base_ui.toml); strings point into the record. bundle_merge merges them with the base's table.
    UiPlace place; bool has_place;
    // T239.3 D17, version 6: has the record data sections? Then `sec` holds them (descrec_core makes the descriptor of them); the destructor frees it.
    bool has_core;
    DescSections *sec;
    DescMap *map;                                 // version 7 (TOML A): the blocks of the gateware; null for every other record
    DescRecord() : sec(0), map(0) { }
    ~DescRecord() { delete sec; delete map; }
    DescRecord(const DescRecord &) = delete;
    DescRecord &operator=(const DescRecord &) = delete;
};

// Reads the record. 0 and `out` filled, or a negative number and a sentence in why.
int descrec_parse(const uint8_t *rec, uint32_t n, DescRecord *out, char *why, int whylen);

// The window `name` of the record (io.window of the base calls it with the core's name of the window): null when the record declares none of that name. The record was checked at
// load: no window covers a register the base drives itself. A version 7 record (TOML A) hands out its blocks by their names too (a core's block covers no such register either).
const DescWindow *descrec_window(const DescRecord &rec, const char *name);
// TOML A: the block `name` of a version 7 record, its property `name`, and the field `name` of a property; null when there is none (also for a record without TOML A). Their pointers stay
// inside the record's DescMap.
const DescBlock *descrec_block(const DescRecord &rec, const char *name);
const DescProp *descrec_prop(const DescRecord &rec, const DescBlock &b, const char *name);
const DescField *descrec_field(const DescRecord &rec, const DescProp &p, const char *name);

// The registers the base drives itself (basereg.def): the first one that the block [address, address + size) covers, else null. `ref` is the code that drives it.
struct BaseReg {
    const char *name;
    uint32_t address, bytes;
    const char *ref;
};
const BaseReg *descrec_base_reg_hit(uint32_t address, uint32_t size);
const BaseReg *descrec_base_regs(int *n);

// What a descriptor holds that is code, not data (T239.3 D17): the control hooks, the operations' registration, the media write-back and the three functions of the mount
// slot. The compiled-in code of the core provides them, or, when the record names a hooks library (section 1, hooks), the library: CoreDesc::hooks_lib is then set, the control
// hooks and the three mount functions stay null in the descriptor descrec_core makes, and hookhost.h hooks_bind puts the library's in once the library is loaded.
struct DescCode {
    core_hook_t init, release, warm_reset, cold_start, media_flush;
    bool (*media_pending)(CoreCtx *c);
    void (*register_ops)(OpRegistry &r);
    uint32_t (*rom_key)(CoreCtx *c);
    const MountSlots *mount_slots;                // its spec, mount and eject are used; n and names come from the record's mount section when it has one
    // the functions that sit inside data tables (T239.3 D17): each is used only where the record says the core has it (a flag of the section) and is refused when the code has none
    bool (*rom_check[ROMS_MAX])(const uint8_t *data, uint32_t size);                     // RomSpec::check of entry i
    int (*romset_version)(const uint8_t *head, int len, uint16_t *ver, uint16_t *rev);    // RomSetSpec::version
    int (*romset_pick)(const KsCandidate *list, int n, uint16_t want);                    // RomSetSpec::pick
    void (*rawkeys_held)(CoreCtx *c, const uint8_t *usages, int n);                       // RawKeySpec::held
    void (*pad_id_loaded)(CoreCtx *c, int type);                                          // PadSpec::id_loaded, set on the record's pad when it has one
};

// The descriptor of the record's data sections: `out` is made whole from `rec` (which has to outlive it and is written to: the mount slots) and `hooks` (null: no code);
// nothing is copied from another descriptor. 0, or a negative number and a sentence in why that names the record and the section.
int descrec_core(DescRecord *rec, const DescCode *hooks, CoreDesc *out, char *why, int whylen);

// Lays the record's own menu on `base`: `out` is a copy of it whose tasks, media slots, file types and settings are the record's, the settings followed by
// those of `base` (the application's own items, T228 D10; a record with no settings leaves the base's); a record without the own tables leaves `out` equal to
// `base`, except that the record's machine-info line and its pad (T228 R10) are set whenever it has them. The arrays live in `rec`, which has to outlive `out`.
void descrec_apply(const DescRecord &rec, const CoreDesc *base, CoreDesc *out);

// Does the record's own menu (its tasks, media slots, file types, settings, machine-info line, pad and restart) go onto `base`? Which description wins (T239.3 D17): a record with data
// sections (the core's identity among them) describes the core completely, so beside a compiled-in descriptor it changes nothing - the compiled-in one wins, the start has used it
// (start_identify) and the menu is built from it; a descriptor that descrec_core made of a bundle's record is that record, and the menu is built from the record's tables. A record
// without data sections (a menu.toml) lays its menu on whichever descriptor there is, as before.
bool descrec_applies(const DescRecord &rec, const CoreDesc *base);

// Was the menu of the library `name` merged into this record when it was packed?
bool descrec_merged(const DescRecord &rec, const char *name);

#endif
