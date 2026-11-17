// The description record of a .cor (T104.1 D9, D12): the TOML description compiled at pack time (tools/cor_pack.py, tools/descrec.py) into a flat,
// pointer-free record that is read by offsets and nothing else - the board does not parse text. It carries what a core says about its menu
// without being code: the route (areas, tabs, widgets), its own nodes, file actions and settings, and what it hides or renames of other
// contributors' nodes. The reader checks every offset, count and string before it hands out a pointer, so a damaged or hostile record is
// refused with a sentence, never followed.
//
// Layout, little endian, every entry fixed size, strings in one table (NUL terminated, offsets into it, 0xFFFFFFFF = none):
//
//   0   u32 magic "TRXD"        4  u16 version (4; 2 and 3 are read too)   6  u16 header size (176)
//   8   u32 total size         12  u32 flags (bit 0: the record gives the route; bit 1: it carries the core's own menu tables)
//   16  u32 string table offset, u32 string table size
//   24  (offset, count) of the tables, in this order, 8 bytes each:
//       area 20 | tab 24 | node 52 | file action 40 | setting 32 | hide 4 | rename 8 | bind 16 | ext 4 | size 4 | value 4 | contributed tab 24 |
//       task 36 | media 44 (24 before version 4) | file type 24 | file type action 36 | core setting 36
//   160 poll: u32 operation (string offset, none = no poll), u32 period in ms, u32 merged libraries (string offset of their names, comma separated, none = no
//       library menu was merged: the menus of those libraries are in this record, the application does not add them again), version 4: the machine-info
//       line (string offset of the settings whose values make Home's machine line, none = no line; before version 4: 0)
//
//   area   key name flags first_tab ntabs            (flags bit 0: shifted)
//   tab    id name n widget groups layout      (layout: string offset, 0 = none: "name;key=value;..." how the tab is drawn, contrib.h ui_attr; version 2: reserved, 0)
//   node   anchor kind label op state order when_state capability id first_bind nbind flags attr      (flags bit 0: closes the menu; attr: string offset, 0 = none,
//          "key;key=value;..." how it is shown: primary, link, goto, sub, chip, tone, off - libapi.h LibNodeDecl::attr; the anchor's group is its slot in the layout)
//   action label op first_ext next first_size nsize first_bind nbind flags reserved                    (flags bit 0: closes the menu)
//   setting group key first_value nvalues lo hi def apply     (nvalues 0: a range lo .. hi)
//   ctab   area id name widget groups n     (a tab added to an area of the route, T104.1 D8)
//   The core's own menu (T104.2 D10), the five tables a C++ descriptor carried as Task / MediaSlot / FileType / Setting arrays:
//   task   label op first_bind nbind flags ask ask_state pick_label group       (flags bit 0: closes the menu, bit 1: a folder is picked first)
//   media  name state eject_op first_bind nbind group [version 4: exts mount empty_tasks mounted_tasks flags(bit 0: eject closes the menu)]
//   ftype  ext inner_ext first_action nactions first_inner ninner                (runs into the file type action table)
//   fact   label op first_bind nbind flags ask ask_off first_size nsize          (flags bit 0: closes the menu; ask_off a signed 16 bit word)
//   cset   key group first_value nvalues def when apply base                     (when 0: live, 1: at the next reset)
//   A tab's `groups` string is, for an information tab, the comma separated lines it shows (the application's fixed ones); a tasks, media or settings
//   tab lists the groups its entries name.
//   hide   id         rename id label
//   bind   arg kind i s
//
// The version is the record's own: a reader refuses a version it does not know, a writer never changes a field's place within one.
#ifndef COREAPP_DESCREC_H
#define COREAPP_DESCREC_H

#include <stdint.h>
#include "contrib.h"

#define DESCREC_MAGIC 0x44585254U                 /* "TRXD" */
#define DESCREC_VERSION 4
#define DESCREC_VERSION_MIN 2                     /* a version 2 record has 0 in the two words version 3 uses (a tab's layout, a node's attributes) */
#define DESCREC_HEADER 176
#define DESCREC_NONE 0xFFFFFFFFU
#define DESCREC_MAX_SIZE 0x20000U                 // 128 KiB: more is not a menu description

#define DR_AREAS 8
#define DR_TABS 32
#define DR_NODES MENU_NODES_MAX
#define DR_ACTIONS 24
#define DR_SETTINGS 16
#define DR_HIDE 16
#define DR_RENAME 16
#define DR_BINDS 700
#define DR_EXTS 48
#define DR_SIZES 16
#define DR_VALUES 3200
#define DR_CTABS 16
#define DR_TASKS 64
#define DR_MEDIA 4
#define DR_FTYPES 32
#define DR_FACTS 200
#define DR_CSETS 330
#define DR_TABLES 17

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
    bool has_own;
    Task tasks[DR_TASKS]; int ntasks;
    MediaSlot media[DR_MEDIA]; int nmedia;
    FileType ftypes[DR_FTYPES]; int nftypes;
    FileAction facts[DR_FACTS];
    Setting csettings[DR_CSETS]; int ncsettings;
};

// Reads the record. 0 and `out` filled, or a negative number and a sentence in why.
int descrec_parse(const uint8_t *rec, uint32_t n, DescRecord *out, char *why, int whylen);

// Lays the record's own menu on `base`: `out` is a copy of it whose tasks, media slots, file types and settings are the record's, the settings followed by
// those of `base` (the application's own items, T228 D10; a record with no settings leaves the base's); a record without the own tables leaves `out` equal to
// `base`, except that the record's machine-info line is set whenever it has one. The arrays live in `rec`, which has to outlive `out`.
void descrec_apply(const DescRecord &rec, const CoreDesc *base, CoreDesc *out);

// Was the menu of the library `name` merged into this record when it was packed?
bool descrec_merged(const DescRecord &rec, const char *name);

#endif
