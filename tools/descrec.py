#!/usr/bin/env python3
"""The description record of a .cor (T104.1 D9, D12): TOML source -> flat binary record, and the checker that reads it back.

The layout is documented in coreapp/src/descrec.h (that file and this one are the two readers of it; tests/coreapp/test_descrec.cc and
tools/test_descrec.py compile the same sources and read them with both). Little endian, fixed entries, one string table, no pointers.

Source (TOML; every key is checked, an unknown one is an error - a typo must not silently become a menu without its entry):

    machine_info = "System Mode,REU Size"   # top-level, optional: the names of the settings whose values the Home machine line prints (record v4); a setting with
                                   # show_state is named only while that state holds (T228 DC5)

    [restart]                      # optional (T228 R17, record v5): what F8 "Restart core" clears before the core starts again (CoreDesc::cold_ram_bytes)
    clear_ram = 65536              # the machine RAM in bytes from address 0, cleared through the RAM window while the core is held (256 .. 16 MiB, whole 256s)
    ram_only = true                # optional: the window in its memory-only mode while it clears (io.h ram_memonly: RAM under the I/O range too)

    # T239.3 D17, record version 6: the data of a core's descriptor that the C++ tables above do not hold, as sections behind the string table (a core with them
    # needs no compiled-in descriptor for its data; its function hooks stay code). Every key of these tables is checked as the others are. Example: cores/vc20/app/desc.toml.
    [core]                         # section 1, required once there is any section: the identity and the machine's flags
    id = "VC20"                    # the four characters of the ID block
    name = "VIC-20"                # the .cor manifest's name (CoreDesc::cor_name); title, short_title, folder, store are the other texts of the descriptor
    min_contract = 1               # reset_ms, service_op + service_period_ms, live_ms, type_op, run_op, needs_program, program_ext, core_id, caps = ["iec"], and the flags
                                   # poll_when_frozen, board_caps, id_shared, own_volume, no_tone, menu_first; hooks = "<library>" names the library of the bundle whose export
                                   # corehooks (coreapp/src/core_hooks.h) is the code of the core: init, release, warm_reset, cold_start, media_flush, media_pending and the mount slots
    [video]                        # section 2 with [freeze]: CoreDesc::video (w, h, scale, x0, y0, fb_base, fb_stride, nbuf, reg_latest, reg_seq, reg_fb_base, reg_fb_stride, and bpp,
    [freeze]                       # pxfmt, h60 where 0 is not meant) and CoreDesc::freeze (reg_status, mask, timeout_ms, word); scale 0 with every other number 0 = the core has no
                                   # picture of its own (it goes through the host's scaler) and a freeze alone
    [[rom]]                        # section 3: a RomSpec - role, names (first match wins), sizes, required, set, shared, put_op; check = true says the core's code checks the content
                                   # (DescCode::rom_check[i]; the check is code and stays so)
    [hotkeys]                      # section 5: a HkSpec - mod, actions (7), tasks, own_keys, vol_key, swap_key, core_op + core_key + core_label, pc_keys
    [mount]                        # section 6: names = the slots of CoreDesc::mount_slots; their three functions (spec, mount, eject) are code
    [[reset_write]]                # section 7: RegWrite, written with every settings commit - reg, value (1 .. 8 of them)
    [id_feature]                   # section 8: CoreDesc::id_feat_reg / mask / val - reg, mask (not 0), value: how an id_shared core is told apart by its gateware
    [[report_reg]]                 # section 9: ReportReg of the bug report - name, reg, bytes (1, 2 or 4); 1 .. 24 of them
    [rawkeys]                      # section 10: RawKeySpec, a USB keyboard as the core's own key events - reg, hid (the machine's code per HID usage, 0xFF none), mod (8 codes),
                                   # caps_usage, caps_code; or held = true + caps_usage: the core's code takes the keys that are down (DescCode::rawkeys_held), no tables
    [alt_video]                    # section 11: CoreDesc::alt_video, the keys of [video]
    [fbwin]                        # section 12: FbWinSpec, the picture window - reg_feat feat_bit reg_ctrl ctrl_video_off reg_diw_h reg_diw_v reg_skip_x reg_skip_y reg_cols reg_lines
                                   # x_per_unit x_origin y_origin src_w src_h def_skip_x def_skip_y def_cols def_lines interval_ms settle_ms frames_ms min_apply_ms
    [mouse]                        # section 13: MouseSinkSpec - kind ("counters" | "pot1351"), reg_dx reg_dy reg_btn clamp step interval_ms
    [romset]                       # section 14: RomSetSpec, a ROM chosen out of a directory - role ext sizes size_text example max choice_key auto_label want head_bytes window_off
                                   # mirror_size mirror_stride clear_below; version = true and pick = true say the core's code has the two functions (DescCode::romset_version, romset_pick)
    [window_media]                 # section 15: WindowMediaSpec - prefix nslots base stride max_image mount_reg mount_step track_bytes ntracks dlog_status dlog_track dlog_drive dlog_pop
                                   # idle_flush_ms max_dirty_ms
    [sector_media]                 # section 16: SectorMediaSpec - prefix nunits mount_reg mount_step feat_reg feat_sectors mbox_status mbox_lba mbox_done stage_off idle_sync_ms done_tag_mask
    # a [[core_setting]] (section 4) with reg_bind = { reg = 161, shift = 0, mask = 31, bytes = [0, 1, 2] } in place of `apply`: the setting is those register bits
    # (regset.h RegBind; bytes has one entry per value, or none: the value itself)
    # a [[core_setting]] with file = true, when = "live" | "reset" and apply (kind 3): a file the person selects in a file selector, the text is its path, none by default; apply takes one
    # text argument "path" (Setting::kind 3; no values, default, base, text, secret or reg_bind). reg_bind takes up to 64 bytes (one per value)
    # a [[core_setting]] (a choice among values) with pick = "<name>" (T239.3 D32, QC of wave 2): the list changes at run time, as a text row's pick names the state that holds its list; the values are what the row
    # shows until the state's list has filled them (one value is enough); the name is the last value string before the lock state, flag 16 of the time code, in a row of the base UI TOML and of a core's alike
    # a [[core_setting]] with romset_choice = true in place of `apply` and reg_bind: the item that chooses the member of the [romset] (its choice_key is this key); no values,
    # the record holds the set's labels (auto_label, then "-" for each of max places) and the reader hands the setting the labels of the set (desc.h Setting::bind)
    # a [[media]] with replace = "<label>" (section 17): MediaSlot::replace, the file action that replaces the file of a filled slot
    # Which description wins (T239.3 D17): while a descriptor for the core is compiled in, it is the core and a record with these sections changes nothing at run time
    # (coreapp/src/descrec.h descrec_applies); a core without one is made of the record (descrec_core) and its code supplies only the hooks. menu_rec is the record itself.

    [[area]]                       # optional; without any, the default route stays (T104 5.8)
    key = "F5"                     # F1 .. F8
    name = "C64"                   # optional: the core's short title
    shifted = false
      [[area.tab]]
      id = "drives"                # lower case, [a-z0-9_]: what an anchor names
      name = "Drives"              # optional: none = named after the area (T205 D1)
      widget = "settings"          # info | browser | media | settings | tasks | system | nodes
      groups = ["drive_a"]         # settings groups the tab lists (widget settings)
      n = 3                        # the printed number; 0 or absent: the next free one

      layout = "settings"          # optional: how the tab is drawn (a screen of the UX concept): home | split | settings | status | library; the flat list when absent
      ui = { title = "TRX64 behavior" }      # optional: the layout's parameters (title, crumb); `ui = { sections = true }` alone (no layout) keeps the tab a list and
                                     # draws each of its groups under a heading made from the group's id (T228; not drawn when it repeats the tab's title)
                                     # `pages = "g1,g2"` (T228 D18): those groups are not sections; one row each opens a page that holds the group's settings (and the nodes
                                     # anchored to the group); "Name=g1+g2" is one page named Name with both groups, each under its heading (R6). A settings tab names pages
                                     # with `sections`; a system tab's groups are pages only, their rows stand under its Network heading. `pages_head = "SID"` is a heading
                                     # above the page rows
                                     # `page = "home"` (T228 R1): the tab is a page of that tab of the area (an earlier one): no number, not on the strip, back to it;
                                     # a task with op menu.screen opens it (the Timewarp and Monitor screens of Home)

    [[node]]                       # a button, a state line or a heading the core itself puts into a tab
    anchor = "F5/drives/drive_a"   # area/tab or area/tab/group
    kind = "op"                    # op | state | group   (setting and widget are refused when the menu is merged)
    label = "Reset Drive"
    op = "drive.reset"             # op: the operation; state: the state value shown
    bind = [ { arg = "slot", str = "drive_a" } ]     # every argument of the operation: file | int | str | ask | text
    order = 10
    when_state = "drive.drive_a.mode != empty"
    capability = "drive_a"
    id = "reset_a"                 # what hide / rename name; default: the operation or state
    closes_menu = false
    ui = { primary = true, goto = "F3", sub = "@drive.drive_a.image", chip = "@drive.drive_a.mode", tone = "cyan", off = "the reason it is greyed" }
                                   # optional, how the node is shown (coreapp/src/screens.h): primary (the violet button), link, goto (an area key or area/tab),
                                   # sub / chip (a second line, a state chip: "@state.name", "@info.Network", "link.Network" or plain text), tone, off (greyed, with its reason),
                                   # why (the reason shown while the node's when_state does not hold)

    [[file_action]]
    ext = [".d64", ".g64"]
    label = "Mount Disk"
    op = "drive.mount"
    bind = [ { arg = "path", file = true }, { arg = "mode", str = "rw" } ]
    size_in = [16384, 32768]
    closes_menu = false

    [[setting]]
    group = "drive_a"
    key = "Drive Bus ID"
    range = [8, 11]                # or values = ["Disabled", "Enabled"]
    default = 8                    # a value of the range, or the index into values
    apply = "drive.set_bus_id"

    [[tab]]                        # a tab this contributor adds to an area of the route (appended after the area's own, never renumbering them)
    area = "F5"
    id = "extras"
    name = "Extras"
    widget = "nodes"               # as for a tab of the route
    groups = ["g"]                 # widget settings: the groups it lists
    n = 0                          # 0: the next free number

    # T104.2 D10: the tables of the core's OWN menu (what C++ descriptors carry as Task / FileType / MediaSlot / Setting tables). A record that has
    # any of them takes the core's tasks, media slots, file types and settings from the record; the application's own settings (Picture, Persist, Home,
    # hotkeys) follow the record's, and the entries a library contributes are merged after them.
    [[task]]                       # a button of a tasks tab; `group` names the tab (the tab's groups list) it belongs on, none: the tabs that list no group
    group = "machine"
    label = "Reset"
    op = "machine.cold_reset"
    bind = [ ]                     # as for a node, plus ask: the typed text goes to the argument bound with `text = true`
    closes_menu = true
    ask = "Name of the new D64"    # optional: the prompt; ask_state: a state whose text prefills it; pick_dir: a folder is picked first (pick_label: its first row)
    ask_state = "drive.suggest.d64"
    pick_dir = true
    pick_label = "Create here"
    hotkey = "T"                   # optional (T228 R1): one upper case letter or digit, the default key of the task's hotkey (the core's modifier + it, F7 > Hotkeys);
                                   # at most 4 tasks, none that asks (ask)

    [[media]]                      # a slot of a media tab: what is in it (state), and how it is ejected
    group = "drives"
    name = "Drive A:"
    state = "drive.drive_a.image"
    eject_op = "drive.eject"
    eject_bind = [ { arg = "slot", str = "iec:drive_a" } ]
    exts = ".d64,.g64"             # the drive list (T212 D5/D11): what Mount from Library offers; mount = a file action's label, empty_tasks / mounted_tasks = task labels
    mount = "Mount Disk"           # (comma separated) listed while the slot is empty / mounted; each must name an existing file action or task (the pack fails otherwise)
    empty_tasks = "Insert blank D64"
    mounted_tasks = "Save disk"
    eject_closes = true            # optional: the Eject row closes the menu

    [[filetype]]                   # what the browser offers for files of an extension (and, for a disk image, for the files inside it)
    ext = ".d64"
    inner_ext = ".prg"             # optional; the actions below [[filetype.inner]] are for the files inside whose name ends so
      [[filetype.action]]
      label = "Mount Disk"
      op = "drive.mount"
      bind = [ { arg = "path", file = true } ]
      closes_menu = true
      ask = "Start address"        # optional: the prompt; the typed address goes to the argument bound with `ask = true`, ask_off: where its default is read
      ask_off = 0
      size_in = [16384]            # optional
      [[filetype.inner]]
      label = "Mount & Run"
      op = "drive.mount_run_file"

    [[core_setting]]               # a setting of the core itself: values (labels) and the index of the default, when it applies ("live" or "reset")
    key = "Drive Bus ID"
    group = "drives"
    values = ["8", "9", "10", "11"]
    default = 0
    when = "live"
    apply = "drive.set_bus_id"
    base = 0                       # optional: the operation gets base + index

    # a tab with widget "info" names the lines it shows (of the application's fixed ones): lines = ["Network", "Media"]; none: all six
    [poll]                         # the operation called every period_ms on a task of its own (T104.1 D6): no arguments, may answer `more`
    op = "drive.poll"
    period_ms = 10

    [pad]                          # T228 R10: the core's USB pads through the pad service (coreapp/src/pad.h PadSpec); a record with it is version 5
    output = "joystick"            # "joystick": the host's joystick lines, port 1 (reg 0) and port 2 (reg 1), at most 5 buttons (up down left right fire);
                                   # "registers" (the default): a 16 bit pair per player in the core's register block, `regs = [r1, r2]`
    per_pad = true                 # one port, fed by the first pad plugged, its map kept per pad (VID:PID) - as the Amiga's port 2 (pad.h by_id)
    buttons = ["Up", "Down", "Left", "Right", "Fire"]       # bit n of the word = buttons[n], the names the "Map buttons" dialog asks for
    default = ["up", "down", "left", "right", "face_bottom"]  # per button: up down left right (D-pad or stick), a position face_right face_bottom
                                   # face_left face_top l r select start (resolved per pad, pad.h PAD_ROLE_*), button1 .. button32, none

    # T239.3 base 3.3 (sections 18 and 19, version 6; they need no [core]): the key table of a core's UI TOML (menu.toml) and the register windows of its API TOML (desc.toml).
    # Both files are one record: cor_pack.py pack --desc menu.toml --desc desc.toml (a table or key present in two files is refused with both names).
    [[key]]                        # a row of the key table (replaces the KS_MONITOR and KS_TIMELINE rows of menukeytab.cc); rows are tried in file order, after an application's
                                   # screens and before the grammar of the menu, the first that matches takes the key. `key = [ {..}, {..} ]` is the same table (one line a row)
    layout = "terminal"            # the scope: while the shown tab has this layout (home desk split settings status library timeline terminal app grid) ...
    node = "monitor"               # ... or, instead of layout, while the shown tab has this id; exactly one of them. Rows do not apply in an application's screens
    key = "any"                    # one key (up down left right return esc del runstop space home pgup pgdn tab f1 .. f8 f10 scrlock menu ctrl+<letter> or one printable
                                   # character), or "any" (every key); or codes = [32, 126] (a range of key codes, inclusive) - exactly one of key and codes
    do = "op"                      # op (the default when `op` is given), decline (the key goes on to the menu's grammar) or swallow (the key is spent)
    op = "monitor.key"             # the operation (do = op)
    value = -10                    # the operation's int argument "value" (KF_ARG); pass_key = true sends the key as its int argument "key" instead (KF_KEYARG): not both
    status = true                  # the answer's text is the status line, bad when the operation failed (KF_STATUS)      status_ok: also when it succeeded (KF_STATUS_OK)
    close = true                   # the menu closes after it (KF_CLOSE)      directive: the answer's text is "close" or "goto SPEC" for the menu (KF_DIRECTIVE)
    direct = true                  # called on the menu task, without the core lock (KF_DIRECT)      when = "!monitor.wants_stop": the row counts only while that state has a text
    [[window]]                     # a register window io.window hands out by name; no table of the base's registers refuses it (a block of TOML A that overlaps another is refused)
    name = "audio_mixer"           # a-z, 0-9, _ (at most 31), once per record
    address = 0xA0000500
    size = 0x14                    # bytes, 1 .. 0x100000

    # T239.3 D32 (sections 23 and 24, version 6; they need no [core]): where the rows the application adds to every core (Picture, Scale, Volume, Tone, the hotkeys, Persist, Home, the
    # look, joystick and mouse) stand in the menu. The SDK's base UI TOML (coreapp/base_ui.toml) holds them all; a core's UI TOML (menu.toml) places a base row elsewhere by naming it again.
    [[group_route]]                # a block of rows in a tab of the area: the group a place names. A route of the core's record with the name of a base route replaces it; others are added
    group = "storage"              # a-z, 0-9, _ (at most 31)
    area = "F7"                    # F1 .. F8
    tab = "settings"               # the id of the tab (a core whose area has no such tab lists the rows in the area's tab "settings")
    heading = "Storage"            # the block's heading, 1 to 31 characters
    label = "Hotkeys"              # optional, required with page: the row that opens the page
    page = false                   # optional: the rows are not in the tab, one row (label) there opens a page of them
    [[place]]                      # one row of the application: found by its operation (and its key, when the operation serves several rows); at most once per operation and key
    op = "volume.apply"            # the operation of the row (Setting::apply)
    key = "Volume"                 # optional: the row's key (case does not matter); a place with a key wins over one without
    route = "audio"                # optional: the route the row stands in (a name of a [[group_route]] of this record or of the base)
    group = "audio"                # optional: the setting group the row gets (what a tab with `groups` or `sections` lists)
    order = 20                     # optional: the place among the rows that are placed; equal numbers keep the order the application adds them in
                                   # (at least one of route, group and order; a core's place leaves out what it does not change, the base's value stays)
    # a [[task]] with job = true runs as a stepped operation (the menu calls it while it answers `more`, with the progress line and a cancel): no hotkey, no pick_dir

    # TOML A (record version 7, T239.3 P1-1, D30, D34): the gateware as capabilities. A file with [io], [identity] and [[block]] and nothing else; see the section "TOML A" below.
    hide = ["some_node_id"]        # nodes of other contributors the description hides; a key at the top of the file, before the first table
    [[rename]]
    id = "some_node_id"
    label = "New label"
"""
import os
import re
import struct

MAGIC = 0x44585254          # "TRXD"
VERSION = 7                 # 7 (T239.3 P1-1, D30, D34) is TOML A: no menu tables, only sections 20 to 22 (see "TOML A" below). 6 (T239.3 D17) appends the core's data sections after the string table (header 216: the sections' offset and size) - written only for a record that has one, so a record without stays what it was; 5 (T228 R10, R17) adds the pad's six words and the restart's two to the header (208 bytes) - written only for a record with a [pad] or a [restart], one without stays version 4; 4 (T228) widens a media slot by the drive list's fields and gives the header's last word the machine-info line; 3 gave the two reserved words of 2 a use (a tab's layout, a node's attributes); 2 and 3 are read too
VERSION_MIN = 2
HEADER = 176                # versions 2 to 4
HEADER_V5 = 208             # version 5: + the pad (kind, reg 0, reg 1, first name, names, default map; kind NONE: no pad) + the restart (clear_ram bytes, flags bit 0 ram_only)
HEADER_V6 = 216             # version 6: + the offset and the size of the sections (T239.3 D17)
RESTART_CLEAR_MAX = 1 << 24 # the most RAM a restart clears (16 MiB)
NONE = 0xFFFFFFFF
MAX_SIZE = 0x20000
LIMITS = dict(area=8, tab=32, node=320, action=24, setting=16, hide=16, rename=16, bind=700, ext=48, size=16, value=8192, ctab=16,
              task=64, media=6, ftype=32, fact=200, cset=330)
ESIZE = dict(area=20, tab=24, node=52, action=40, setting=32, hide=4, rename=8, bind=16, ext=4, size=4, value=4, ctab=28,
             task=36, media=44, ftype=24, fact=36, cset=36)
ESIZE_V3_MEDIA = 24
ORDER = ("area", "tab", "node", "action", "setting", "hide", "rename", "bind", "ext", "size", "value", "ctab", "task", "media", "ftype", "fact", "cset")
OWN = ("task", "media", "ftype", "fact", "cset")          # the tables of the core's own menu (flag bit 1)
INFO_LINES = ("Core", "Application", "Network", "Media", "ROMs", "Libraries")   # menutree.cc menu_info_labels, the application's fixed lines
FILE_FLAG = 8              # the time code of a core setting: the setting is a file the person selects (Setting::kind 3); bit 2 is the lock flag, bits 15:8 the show_state
WHEN = {"live": 0, "reset": 1}
WIDGETS = ("info", "browser", "media", "settings", "tasks", "system", "nodes")
NODE_KINDS = {"op": 0, "setting": 1, "state": 2, "widget": 3, "group": 4}
BIND_KINDS = {"file": 0, "int": 1, "str": 2, "ask": 3, "text": 4}
PAD_OUTPUTS = {"registers": 0, "joystick": 1}       # pad.h PAD_OUT_REGS, PAD_OUT_JOY
PAD_ROLES = ("face_right", "face_bottom", "face_left", "face_top", "l", "r", "select", "start")   # pad.h PAD_ROLE_RIGHT (0x40) .. PAD_ROLE_START (0x47)
PAD_DIRS = ("up", "down", "left", "right")          # pad.h PAD_SRC_UP (32) .. PAD_SRC_RIGHT (35)
# T239.3 P3-2 (D31): the maxima of a record are per core. The [sizes] table of a core's TOML B names them (tools/app_build.py has the whole table: its keys rec_* are these);
# the C reader is compiled with the same numbers (descrec.h), so the compiler and the board refuse the same record.
SIZE_LIMITS = {"rec_binds": "bind", "rec_values": "value", "rec_csettings": "cset", "rec_facts": "fact", "rec_tasks": "task", "rec_exts": "ext", "rec_ftypes": "ftype"}
SIZE_MODULE = {"record_bytes": "MAX_SIZE", "rec_keys": "KEYS_MAX", "rec_windows": "WINDOWS_MAX", "rec_blocks": "BLOCKS_MAX", "rec_props": "PROPS_MAX", "rec_fields": "FIELDS_MAX", "rec_states": "STATES_MAX"}


def apply_sizes(sizes):
    """Sets the maxima of this module from a [sizes] table (a dict of key -> int); keys that are not a record maximum are not this module's and are left alone."""
    for k, v in sizes.items():
        if k in SIZE_LIMITS or k in SIZE_MODULE:
            if isinstance(v, bool) or not isinstance(v, int) or v < 1:
                raise DescError("sizes: %s is a positive integer" % k)
        if k in SIZE_LIMITS:
            LIMITS[SIZE_LIMITS[k]] = v
        elif k in SIZE_MODULE:
            globals()[SIZE_MODULE[k]] = v


TABS_PER_AREA = 8                  # = MENU_TABS_MAX; the tab strip of the view has room for eight of them (TRX_TABS_MAX) (an application's hidden screens are tabs that are not on it)


class DescError(Exception):
    pass


# ---- a TOML subset ---------------------------------------------------------------------------------------------------------
# tomllib is 3.11; the build image has 3.10. This reads what the description uses: comments, [table] and [[array.of.tables]] with dotted
# names, key = value with strings (basic and literal), integers (decimal, 0x), booleans, arrays (also over several lines) and inline tables.

class _Toml:
    def __init__(self, text):
        self.s = text
        self.i = 0

    def err(self, msg):
        line = self.s.count("\n", 0, self.i) + 1
        raise DescError("TOML line %d: %s" % (line, msg))

    def ws(self, newlines=False):
        while self.i < len(self.s):
            c = self.s[self.i]
            if c in " \t\r" or (newlines and c == "\n"):
                self.i += 1
            elif c == "#":
                while self.i < len(self.s) and self.s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def key(self):
        m = re.compile(r'[A-Za-z0-9_-]+|"([^"\n]*)"').match(self.s, self.i)
        if not m:
            self.err("a key is expected")
        self.i = m.end()
        return m.group(1) if m.group(1) is not None else m.group(0)

    def dotted(self):
        parts = [self.key()]
        while True:
            self.ws()
            if self.i < len(self.s) and self.s[self.i] == ".":
                self.i += 1
                self.ws()
                parts.append(self.key())
            else:
                return parts

    def value(self):
        s = self.s
        if self.i >= len(s):
            self.err("a value is expected")
        c = s[self.i]
        if c == '"':
            return self.basic_string()
        if c == "'":
            j = s.find("'", self.i + 1)
            if j < 0 or "\n" in s[self.i:j]:
                self.err("unterminated string")
            v = s[self.i + 1:j]
            self.i = j + 1
            return v
        if c == "[":
            self.i += 1
            out = []
            while True:
                self.ws(True)
                if self.i < len(s) and s[self.i] == "]":
                    self.i += 1
                    return out
                out.append(self.value())
                self.ws(True)
                if self.i < len(s) and s[self.i] == ",":
                    self.i += 1
                elif self.i < len(s) and s[self.i] == "]":
                    self.i += 1
                    return out
                else:
                    self.err("',' or ']' is expected in an array")
        if c == "{":
            self.i += 1
            out = {}
            while True:
                self.ws()
                if self.i < len(s) and s[self.i] == "}":
                    self.i += 1
                    return out
                k = self.key()
                self.ws()
                if s[self.i:self.i + 1] != "=":
                    self.err("'=' is expected")
                self.i += 1
                self.ws()
                if k in out:
                    self.err("key %r twice" % k)
                out[k] = self.value()
                self.ws()
                if s[self.i:self.i + 1] == ",":
                    self.i += 1
                elif s[self.i:self.i + 1] != "}":
                    self.err("',' or '}' is expected in an inline table")
        if s.startswith("true", self.i):
            self.i += 4
            return True
        if s.startswith("false", self.i):
            self.i += 5
            return False
        m = re.compile(r"[+-]?(0x[0-9A-Fa-f_]+|[0-9][0-9_]*)").match(s, self.i)
        if not m:
            self.err("a value is expected")
        self.i = m.end()
        t = m.group(0).replace("_", "")
        return int(t, 16) if "0x" in t.lower() else int(t)

    def basic_string(self):
        s = self.s
        j = self.i + 1
        out = []
        esc = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
        while j < len(s) and s[j] != '"':
            if s[j] == "\n":
                self.err("unterminated string")
            if s[j] == "\\":
                if j + 1 >= len(s) or s[j + 1] not in esc:
                    self.err("an escape of a string is not known")
                out.append(esc[s[j + 1]])
                j += 2
            else:
                out.append(s[j])
                j += 1
        if j >= len(s):
            self.err("unterminated string")
        self.i = j + 1
        return "".join(out)

    def parse(self):
        root = {}
        cur = root
        while True:
            self.ws(True)
            if self.i >= len(self.s):
                return root
            if self.s.startswith("[[", self.i):
                self.i += 2
                self.ws()
                path = self.dotted()
                self.ws()
                if not self.s.startswith("]]", self.i):
                    self.err("']]' is expected")
                self.i += 2
                cur = self.array_table(root, path)
            elif self.s[self.i] == "[":
                self.i += 1
                self.ws()
                path = self.dotted()
                self.ws()
                if self.s[self.i:self.i + 1] != "]":
                    self.err("']' is expected")
                self.i += 1
                cur = root
                for p in path:
                    cur = cur.setdefault(p, {})
                    if isinstance(cur, list):
                        cur = cur[-1]
            else:
                k = self.key()
                self.ws()
                if self.s[self.i:self.i + 1] != "=":
                    self.err("'=' is expected")
                self.i += 1
                self.ws()
                if k in cur:
                    self.err("key %r twice" % k)
                cur[k] = self.value()
            self.ws()
            if self.i < len(self.s) and self.s[self.i] not in "\n#":
                self.err("unexpected text after a value")

    def array_table(self, root, path):
        cur = root
        for p in path[:-1]:
            nxt = cur.setdefault(p, {})
            cur = nxt[-1] if isinstance(nxt, list) else nxt
        lst = cur.setdefault(path[-1], [])
        if not isinstance(lst, list):
            self.err("%r is not an array of tables" % path[-1])
        d = {}
        lst.append(d)
        return d


def toml_loads(text):
    """tomllib's answer where there is one (3.11), the subset reader otherwise: the description is written in the subset, so both agree."""
    try:
        import tomllib
    except ImportError:
        return _Toml(text).parse()
    try:
        return tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise DescError("TOML: %s" % e)


# ---- the compiler -----------------------------------------------------------------------------------------------------------

def _only(d, allowed, what):
    for k in d:
        if k not in allowed:
            raise DescError("%s: unknown key %r (known: %s)" % (what, k, ", ".join(sorted(allowed))))


def _need(d, k, typ, what):
    if k not in d or not isinstance(d[k], typ) or isinstance(d[k], bool) != (typ is bool):
        raise DescError("%s: %r is missing or not a %s" % (what, k, typ.__name__))
    return d[k]


def _opt(d, k, typ, what, default=None):
    if k not in d:
        return default
    return _need(d, k, typ, what)


class _Strings:
    def __init__(self):
        self.buf = bytearray(b"\0")          # offset 0 is the empty string
        self.idx = {"": 0}

    def add(self, s):
        if s is None:
            return NONE
        if s not in self.idx:
            self.idx[s] = len(self.buf)
            self.buf += s.encode("utf-8") + b"\0"
        return self.idx[s]


UI_NODE_KEYS = {"primary": bool, "link": bool, "goto": str, "sub": str, "chip": str, "tone": str, "off": str, "why": str, "confirm": str, "label": str, "hide": str, "ask": str, "ask_state": str, "job": bool, "pick": str, "rename": bool, "view": str, "key": str, "none": str, "nokey": bool, "empty": str, "bkey": str, "ctrl": str, "legend": str, "open": str}
UI_TONES = ("muted", "cyan", "violet", "amber", "coral")
LAYOUTS = ("home", "desk", "split", "settings", "status", "library", "timeline", "terminal", "app", "grid")
UI_TAB_KEYS = {"title": str, "crumb": str, "side": str, "chip": str, "sheet": str, "sheetx": str, "nodes_first": bool, "sections": bool, "pages": str, "pages_head": str, "left": str, "label": str, "frame": str, "bar": str, "hint": str, "marks": str, "pos": str, "info": str, "glyph": str, "track": str,
               # an application's own screens (T104.3 D0, layouts app and grid): which tab is shown, where a key goes, and what the screen draws of the application's state
               "page": str,
               "hidden": bool, "lines": str, "under": str, "back": str, "follow": str, "lock": str, "keys": str, "enter": str, "rows": str, "input": str, "grid": str, "note": str,
               "status": str, "pct": str, "pct_label": str, "pct_note": str,
               "map1": str, "map1_s": str, "map1_d": str, "map1_t": str, "map2": str, "map2_s": str, "map2_d": str, "map2_t": str}


def _ui_word(what, k, v):
    if ";" in v or not v or v != v.strip():
        raise DescError("%s: ui %s is a text without ';' and without spaces around it" % (what, k))


def _ui_string(d, keys, what):
    """A ui inline table -> "key;key=value;..." (flags first in the order of keys, then values in the order of keys)."""
    if d is None:
        return None
    if not isinstance(d, dict):
        raise DescError("%s: ui is a table { key = value }" % what)
    _only(d, set(keys), what + " ui")
    words = []
    for k, typ in keys.items():
        if k not in d:
            continue
        v = d[k]
        if typ is bool:
            if v is not True:
                raise DescError("%s: ui %s is written 'true'" % (what, k))
            words.append(k)
        else:
            if not isinstance(v, str):
                raise DescError("%s: ui %s is a text" % (what, k))
            _ui_word(what, k, v)
            words.append("%s=%s" % (k, v))
    return ";".join(words) or None


def _ui_table(s, keys):
    """The inverse of _ui_string."""
    d = {}
    for w in (s or "").split(";"):
        if not w:
            continue
        k, _, v = w.partition("=")
        d[k] = True if keys.get(k) is bool else v
    return d


GROUP_WIDGETS = ("settings", "tasks", "media", "system")      # the widgets whose entries name a group (a system tab's groups are its pages, T228 R6)
HOTKEY_TASKS = 4                                    # T228 R1: the tasks that may declare a hotkey (coreapp/src/hotkeys.h HK_TASKS_MAX)


def _tab_groups(tb, tid, widget):
    """The comma list a tab stores in its `groups` field: the settings / tasks / media groups it lists, or - for an info tab - the lines it shows."""
    groups = _opt(tb, "groups", list, "tab " + tid, None)
    lines = _opt(tb, "lines", list, "tab " + tid, None)
    if groups is not None and (not groups or not all(isinstance(g, str) and re.fullmatch(r"[A-Za-z0-9_]+", g) for g in groups)):
        raise DescError("tab %s: groups are names" % tid)
    if groups and widget not in GROUP_WIDGETS:
        raise DescError("tab %s: groups belong to a settings, tasks, media or system tab" % tid)
    if lines is not None:
        if widget != "info":
            raise DescError("tab %s: lines belong to an info tab" % tid)
        if not lines or not all(isinstance(g, str) and g in INFO_LINES for g in lines) or len(set(lines)) != len(lines):
            raise DescError("tab %s: lines are some of %s, each once" % (tid, ", ".join(INFO_LINES)))
        if groups:
            raise DescError("tab %s: groups and lines together" % tid)
        return lines
    return groups


def _tab_pages(tb, tid, widget, groups):
    """T228 D18, R6: `pages` names groups of the tab: "g" is the page of the group g, "Name=g1+g2" one page named Name with the groups g1 and g2. Each group is on one
    page at most; a settings tab shows pages only with `sections`, a system tab shows its groups as pages and nothing else (menutree.cc TabPages)."""
    ui = tb.get("ui") or {}
    pages = ui.get("pages", "")
    if not isinstance(pages, str):
        return                                                  # _ui_string says what is wrong with it
    held = []
    for item in [p for p in pages.split(",") if p] if pages else []:
        name, _, members = item.partition("=")
        members = members.split("+") if _ else [name]
        if not re.fullmatch(r"[A-Za-z0-9_]{1,31}", name) or not all(members):
            raise DescError("tab %s: ui pages %r is a group, or Name=group+group" % (tid, item))
        for g in members:
            if g not in (groups or []):
                raise DescError("tab %s: ui pages names %r, a group the tab does not list" % (tid, g))
            if g in held:
                raise DescError("tab %s: ui pages names the group %r twice" % (tid, g))
            held.append(g)
    if len(pages) > 255:
        raise DescError("tab %s: ui pages is longer than 255 characters (the application reads that much)" % tid)
    if pages and widget == "settings" and not ui.get("sections"):
        raise DescError("tab %s: ui pages needs `sections` (a plain list shows its groups as they are)" % tid)
    if pages and widget not in ("settings", "system"):
        raise DescError("tab %s: ui pages belongs to a settings or system tab" % tid)
    if widget == "system" and groups and set(groups) != set(held):
        raise DescError("tab %s: a system tab shows its groups as pages only - name each of them in ui pages" % tid)
    if "pages_head" in ui and not pages:
        raise DescError("tab %s: ui pages_head heads the rows of ui pages, and there are none" % tid)


def _expand_apps(doc):
    """T104.3 D6: an entry that opens an application says so as `app = "trex_crt"` and `action = "write"`; it is the operation app.open with the application, the action
    and - for a file action - the file as its bindings. A node, a task or a file action; the result is what the entry would have been written as."""
    def one(e, what, has_file):
        if "app" not in e:
            if "action" in e and what != "filetype action":
                raise DescError("%s: action without app" % what)
            return
        if "op" in e or "bind" in e:
            raise DescError("%s: app and action stand for op and bind; give one or the other" % what)
        app = _need(e, "app", str, what)
        if not re.fullmatch(r"[a-z0-9_]{1,15}", app):
            raise DescError("%s: app %r is 1..15 characters of [a-z0-9_]" % (what, app))
        action = _opt(e, "action", str, what, "")
        e["op"] = "app.open"
        e["bind"] = [{"arg": "app", "str": app}, {"arg": "action", "str": action}, ({"arg": "file", "file": True} if has_file else {"arg": "file", "str": ""})]
        del e["app"]
        e.pop("action", None)
    for e in doc.get("file_action", []):
        one(e, "file_action", True)
    for e in doc.get("node", []):
        one(e, "node", False)
    for e in doc.get("task", []):
        one(e, "task", False)
    for ft in doc.get("filetype", []):
        for e in ft.get("action", []):
            one(e, "filetype action", True)


# the run-time lists the application serves to a core's choice row (menubuild.cc menu_pick_fill); video.modes is the Picture row of the base alone
CORE_PICKS = ("hotkeys.keys", "hotkeys.modifiers", "view.schemes")


def compile_desc(doc, merged=(), app_base=False):
    """doc: the parsed TOML (a dict). Returns the record's bytes. `merged`: the names of the libraries whose menus were merged into it (merge_lib_menus).
    `app_base`: the document is the SDK's base UI TOML (coreapp/base_ui.toml, T239.3 D32), whose [[core_setting]] rows are the application's own: they may take the keys and operations
    of Picture, Scale, Smoothing and Frame follow; no core's record may."""
    _expand_apps(doc)
    _only(doc, {"area", "node", "file_action", "setting", "hide", "rename", "tab", "poll", "task", "media", "filetype", "core_setting", "lists", "machine_info",
                "pad", "restart", "key", "window", "group_route", "place"} | set(SEC_KEYS) | set(TOMLA_KEYS), "description")
    if any(k in doc for k in TOMLA_KEYS):
        if not all(k in doc for k in TOMLA_KEYS) or any(k not in TOMLA_KEYS for k in doc):
            raise DescError("TOML A (version 7) is [io], [identity] and [[block]] and nothing else: a file has all three and no menu table, setting or window")
    st = _Strings()
    t = {k: [] for k in ORDER}

    def bind_list(binds, what):
        out = []
        for b in binds or []:
            if not isinstance(b, dict):
                raise DescError("%s: a binding is a table { arg = .., file|int|str|ask|text = .. }" % what)
            _only(b, {"arg"} | set(BIND_KINDS), what + " binding")
            arg = _need(b, "arg", str, what + " binding")
            kinds = [k for k in BIND_KINDS if k in b]
            if len(kinds) != 1:
                raise DescError("%s: binding %r needs exactly one of file, int, str, ask, text" % (what, arg))
            k = kinds[0]
            iv, sv = 0, NONE
            if k == "int":
                iv = _need(b, "int", int, what)
                if not -2 ** 31 <= iv < 2 ** 31:
                    raise DescError("%s: binding %r: the number does not fit 32 bits" % (what, arg))
            elif k == "str":
                sv = st.add(_need(b, "str", str, what))
            elif b[k] is not True:
                raise DescError("%s: binding %r: %s is written 'true'" % (what, arg, k))
            out.append((st.add(arg), BIND_KINDS[k], iv & 0xFFFFFFFF, sv))
        first = len(t["bind"])
        t["bind"].extend(out)
        return first, len(out)

    areas = _opt(doc, "area", list, "description", [])
    for a in areas:
        _only(a, {"key", "name", "shifted", "tab"}, "area")
        key = _need(a, "key", str, "area")
        if not re.fullmatch(r"F[1-8]|APP", key):
            raise DescError("area key %r: F1 .. F8, or APP (an application's own screens)" % key)
        tabs = _opt(a, "tab", list, "area %s" % key, [])
        if not tabs or len(tabs) > TABS_PER_AREA:
            raise DescError("area %s: 1 to %d tabs" % (key, TABS_PER_AREA))
        first = len(t["tab"])
        seen_id, seen_n = set(), set()
        for tb in tabs:
            _only(tb, {"id", "name", "widget", "groups", "lines", "n", "layout", "ui"}, "tab")
            tid = _need(tb, "id", str, "tab")
            if not re.fullmatch(r"[a-z0-9_]{1,31}", tid):
                raise DescError("tab id %r: lower case, digits and _, at most 31" % tid)
            widget = _need(tb, "widget", str, "tab " + tid)
            if widget not in WIDGETS:
                raise DescError("tab %s: widget %r (one of %s)" % (tid, widget, ", ".join(WIDGETS)))
            n = _opt(tb, "n", int, "tab " + tid, 0)
            if not 0 <= n <= 99:
                raise DescError("tab %s: n is 0 .. 99" % tid)
            if tid in seen_id or (n and n in seen_n):
                raise DescError("area %s: tab %r or its number twice" % (key, tid))
            seen_id.add(tid)
            seen_n.add(n)
            groups = _tab_groups(tb, tid, widget)
            layout = _opt(tb, "layout", str, "tab " + tid, None)
            if layout is None and "ui" in tb and not set(tb["ui"]) <= {"sections", "pages", "pages_head"}:
                raise DescError("tab %s: ui without a layout (only `sections`, `pages` and `pages_head` stand alone)" % tid)
            if layout is not None and layout not in LAYOUTS:
                raise DescError("tab %s: layout %r (one of %s)" % (tid, layout, ", ".join(LAYOUTS)))
            _tab_pages(tb, tid, widget, groups)
            parent = (tb.get("ui") or {}).get("page")
            if parent is not None and (parent == tid or parent not in seen_id or n):    # T228 R1: a page of an earlier tab of this area, with no number
                raise DescError("tab %s: ui page names %r, which is no earlier tab of area %s, or the page has a number (n)" % (tid, parent, key))
            lay = None
            if layout is not None or "ui" in tb:                         # no layout: the name is empty, the tab stays a list (T228 `sections`)
                params = _ui_string(tb.get("ui"), UI_TAB_KEYS, "tab " + tid)
                lay = (layout or "") + (";" + params if params else "")
            t["tab"].append((st.add(tid), st.add(_opt(tb, "name", str, "tab " + tid, "")), n, WIDGETS.index(widget),
                             st.add(",".join(groups)) if groups else NONE, st.add(lay) if lay else 0))
        t["area"].append((st.add(key), st.add(_opt(a, "name", str, "area " + key, None)), 1 if _opt(a, "shifted", bool, "area", False) else 0,
                          first, len(tabs)))

    page_keys = {}
    for n in _opt(doc, "node", list, "description", []):
        _only(n, {"anchor", "kind", "label", "op", "state", "bind", "order", "when_state", "capability", "id", "closes_menu", "ui"}, "node")
        label = _need(n, "label", str, "node")
        kind = _need(n, "kind", str, "node " + label)
        if kind not in NODE_KINDS:
            raise DescError("node %s: kind %r (one of %s)" % (label, kind, ", ".join(NODE_KINDS)))
        anchor = _need(n, "anchor", str, "node " + label)
        if not re.fullmatch(r"(F[1-8]|APP)/[a-z0-9_]+(/[A-Za-z0-9_]+)?", anchor):
            raise DescError("node %s: anchor %r is area/tab or area/tab/group" % (label, anchor))
        order = _opt(n, "order", int, "node " + label, 0)
        if not -32768 <= order <= 32767:
            raise DescError("node %s: order does not fit 16 bits" % label)
        fb, nb = bind_list(_opt(n, "bind", list, "node " + label, []), "node " + label)
        ui = _ui_string(_opt(n, "ui", dict, "node " + label, None), UI_NODE_KEYS, "node " + label)
        if ui:
            tone = _ui_table(ui, UI_NODE_KEYS).get("tone")
            if tone is not None and tone not in UI_TONES:
                raise DescError("node %s: ui tone %r (one of %s)" % (label, tone, ", ".join(UI_TONES)))
            goto = _ui_table(ui, UI_NODE_KEYS).get("goto")
            if goto is not None and not re.fullmatch(r"(F[1-8]|APP)(/[a-z0-9_]+)?", goto):
                raise DescError("node %s: ui goto %r is an area key or area/tab" % (label, goto))
        if ui:                                                      # a letter belongs to its page: no two elements of a page claim one, none claims a digit or a key of the navigation
            uit = _ui_table(ui, UI_NODE_KEYS)
            page = "/".join(anchor.split("/")[:2])
            cv = uit.get("ctrl")
            if cv is not None and not (len(cv) == 1 and cv.isalpha() and cv == cv.upper()):
                raise DescError("node %s: ui ctrl is one upper case letter (C= + that letter runs the node)" % label)
            for kk in ("key", "bkey"):
                kv = uit.get(kk)
                if kv is None:
                    continue
                if kk == "key" and anchor.startswith("APP/") and re.fullmatch(r"F[1-8]", kv):      # an application's own screen names its function keys ("F3 DUMP" in the foot, menuctl.cc)
                    continue
                if not (len(kv) == 1 and kv.isalpha() and kv == kv.upper()):
                    raise DescError("node %s: ui %s is one upper case letter (digits and the F keys belong to the navigation)" % (label, kk))
                if kv in page_keys.setdefault(page, {}):
                    raise DescError("page %s: the letter %s is claimed by %s and by %s" % (page, kv, page_keys[page][kv], label))
                page_keys[page][kv] = label
        t["node"].append((st.add(anchor), NODE_KINDS[kind], st.add(label), st.add(_opt(n, "op", str, "node", None)),
                          st.add(_opt(n, "state", str, "node", None)), order & 0xFFFFFFFF, st.add(_opt(n, "when_state", str, "node", None)),
                          st.add(_opt(n, "capability", str, "node", None)), st.add(_opt(n, "id", str, "node", None)), fb, nb,
                          1 if _opt(n, "closes_menu", bool, "node", False) else 0, st.add(ui) if ui else 0))

    for f in _opt(doc, "file_action", list, "description", []):
        _only(f, {"ext", "label", "op", "bind", "size_in", "closes_menu"}, "file_action")
        label = _need(f, "label", str, "file_action")
        exts = _need(f, "ext", list, "file_action " + label)
        if not exts or not all(isinstance(e, str) and e.startswith(".") and len(e) > 1 for e in exts):
            raise DescError("file_action %s: ext is a list of extensions with a dot" % label)
        sizes = _opt(f, "size_in", list, "file_action " + label, [])
        if not all(isinstance(z, int) and not isinstance(z, bool) and 0 < z < 2 ** 32 for z in sizes):
            raise DescError("file_action %s: size_in are sizes in bytes" % label)
        fe = len(t["ext"])
        t["ext"].extend((st.add(e.lower()),) for e in exts)
        fz = len(t["size"])
        t["size"].extend((z,) for z in sizes)
        fb, nb = bind_list(_opt(f, "bind", list, "file_action " + label, []), "file_action " + label)
        t["action"].append((st.add(label), st.add(_need(f, "op", str, "file_action " + label)), fe, len(exts), fz, len(sizes), fb, nb,
                            1 if _opt(f, "closes_menu", bool, "file_action", False) else 0, 0))

    for s in _opt(doc, "setting", list, "description", []):
        _only(s, {"group", "key", "values", "range", "default", "apply"}, "setting")
        key = _need(s, "key", str, "setting")
        apply = _need(s, "apply", str, "setting " + key)
        _app_owned("setting " + key, key, apply)
        group = _opt(s, "group", str, "setting " + key, None)
        if ("values" in s) == ("range" in s):
            raise DescError("setting %s: exactly one of values and range" % key)
        fv, nv, lo, hi = len(t["value"]), 0, 0, 0
        if "values" in s:
            vals = _need(s, "values", list, "setting " + key)
            if len(vals) < 2 or not all(isinstance(v, str) and v for v in vals):
                raise DescError("setting %s: values are at least two non-empty labels" % key)
            t["value"].extend((st.add(v),) for v in vals)
            nv = len(vals)
            default = _opt(s, "default", int, "setting " + key, 0)
            if not 0 <= default < nv:
                raise DescError("setting %s: default %d is not an index of values" % (key, default))
        else:
            rng = _need(s, "range", list, "setting " + key)
            if len(rng) != 2 or not all(isinstance(v, int) for v in rng) or rng[1] <= rng[0]:
                raise DescError("setting %s: range = [lo, hi] with hi above lo" % key)
            lo, hi = rng
            default = _opt(s, "default", int, "setting " + key, lo)
            if not lo <= default <= hi:
                raise DescError("setting %s: default %d is outside the range" % (key, default))
        t["setting"].append((st.add(group), st.add(key), fv, nv, lo & 0xFFFFFFFF, hi & 0xFFFFFFFF, default & 0xFFFFFFFF, st.add(apply)))

    for h in _opt(doc, "hide", list, "description", []):
        if not isinstance(h, str) or not h:
            raise DescError("hide: node ids")
        t["hide"].append((st.add(h),))
    for r in _opt(doc, "rename", list, "description", []):
        _only(r, {"id", "label"}, "rename")
        t["rename"].append((st.add(_need(r, "id", str, "rename")), st.add(_need(r, "label", str, "rename"))))

    for tb in _opt(doc, "tab", list, "description", []):
        _only(tb, {"area", "id", "name", "widget", "groups", "lines", "n", "layout", "ui"}, "tab")
        area = _need(tb, "area", str, "tab")
        if not re.fullmatch(r"F[1-8]", area):
            raise DescError("tab area %r: F1 .. F8" % area)
        tid = _need(tb, "id", str, "tab")
        if not re.fullmatch(r"[a-z0-9_]{1,31}", tid):
            raise DescError("tab id %r: lower case, digits and _, at most 31" % tid)
        widget = _need(tb, "widget", str, "tab " + tid)
        if widget not in WIDGETS:
            raise DescError("tab %s: widget %r (one of %s)" % (tid, widget, ", ".join(WIDGETS)))
        n = _opt(tb, "n", int, "tab " + tid, 0)
        if not 0 <= n <= 99:
            raise DescError("tab %s: n is 0 .. 99" % tid)
        groups = _tab_groups(tb, tid, widget)
        _tab_pages(tb, tid, widget, groups)
        layout = _opt(tb, "layout", str, "tab " + tid, None)         # T239.3 L-3: how the tab is drawn, as the route's own tabs say it (LibRecord::tab_layouts)
        if layout is None and "ui" in tb and not set(tb["ui"]) <= {"sections", "pages", "pages_head"}:
            raise DescError("tab %s: ui without a layout (only `sections`, `pages` and `pages_head` stand alone)" % tid)
        if layout is not None and layout not in LAYOUTS:
            raise DescError("tab %s: layout %r (one of %s)" % (tid, layout, ", ".join(LAYOUTS)))
        lay = None
        if layout is not None or "ui" in tb:
            params = _ui_string(tb.get("ui"), UI_TAB_KEYS, "tab " + tid)
            lay = (layout or "") + (";" + params if params else "")
        t["ctab"].append((st.add(area), st.add(tid), st.add(_need(tb, "name", str, "tab " + tid)), WIDGETS.index(widget), st.add(",".join(groups)) if groups else NONE, n,
                          st.add(lay) if lay else 0))

    # ---- the core's own menu (T104.2 D10)
    def text_key(d, k, what, default=None):
        v = _opt(d, k, str, what, default)
        if v is not None and not v:
            raise DescError("%s: %r is empty" % (what, k))
        return v

    def group_name(d, what):
        g = _opt(d, "group", str, what, None)
        if g is not None and not re.fullmatch(r"[A-Za-z0-9_]+", g):
            raise DescError("%s: group %r is a name of letters, digits and _" % (what, g))
        return g

    hotkeys = []
    for tk in _opt(doc, "task", list, "description", []):
        _only(tk, {"group", "label", "op", "bind", "closes_menu", "ask", "ask_state", "pick_dir", "pick_label", "hotkey", "show_state", "job"}, "task")
        label = _need(tk, "label", str, "task")
        what = "task " + label
        fb, nb = bind_list(_opt(tk, "bind", list, what, []), what)
        ask = text_key(tk, "ask", what)
        if ask is None and any(k in tk for k in ("ask_state", "pick_dir", "pick_label")):
            raise DescError("%s: ask_state, pick_dir and pick_label need an ask" % what)
        if "pick_label" in tk and not tk.get("pick_dir"):
            raise DescError("%s: pick_label needs pick_dir" % what)
        hk = _opt(tk, "hotkey", str, what, "")
        if hk and not (len(hk) == 1 and hk.isalnum() and hk.isascii() and hk == hk.upper()):
            raise DescError("%s: hotkey is one upper case letter or digit (the default key with the core's hotkey modifier)" % what)
        if hk and ask is not None:
            raise DescError("%s: a hotkey runs the task while the core runs, where nothing can be asked; a task with ask has none" % what)
        if hk:
            hotkeys.append(label)
        job = _opt(tk, "job", bool, what, False)
        if job and (hk or tk.get("pick_dir")):
            raise DescError("%s: a job is a stepped operation the menu runs with its progress line; it has no hotkey (nothing shows the progress while the core runs) and no pick_dir (that task is stepped already)" % what)
        flags = (1 if _opt(tk, "closes_menu", bool, what, False) else 0) | (2 if _opt(tk, "pick_dir", bool, what, False) else 0) | (4 if job else 0) | (ord(hk) << 8 if hk else 0)   # bit 2 job, bits 15:8 the hotkey
        show = _opt(tk, "show_state", str, what, None)                   # the row is listed only while this condition holds (contrib.h cond_holds); bits 31:16 the text's offset + 1, as a file action's
        if show:
            so = st.add(show) + 1
            if so >= 0x10000:
                raise DescError("%s: show_state lies beyond the first 64 KB of the strings" % what)
            flags |= so << 16
        t["task"].append((st.add(label), st.add(_need(tk, "op", str, what)), fb, nb, flags, st.add(ask), st.add(text_key(tk, "ask_state", what)),
                          st.add(text_key(tk, "pick_label", what)), st.add(group_name(tk, what))))
    if len(hotkeys) > HOTKEY_TASKS:
        raise DescError("tasks: %d declare a hotkey (%s); the application takes %d (coreapp/src/hotkeys.h HK_TASKS_MAX)" % (len(hotkeys), ", ".join(hotkeys), HOTKEY_TASKS))
    hk_keys = [chr((e[4] >> 8) & 0xFF) for e in t["task"] if (e[4] >> 8) & 0xFF]
    if len(set(hk_keys)) != len(hk_keys):
        raise DescError("tasks: two hotkeys have the same default key (%s)" % ", ".join(hk_keys))

    replaces = []              # (media index, label): the file action that replaces the file of a filled slot (MediaSlot::replace), section 17
    for m in _opt(doc, "media", list, "description", []):
        _only(m, {"group", "name", "state", "eject_op", "eject_bind", "exts", "mount", "empty_tasks", "mounted_tasks", "eject_closes", "replace"}, "media")
        name = _need(m, "name", str, "media")
        what = "media " + name
        if "eject_op" not in m and "eject_bind" in m:
            raise DescError("%s: eject_bind without eject_op" % what)
        fb, nb = bind_list(_opt(m, "eject_bind", list, what, []), what)
        if "replace" in m:
            replaces.append((len(t["media"]), text_key(m, "replace", what)))
        t["media"].append((st.add(name), st.add(_need(m, "state", str, what)), st.add(text_key(m, "eject_op", what)), fb, nb, st.add(group_name(m, what)),
                           st.add(text_key(m, "exts", what)), st.add(text_key(m, "mount", what)), st.add(text_key(m, "empty_tasks", what)),
                           st.add(text_key(m, "mounted_tasks", what)), 1 if _opt(m, "eject_closes", bool, what, False) else 0))

    seen_ft = set()
    sheet_keys = {}
    fact_labels = set()
    def file_actions(lst, what):
        first = len(t["fact"])
        for f in lst:
            fact_labels.add(f.get("label"))
            _only(f, {"label", "op", "bind", "closes_menu", "ask", "ask_off", "size_in", "key", "show_state", "repeat_op", "show"}, what)
            label = _need(f, "label", str, what)
            w2 = "%s action %s" % (what, label)
            ask = text_key(f, "ask", w2)
            rep = text_key(f, "repeat_op", w2)                  # T228 R21: the action is offered once per N, N from this operation; the ask word holds its name
            if rep is not None and (ask is not None or "ask_off" in f):
                raise DescError("%s: repeat_op and ask do not go together" % w2)
            if rep is not None:
                ask = rep
            if "ask_off" in f and ask is None:
                raise DescError("%s: ask_off needs an ask" % w2)
            off = _opt(f, "ask_off", int, w2, 0)
            if not -32768 <= off <= 32767:
                raise DescError("%s: ask_off does not fit 16 bits" % w2)
            sizes = _opt(f, "size_in", list, w2, [])
            if not all(isinstance(z, int) and not isinstance(z, bool) and 0 < z < 2 ** 32 for z in sizes):
                raise DescError("%s: size_in are sizes in bytes" % w2)
            fz = len(t["size"])
            t["size"].extend((z,) for z in sizes)
            fb, nb = bind_list(_opt(f, "bind", list, w2, []), w2)
            key = _opt(f, "key", str, w2, "")
            if key and key in sheet_keys.setdefault(what, {}):
                raise DescError("%s: the sheet letter %s is claimed by %s and by %s" % (what, key, sheet_keys[what][key], label))
            if key:
                sheet_keys[what][key] = label
            if key and not (len(key) == 1 and key.isalnum() and key == key.upper()):
                raise DescError("%s: key is one upper case letter or digit (the letter that runs the item in the sheet)" % w2)
            fl = (1 if _opt(f, "closes_menu", bool, w2, False) else 0) | (ord(key) << 8 if key else 0)      # bits 15:8 the key
            show = _opt(f, "show_state", str, w2, None)                  # the action is offered only while this condition holds (contrib.h cond_holds); bits 31:16 the text's offset + 1
            if show:
                so = st.add(show) + 1
                if so >= 0x10000:
                    raise DescError("%s: show_state lies beyond the first 64 KB of the strings" % w2)
                fl |= so << 16
            fl |= (2 if rep is not None else 0) | (4 if _opt(f, "show", bool, w2, False) else 0)       # bit 1 repeat (ask = the count's op), bit 2 shows the op's text
            t["fact"].append((st.add(label), st.add(_need(f, "op", str, w2)), fb, nb, fl, st.add(ask),
                              off & 0xFFFFFFFF, fz, len(sizes)))
        return first, len(lst)

    for ft in _opt(doc, "filetype", list, "description", []):
        _only(ft, {"ext", "inner_ext", "action", "inner"}, "filetype")
        ext = _need(ft, "ext", str, "filetype")
        if not ext.startswith(".") or len(ext) < 2 or ext != ext.lower():
            raise DescError("filetype %r: a lower case extension with a dot" % ext)
        if ext in seen_ft:
            raise DescError("filetype %s twice" % ext)
        seen_ft.add(ext)
        acts = _opt(ft, "action", list, "filetype " + ext, [])
        inner = _opt(ft, "inner", list, "filetype " + ext, [])
        if not acts:
            raise DescError("filetype %s: at least one [[filetype.action]]" % ext)
        inner_ext = text_key(ft, "inner_ext", "filetype " + ext)
        if bool(inner) != (inner_ext is not None) or (inner_ext is not None and not inner_ext.startswith(".")):
            raise DescError("filetype %s: inner_ext (with a dot) and [[filetype.inner]] go together" % ext)
        fa, na = file_actions(acts, "filetype " + ext)
        fi, ni = file_actions(inner, "filetype " + ext + " inner")
        t["ftype"].append((st.add(ext), st.add(inner_ext), fa, na, fi, ni))

    # a media slot's drive list names its entries by label: a typo would silently drop the row, so every name has to exist
    task_labels = {tk.get("label") for tk in _opt(doc, "task", list, "description", [])}
    for m in _opt(doc, "media", list, "description", []):
        what = "media " + m["name"]
        if m.get("mount") is not None and m["mount"] not in fact_labels:
            raise DescError("%s: mount %r is not the label of a file action of any filetype" % (what, m["mount"]))
        for k_ in ("empty_tasks", "mounted_tasks"):
            for lab in (m.get(k_) or "").split(","):
                if lab and lab not in task_labels:
                    raise DescError("%s: %s names %r, which is not a task" % (what, k_, lab))

    seen_set = set()
    setbinds = []
    lists = _opt(doc, "lists", dict, "description", {})
    for cs in _opt(doc, "core_setting", list, "description", []):
        _only(cs, {"key", "group", "values", "list", "range", "unit", "default", "when", "apply", "base", "text", "secret", "maxlen", "show_state", "lock_state", "label", "empty", "pick", "reg_bind", "file", "romset_choice"}, "core_setting")
        key = _need(cs, "key", str, "core_setting")
        what = "core_setting " + key
        if key.lower() in seen_set:
            raise DescError("%s: the key is taken" % what)
        choice = _opt(cs, "romset_choice", bool, what, False)
        if choice:
            # the third form (desc.h Setting::bind): the item that chooses the member of the core's rom set; no operation, no register bits, and its values are the set's labels
            rs = doc.get("romset")
            if "reg_bind" in cs or "apply" in cs or any(k in cs for k in ("values", "list", "range", "text", "secret", "base")):
                raise DescError("%s: romset_choice takes no apply, reg_bind, values, list, range, text, secret or base (its values are the rom set's labels)" % what)
            if not isinstance(rs, dict) or rs.get("choice_key") != key or not isinstance(rs.get("max"), int) or not isinstance(rs.get("auto_label"), str):
                raise DescError("%s: romset_choice needs a [romset] whose choice_key is this key" % what)
        elif ("reg_bind" in cs) == ("apply" in cs):
            raise DescError("%s: exactly one of apply (an operation) and reg_bind (the register bits it is, T239.3 D17)" % what)
        if not app_base:
            _app_owned(what, key, cs.get("apply") or "")
        list_pick = None                     # a choice row's pick: the name of the list that fills its values at run time (a text row's pick is its own, below)
        if "pick" in cs and not (cs.get("text") or cs.get("secret") or cs.get("file")):
            list_pick = _opt(cs, "pick", str, what, None)
            if not re.fullmatch(r"[a-z0-9_.]{1,40}", list_pick) or choice or "reg_bind" in cs or (not app_base and list_pick not in CORE_PICKS):
                raise DescError("%s: pick on a choice among values names the list that fills the values at run time (the values are what the row shows until then); no romset_choice or reg_bind; a core's row names one of %s" % (what, ", ".join(CORE_PICKS)))
        seen_set.add(key.lower())
        when = _opt(cs, "when", str, what, "live")
        if when not in WHEN:
            raise DescError("%s: when is live or reset" % what)
        base = _opt(cs, "base", int, what, 0)
        wcode = WHEN[when]
        kind_code = 0                       # Setting::kind: 0 a choice among values, 1 a text, 2 a secret text, 3 a file the person selects
        if _opt(cs, "file", bool, what, False):
            # T239.3 D17: a file the person selects in a file selector (Setting::kind 3): the text is the file's path, none selected by default; `apply` is the operation that checks a
            # chosen path and keeps it (one text argument "path"). The time code keeps its live / reset; FILE_FLAG says the kind (the reader gives values[0] back as "")
            if cs.get("text") or cs.get("secret") or any(k in cs for k in ("values", "list", "range", "unit", "default", "maxlen", "empty", "pick", "base", "reg_bind")) or "apply" not in cs:
                raise DescError("%s: a file setting has an apply and none of text, secret, values, list, range, unit, default, maxlen, empty, pick, base, reg_bind" % what)
            kind_code = 3
            wcode |= FILE_FLAG
            vals = [" "]                    # the record has no empty strings
            default = 0
        elif _opt(cs, "text", bool, what, False) or _opt(cs, "secret", bool, what, False):
            # a text the person types: values[0] is its default, base its longest length (T104.2 UI)
            vals = [_opt(cs, "default", str, what, "")]
            wcode = 3 if _opt(cs, "secret", bool, what, False) else 2
            kind_code = wcode - 1
            base = _opt(cs, "maxlen", int, what, 40)
            default = 0
            if vals[0] == "":
                vals = [" "]                # the record has no empty strings; a blank is trimmed at the reader's use
            pick = _opt(cs, "pick", str, what, "")
            if _opt(cs, "empty", str, what, "") or pick:
                vals.append(cs.get("empty") or "Not set")    # what the row shows while the text is empty (values[1])
            if pick:
                vals.append(pick)           # T228 R21: the state whose text holds the rows of the list RETURN opens (picklist.h); values[2]
        elif choice:
            vals = [rs["auto_label"]] + ["-"] * rs["max"]       # romsetspec.h ROMSET_LABELS: Auto's text, then "-" for each place of the set
            default = _opt(cs, "default", int, what, 0)
            if not 0 <= default < len(vals):
                raise DescError("%s: default %s is not an index of the rom set's choice" % (what, default))
        else:
            if "list" in cs:
                vals = lists.get(cs["list"])
                if not isinstance(vals, list):
                    raise DescError("%s: no list named %s" % (what, cs["list"]))
            elif "range" in cs:
                rg = cs["range"]
                if not (isinstance(rg, list) and len(rg) == 2 and all(isinstance(x, int) for x in rg) and rg[0] < rg[1]):
                    raise DescError("%s: range is [lo, hi]" % what)
                unit = _opt(cs, "unit", str, what, "")
                vals = ["%d%s" % (x, unit) for x in range(rg[0], rg[1] + 1)]
                if "base" not in cs:
                    base = rg[0]
            else:
                vals = _need(cs, "values", list, what)
            if len(vals) < (1 if list_pick else 2) or not all(isinstance(v, str) and v for v in vals):
                raise DescError("%s: values are at least two non-empty labels (one with a pick)" % what)
            default = cs.get("default", 0)
            if isinstance(default, str):
                if default not in vals:
                    raise DescError("%s: default %s is not one of the values" % (what, default))
                default = vals.index(default)
            elif "range" in cs:
                default = default - cs["range"][0]
            if not isinstance(default, int) or not 0 <= default < len(vals):
                raise DescError("%s: default %s is not an index of values" % (what, default))
        if not -2 ** 31 <= base < 2 ** 31:
            raise DescError("%s: base does not fit 32 bits" % what)
        show = _opt(cs, "show_state", str, what, None)         # the row is listed only while this condition holds (contrib.h cond_holds); packed above the time code
        if show:
            wcode |= (st.add(show) + 1) << 8
        if list_pick:
            vals = list(vals) + [list_pick]                      # T239.3 D32: the row's pick (the run-time list), the value string before the lock state; flag 16 of the time code
            wcode |= 16
        lock = _opt(cs, "lock_state", str, what, None)         # T228 R21: while this state has a text the row shows it in place of the value and refuses edits (FW set_override); the record keeps it as the last value string, flag 4 of the time code
        if lock:
            vals = list(vals) + [lock]
            wcode |= 4
        fv = len(t["value"])
        big = tuple(vals) if len(vals) > 256 else None       # a long range (a number with hundreds of steps) is stored once, however many settings share it
        if big is not None and big in t.setdefault("_big", {}):
            fv = t["_big"][big]
        else:
            t["value"].extend((st.add(v),) for v in vals)
            if big is not None:
                t["_big"][big] = fv
        if "reg_bind" in cs:
            rb = _need(cs, "reg_bind", dict, what)
            if kind_code or len(vals) < 2 or base != 0:
                raise DescError("%s: reg_bind is for a choice among values (no text, no file, no base)" % what)
            if "bytes" in rb and len(rb["bytes"]) != len(vals):
                raise DescError("%s: reg_bind bytes has one entry per value (%d)" % (what, len(vals)))
            setbinds.append((len(t["cset"]), rb))
        t["cset"].append((st.add(key), st.add(group_name(cs, what)), fv, len(vals), default, wcode, st.add(cs.get("apply") if "apply" in cs else None), base & 0xFFFFFFFF, st.add(_opt(cs, "label", str, what, None))))

    poll_op, poll_ms = NONE, 0
    poll = _opt(doc, "poll", dict, "description", None)
    if poll is not None:
        _only(poll, {"op", "period_ms"}, "poll")
        poll_op = st.add(_need(poll, "op", str, "poll"))
        poll_ms = _need(poll, "period_ms", int, "poll")
        if not 1 <= poll_ms <= 60000:
            raise DescError("poll: period_ms is 1 .. 60000")
    for m_ in merged:
        if not re.fullmatch(r"[a-z0-9_]{1,15}", m_):
            raise DescError("merged library name %r: 1 to 15 characters of a-z, 0-9 and _" % m_)
    merged_off = st.add(",".join(merged)) if merged else NONE
    mi = _opt(doc, "machine_info", str, "description", None)
    mi_off = st.add(mi) if mi else NONE
    pad_words = _pad_words(_opt(doc, "pad", dict, "description", None), t, st)
    restart_words = _restart_words(_opt(doc, "restart", dict, "description", None))
    for k in ORDER:
        if len(t[k]) > LIMITS[k]:
            raise DescError("%d %s entries; the board takes %d" % (len(t[k]), k, LIMITS[k]))
    sect_bytes = _sections(doc, setbinds, st, replaces)
    # layout: header, tables in ORDER, strings, then the data sections (version 6; TOML A is version 7 with the same header)
    version, header = (7 if TOMLA_KEYS[0] in doc else 6, HEADER_V6) if sect_bytes else (5, HEADER_V5) if pad_words or restart_words else (4, HEADER)
    pos = header
    offs = {}
    for k in ORDER:
        offs[k] = pos if t[k] else 0
        pos += ESIZE[k] * len(t[k])
    str_off = pos
    sect_off = str_off + len(st.buf) if sect_bytes else 0
    total = str_off + len(st.buf) + len(sect_bytes)
    if total > MAX_SIZE:
        raise DescError("the record is %d bytes; the board takes %d" % (total, MAX_SIZE))
    out = bytearray(struct.pack("<IHHIIII", MAGIC, version, header, total, (1 if t["area"] else 0) | (2 if any(t[k] for k in OWN) else 0) | (4 if sect_bytes else 0), str_off, len(st.buf)))
    for k in ORDER:
        out += struct.pack("<II", offs[k], len(t[k]))
    out += struct.pack("<IIII", poll_op, poll_ms, merged_off, mi_off)
    if version >= 5:
        out += struct.pack("<6I", *(pad_words or (NONE, 0, 0, 0, 0, NONE)))
        out += struct.pack("<II", *(restart_words or (0, 0)))
    if version >= 6:
        out += struct.pack("<II", sect_off, len(sect_bytes))
    assert len(out) == header, len(out)
    for k in ORDER:
        for e in t[k]:
            out += struct.pack("<%dI" % (ESIZE[k] // 4), *e)
    out += st.buf
    out += sect_bytes
    assert len(out) == total
    return bytes(out)


def compile_text(text, path=None):
    """path: the file the text is from; the TOML A it names (`toml_a`) is looked up beside it."""
    return compile_desc(resolve_toml_a_refs(toml_loads(text), path))


# ---- a register of the description named from the core's TOML A (T239.3 D30 condition 3 and 4, plan step V-2) ----------------------------------------------
# A core's gateware facts stand once, in its TOML A; its description (TOML B, desc.toml) keeps the application's data. A number that is a register of the gateware is
# written as the name of the property, "block.property" (a register number: the address of the register minus the address of the identity block, which is
# offset 0 of the core's window), or as a state of it, "block.property.state" (its value). The file is named by the top-level key `toml_a = "<file>"`, a path from the
# description's own directory; the key is read here and is not part of the record. Only these keys take a name: reg, reg_* , fb_base and fb_stride. The record is
# the same bytes as with the numbers written out.
TOML_A_REF_KEYS = ("reg", "fb_base", "fb_stride")
_REF_RE = re.compile(r"[a-z0-9_]{1,31}(\.[a-z0-9_]{1,31}){1,2}")


def _is_ref_key(k):
    return k in TOML_A_REF_KEYS or (isinstance(k, str) and k.startswith("reg_"))


def toml_a_names(tomla):
    """{ "block.property": register number, "block.property.state": value } of a TOML A document (checked by compile_desc first); the register number counts from the
    address of the identity block."""
    import copy
    compile_desc(copy.deepcopy(tomla))
    base = next(b["address"] for b in tomla["block"] if b["name"] == tomla["identity"]["block"])
    out = {}
    for b in tomla["block"]:
        for p in b.get("property", []):
            nm = "%s.%s" % (b["name"], p["name"])
            out[nm] = b["address"] + p["offset"] - base
            for stt in p.get("state", []):
                out["%s.%s" % (nm, stt["name"])] = stt["value"]
    return out


def _resolve_walk(node, names, what, where):
    if isinstance(node, dict):
        for k in list(node):
            v = node[k]
            if _is_ref_key(k) and isinstance(v, str):
                if v not in names:
                    raise DescError("%s: %s = %r is no property or state of the TOML A" % (what, k, v))
                if v.count(".") == 1 and names[v] < 0:
                    raise DescError("%s: %s = %r lies below the core's window" % (what, k, v))
                node[k] = names[v]
            else:
                _resolve_walk(v, names, what, "%s.%s" % (where, k))
    elif isinstance(node, list):
        for i, v in enumerate(node):
            _resolve_walk(v, names, what, "%s[%d]" % (where, i))


def _has_ref(node):
    if isinstance(node, dict):
        return any((_is_ref_key(k) and isinstance(v, str)) or _has_ref(v) for k, v in node.items())
    if isinstance(node, list):
        return any(_has_ref(v) for v in node)
    return False


def resolve_toml_a_refs(doc, path=None):
    """doc is changed: the names of its register keys become numbers (see above). Without `toml_a` a name is refused; without a path `toml_a` is refused."""
    ref = doc.pop("toml_a", None)
    if ref is None:
        if _has_ref(doc):
            raise DescError("a register is named as block.property, and the description has no toml_a (the file of the TOML A)")
        return doc
    if not isinstance(ref, str) or not ref:
        raise DescError("toml_a is the path of the TOML A, from the description's directory")
    if path is None:
        raise DescError("toml_a: the description is read without a file name, so the TOML A cannot be found")
    fn = os.path.join(os.path.dirname(os.path.abspath(path)), ref)
    try:
        with open(fn) as fh:
            tomla = toml_loads(fh.read())
        names = toml_a_names(tomla)
    except OSError as e:
        raise DescError("toml_a %s: %s" % (ref, e.strerror))
    except DescError as e:
        raise DescError("toml_a %s: %s" % (ref, e))
    _resolve_walk(doc, names, os.path.basename(path), "")
    return doc



def _pad_code(tok):
    """A [pad] default entry -> the source id of pad.h: a direction 32..35, a position (role) 0x40..0x47, button1..32 -> 0..31, none -> 0xFF."""
    if tok in PAD_DIRS:
        return 32 + PAD_DIRS.index(tok)
    if tok in PAD_ROLES:
        return 0x40 + PAD_ROLES.index(tok)
    if tok == "none":
        return 0xFF
    m = re.fullmatch(r"button([1-9][0-9]?)", tok) if isinstance(tok, str) else None
    if m and int(m.group(1)) <= 32:
        return int(m.group(1)) - 1
    raise DescError("pad: default %r is not a source (%s, %s, button1 .. button32, none)" % (tok, ", ".join(PAD_DIRS), ", ".join(PAD_ROLES)))


def _pad_token(code):
    if 32 <= code <= 35:
        return PAD_DIRS[code - 32]
    if 0x40 <= code <= 0x47:
        return PAD_ROLES[code - 0x40]
    if code == 0xFF:
        return "none"
    return "button%d" % (code + 1)


def _restart_words(rs):
    """[restart] -> the two header words of version 5 (T228 R17): the bytes F8 "Restart core" clears from address 0, flags (bit 0: memory-only). None: no table."""
    if rs is None:
        return None
    _only(rs, {"clear_ram", "ram_only"}, "restart")
    n = _need(rs, "clear_ram", int, "restart")
    if isinstance(n, bool) or not 256 <= n <= RESTART_CLEAR_MAX or n % 256:
        raise DescError("restart: clear_ram is 256 .. %d bytes, a multiple of 256" % RESTART_CLEAR_MAX)
    return n, 1 if _opt(rs, "ram_only", bool, "restart", False) else 0


def _pad_words(pad, t, st):
    """[pad] -> the six header words of version 5 (kind, reg 0, reg 1, first name, names, default map); the names go into the value table. None: no pad."""
    if pad is None:
        return None
    _only(pad, {"output", "per_pad", "regs", "buttons", "default"}, "pad")
    output = _opt(pad, "output", str, "pad", "registers")
    if output not in PAD_OUTPUTS:
        raise DescError("pad: output %r (one of %s)" % (output, ", ".join(PAD_OUTPUTS)))
    names = _need(pad, "buttons", list, "pad")
    if not 1 <= len(names) <= 16 or not all(isinstance(x, str) and x for x in names):
        raise DescError("pad: buttons are 1 to 16 names")
    if output == "joystick":
        if "regs" in pad:
            raise DescError("pad: the joystick output has its ports (1 and 2), no regs")
        if len(names) > 5:
            raise DescError("pad: a joystick has 5 lines (up, down, left, right, fire)")
        regs = [0, 1]
    else:
        regs = _need(pad, "regs", list, "pad")
        one_port = _opt(pad, "per_pad", bool, "pad", False)           # one port (pad.h by_id): the second register is never picked and may be 0, as the Amiga's
        if len(regs) != 2 or not all(isinstance(x, int) and not isinstance(x, bool) and (0 < x or (one_port and i == 1)) and x <= 0xFFFF for i, x in enumerate(regs)):
            raise DescError("pad: regs are the two players' registers (the second may be 0 for a core with one port, per_pad)")
    dflt = _need(pad, "default", list, "pad")
    if len(dflt) != len(names):
        raise DescError("pad: default has one source per button")
    codes = [_pad_code(x) for x in dflt]
    fv = len(t["value"])
    t["value"].extend((st.add(x),) for x in names)
    kind = PAD_OUTPUTS[output] | (0x100 if _opt(pad, "per_pad", bool, "pad", False) else 0)
    return (kind, regs[0], regs[1], fv, len(names), st.add("".join("%02X" % c for c in codes)))


# ---- the core's data sections (T239.3 D17) ----------------------------------------------------------------------------------
# What a C++ descriptor holds besides the menu tables above, as TOML keys and as sections behind the string table: [core] (identity, the operations the
# machine is served by, its flags), [video] and [freeze], [[rom]], [hotkeys], [mount] and, inside a [[core_setting]], `reg_bind` (the register bits a setting
# is). A section is `u32 id, u32 length, length bytes` (a multiple of 4), at most once each; a reader refuses an id it does not know. Strings are offsets
# into the record's string table, NONE = null.

SEC_CORE, SEC_VIDEO, SEC_ROMS, SEC_SETBIND, SEC_HOTKEYS, SEC_MOUNT = 1, 2, 3, 4, 5, 6
(SEC_RESETW, SEC_IDFEAT, SEC_REPORT, SEC_RAWKEYS, SEC_ALTVIDEO, SEC_FBWIN, SEC_MOUSE, SEC_ROMSET, SEC_WINMEDIA, SEC_SECMEDIA, SEC_REPLACE) = range(7, 18)
SEC_IO, SEC_IDENT, SEC_BLOCKS = 20, 21, 22     # version 7 only: TOML A
SEC_ROUTES, SEC_PLACES = 23, 24        # T239.3 D32: where the rows the application adds stand in the menu (no [core] either; a version 6 record only)
SEC_KEYROWS, SEC_WINDOWS = 18, 19      # base 3.3: the key table of the UI TOML and the register windows of the API TOML; they need no [core] (a menu.toml carries the key table)
SEC_NAMES = {SEC_CORE: "core", SEC_VIDEO: "video", SEC_ROMS: "roms", SEC_SETBIND: "setting register bits", SEC_HOTKEYS: "hotkeys", SEC_MOUNT: "mount slots",
             SEC_RESETW: "reset writes", SEC_IDFEAT: "id feature", SEC_REPORT: "report registers", SEC_RAWKEYS: "raw keys", SEC_ALTVIDEO: "alternative video",
             SEC_FBWIN: "picture window", SEC_MOUSE: "mouse", SEC_ROMSET: "rom set", SEC_WINMEDIA: "window media", SEC_SECMEDIA: "sector media", SEC_REPLACE: "media replace",
             SEC_KEYROWS: "key rows", SEC_WINDOWS: "windows", SEC_IO: "io", SEC_IDENT: "identity", SEC_BLOCKS: "blocks", SEC_ROUTES: "routes", SEC_PLACES: "places"}
SEC_KEYS = ("core", "video", "freeze", "rom", "hotkeys", "mount", "reset_write", "id_feature", "report_reg", "rawkeys", "alt_video", "fbwin", "mouse", "romset",
            "window_media", "sector_media")           # the top-level TOML keys that make a record version 6 (and `replace` in a [[media]])
CORE_FLAGS = ("poll_when_frozen", "board_caps", "id_shared", "own_volume", "no_tone", "menu_first")
CORE_REQ = ("name", "title", "short_title", "folder", "store")
CORE_OPT = ("service_op", "needs_program", "program_ext", "type_op", "run_op", "core_id")
CORE_INTS = ("min_contract", "reset_ms", "service_period_ms", "live_ms")
VIDEO_INTS = ("w", "h", "scale", "x0", "y0", "fb_base", "fb_stride", "nbuf", "reg_latest", "reg_seq", "reg_fb_base", "reg_fb_stride", "bpp", "pxfmt", "h60")
FREEZE_INTS = ("reg_status", "mask", "timeout_ms")
ROMS_MAX, ROM_NAMES_MAX, ROM_SIZES_MAX = 14, 10, 3
HK_ACTIONS = 7                                                              # coreapp/src/hotkeys.h HK_NACT
SETBIND_MAX = 16
SETBIND_BYTES_MAX = 64
MOUNT_NAMES_MAX = 6
CAP_RE = r"[a-z0-9_]{1,15}"
HOOKS_RE = r"[a-z0-9_]{1,15}"                                               # the name of a library (u2a_pack.py field), coreapp/src/descrec.cc hooks_name_ok
CAPS_MAX = 8                                                                # coreapp/src/descrec.h DR_CAPS


def _u32(v, what):
    if isinstance(v, bool) or not isinstance(v, int) or not 0 <= v < 2 ** 32:
        raise DescError("%s: a number of 32 bits is expected" % what)
    return v


def _int_in(d, k, lo, hi, what, default=None):
    v = d.get(k, default)
    if v is None:
        raise DescError("%s: %r is missing" % (what, k))
    if isinstance(v, bool) or not isinstance(v, int) or not lo <= v <= hi:
        raise DescError("%s: %r is a number from %d to %d" % (what, k, lo, hi))
    return v


def _sec_core(c, st):
    _only(c, {"id", "min_contract", "reset_ms", "service_period_ms", "live_ms", "caps", "hooks"} | set(CORE_REQ) | set(CORE_OPT) | set(CORE_FLAGS), "core")
    cid = _need(c, "id", str, "core")
    if len(cid) != 4 or not cid.isascii() or not cid.isprintable():
        raise DescError("core: id is the four characters of the ID block")
    for k in CORE_REQ:
        if not _need(c, k, str, "core"):
            raise DescError("core: %r is empty" % k)
    for k in CORE_OPT:
        if k in c and (not isinstance(c[k], str) or not c[k]):
            raise DescError("core: %r is a text, not empty" % k)
    caps = _opt(c, "caps", list, "core", [])
    if not all(isinstance(x, str) and re.fullmatch(CAP_RE, x) for x in caps) or len(set(caps)) != len(caps) or len(caps) > CAPS_MAX:
        raise DescError("core: caps are at most %d names of 1 to 15 characters of a-z, 0-9 and _" % CAPS_MAX)
    period = _int_in(c, "service_period_ms", 0, 60000, "core", 0)
    if ("service_op" in c) != (period > 0):
        raise DescError("core: service_op and service_period_ms go together")
    flags = 0
    for i, k in enumerate(CORE_FLAGS):
        if _opt(c, k, bool, "core", False):
            flags |= 1 << i
    w = [struct.unpack("<I", cid.encode("ascii"))[0], st.add(c["name"]), st.add(c["title"]), st.add(c["short_title"]), st.add(c["folder"]), st.add(c["store"]),
         _int_in(c, "min_contract", 0, 255, "core", 1), _int_in(c, "reset_ms", 0, 65535, "core", 0), st.add(c.get("service_op")), period, flags,
         st.add(c.get("needs_program")), st.add(c.get("program_ext")), st.add(c.get("type_op")), st.add(c.get("run_op")), st.add(c.get("core_id")),
         _int_in(c, "live_ms", 0, 65535, "core", 0), st.add(",".join(caps)) if caps else NONE]
    if "hooks" in c:                                                         # a 19th word, only for a core whose code is a library (core_hooks.h); a record without it stays 18 words
        if not isinstance(c["hooks"], str) or not re.fullmatch(HOOKS_RE, c["hooks"]):
            raise DescError("core: hooks names a library of 1 to 15 characters of a-z, 0-9 and _")
        w.append(st.add(c["hooks"]))
    return struct.pack("<%dI" % len(w), *w)


def _scale_ok(v, may_be_none):
    """scale 1 .. 3; 0 is no picture of the core's own (CoreDesc::video all zeros: the SuperCPU's picture goes through the host's scaler) and only then, in [video] and not in [alt_video]."""
    return v["scale"] in (1, 2, 3) or (may_be_none and v["scale"] == 0 and not any(v[k] for k in VIDEO_INTS))


def _video_words(v, what="video", none_ok=False):
    _only(v, set(VIDEO_INTS), what)
    if none_ok and not any(v.get(k) for k in VIDEO_INTS):       # [video] with no key, or every key zero: a core with no picture of its own (all words zero)
        for k in VIDEO_INTS:
            _int_in(v, k, 0, 0, what, 0)
        return [0] * len(VIDEO_INTS)
    vals = {}
    for k in VIDEO_INTS:
        if k in ("x0", "y0"):
            vals[k] = _int_in(v, k, -2 ** 31, 2 ** 31 - 1, "video") & 0xFFFFFFFF
        elif k in ("fb_base", "fb_stride", "w", "h"):
            vals[k] = _int_in(v, k, 0, 2 ** 32 - 1, "video")
        elif k in ("bpp", "pxfmt", "h60"):
            vals[k] = _int_in(v, k, 0, 65535, "video", 0)
        else:
            vals[k] = _int_in(v, k, 0, 65535, "video")
    if not _scale_ok(vals, False) or vals["nbuf"] > 8 or vals["bpp"] not in (0, 2, 4) or vals["pxfmt"] > 4:
        raise DescError("%s: scale 1 .. 3 (0 only in [video], with every other number 0: a core whose picture goes through the host's scaler), nbuf at most 8, bpp 0, 2 or 4, pxfmt at most 4" % what)
    return [vals[k] for k in VIDEO_INTS]


def _sec_video(v, fz, st):
    w = _video_words(v, "video", True)
    _only(fz, set(FREEZE_INTS) | {"word"}, "freeze")
    w += [_int_in(fz, "reg_status", 0, 65535, "freeze"), _int_in(fz, "mask", 0, 255, "freeze"), _int_in(fz, "timeout_ms", 0, 65535, "freeze"),
          1 if _opt(fz, "word", bool, "freeze", False) else 0]
    return struct.pack("<%dI" % len(w), *w)


def _sec_roms(roms, st):
    if not 1 <= len(roms) <= ROMS_MAX:
        raise DescError("rom: 1 to %d entries" % ROMS_MAX)
    out = struct.pack("<I", len(roms))
    for r in roms:
        _only(r, {"role", "names", "sizes", "required", "set", "shared", "put_op", "check"}, "rom")
        role = _need(r, "role", str, "rom")
        what = "rom " + role
        names = _need(r, "names", list, what)
        sizes = _need(r, "sizes", list, what)
        if not 1 <= len(names) <= ROM_NAMES_MAX or not all(isinstance(x, str) and x for x in names):
            raise DescError("%s: 1 to %d file names" % (what, ROM_NAMES_MAX))
        if not 1 <= len(sizes) <= ROM_SIZES_MAX or not all(isinstance(x, int) and not isinstance(x, bool) and 0 < x < 2 ** 32 for x in sizes):
            raise DescError("%s: 1 to %d sizes in bytes" % (what, ROM_SIZES_MAX))
        flags = (1 if _opt(r, "required", bool, what, False) else 0) | (2 if _opt(r, "shared", bool, what, False) else 0) | (4 if _opt(r, "check", bool, what, False) else 0) | (_int_in(r, "set", 0, 255, what, 0) << 8)
        w = [st.add(role), st.add(_need(r, "put_op", str, what)), flags] + sizes + [0] * (ROM_SIZES_MAX - len(sizes)) + [st.add(x) for x in names] + [NONE] * (ROM_NAMES_MAX - len(names))
        out += struct.pack("<%dI" % len(w), *w)
    return out


def _sec_hotkeys(h, st):
    _only(h, {"mod", "actions", "tasks", "vol_key", "swap_key", "own_keys", "core_op", "core_key", "core_label", "pc_keys"}, "hotkeys")
    mod = _need(h, "mod", list, "hotkeys")
    act = _need(h, "actions", list, "hotkeys")
    tasks = _opt(h, "tasks", list, "hotkeys", [])
    own = _opt(h, "own_keys", list, "hotkeys", [])
    if len(mod) != 2 or len(act) != HK_ACTIONS or len(tasks) > HOTKEY_TASKS:
        raise DescError("hotkeys: mod has 2 keys, actions %d, tasks at most %d (255 = none)" % (HK_ACTIONS, HOTKEY_TASKS))
    for x in mod + act + tasks:
        if isinstance(x, bool) or not isinstance(x, int) or not (0 <= x <= 64 or x == 255):
            raise DescError("hotkeys: a key is 0 .. 64, or 255 for none")
    if not all(isinstance(x, int) and not isinstance(x, bool) and 0 <= x <= 63 for x in own):
        raise DescError("hotkeys: own_keys are key indices 0 .. 63")
    mask = 0
    for x in own:
        mask |= 1 << x
    if ("core_op" in h) != ("core_key" in h) or ("core_op" in h) != ("core_label" in h):
        raise DescError("hotkeys: core_op, core_key and core_label go together")
    w = mod + act + [len(tasks)] + tasks + [0] * (HOTKEY_TASKS - len(tasks)) + [st.add(h.get("vol_key")), st.add(h.get("swap_key")), mask & 0xFFFFFFFF, mask >> 32,
        st.add(h.get("core_op")), st.add(h.get("core_key")), st.add(h.get("core_label")), 1 if _opt(h, "pc_keys", bool, "hotkeys", False) else 0]
    return struct.pack("<%dI" % len(w), *w)


def _sec_mount(m, st):
    _only(m, {"names"}, "mount")
    names = _need(m, "names", list, "mount")
    if not 1 <= len(names) <= MOUNT_NAMES_MAX or not all(isinstance(x, str) and 1 <= len(x) <= 9 for x in names):
        raise DescError("mount: 1 to %d slot names of 1 to 9 characters" % MOUNT_NAMES_MAX)
    return struct.pack("<I%dI" % len(names), len(names), *[st.add(x) for x in names])


def _sec_setbind(binds):
    out = struct.pack("<I", len(binds))
    for idx, b in binds:
        what = "core_setting reg_bind"
        _only(b, {"reg", "shift", "mask", "bytes"}, what)
        by = _opt(b, "bytes", list, what, [])
        if not all(isinstance(x, int) and not isinstance(x, bool) and 0 <= x <= 255 for x in by) or len(by) > SETBIND_BYTES_MAX:
            raise DescError("%s: bytes are at most %d numbers 0 .. 255" % (what, SETBIND_BYTES_MAX))
        out += struct.pack("<5I", idx, _int_in(b, "reg", 0, 65535, what), _int_in(b, "shift", 0, 7, what, 0), _int_in(b, "mask", 1, 255, what), len(by))
        out += bytes(by) + bytes(-len(by) % 4)
    return out


# The sections of the data a C++ descriptor holds as small structs (T239.3 D17): a table of fields, each a word. kind "u<bits>" an unsigned number, "i<bits>" a signed
# one (stored as 32 bit two's complement), "t" a text (a string table offset, required), "o" an optional text (none = NONE). One table writes and reads a section,
# and coreapp/src/descrec.cc reads the same words in the same order.
MOUSE_KINDS = ("counters", "pot1351")                   # mousesink.h MouseKind
FBWIN_FIELDS = (("reg_feat", "u16"), ("feat_bit", "u8"), ("reg_ctrl", "u16"), ("ctrl_video_off", "u8"), ("reg_diw_h", "u16"), ("reg_diw_v", "u16"), ("reg_skip_x", "u16"),
                ("reg_skip_y", "u16"), ("reg_cols", "u16"), ("reg_lines", "u16"), ("x_per_unit", "i16"), ("x_origin", "i16"), ("y_origin", "i16"), ("src_w", "i16"),
                ("src_h", "i16"), ("def_skip_x", "u16"), ("def_skip_y", "u16"), ("def_cols", "u16"), ("def_lines", "u16"), ("interval_ms", "u16"), ("settle_ms", "u16"),
                ("frames_ms", "u16"), ("min_apply_ms", "u16"))
MOUSE_FIELDS = (("kind", "k"), ("reg_dx", "u16"), ("reg_dy", "u16"), ("reg_btn", "u16"), ("clamp", "i16"), ("step", "i16"), ("interval_ms", "u8"))
WINMEDIA_FIELDS = (("prefix", "t"), ("nslots", "u3"), ("base", "u32"), ("stride", "u32"), ("max_image", "u32"), ("mount_reg", "u16"), ("mount_step", "u16"),
                   ("track_bytes", "u32"), ("ntracks", "u8"), ("dlog_status", "u16"), ("dlog_track", "u16"), ("dlog_drive", "u16"), ("dlog_pop", "u16"),
                   ("idle_flush_ms", "u16"), ("max_dirty_ms", "u16"))
SECMEDIA_FIELDS = (("prefix", "t"), ("nunits", "u2"), ("mount_reg", "u16"), ("mount_step", "u16"), ("feat_reg", "u16"), ("feat_sectors", "u8"), ("mbox_status", "u16"),
                   ("mbox_lba", "u16"), ("mbox_done", "u16"), ("stage_off", "u32"), ("idle_sync_ms", "u16"), ("done_tag_mask", "u8"))
ROMSET_FIELDS = (("role", "t"), ("ext", "t"), ("size_text", "t"), ("example", "t"), ("max", "u4"), ("choice_key", "t"), ("auto_label", "t"), ("want", "u16"),
                 ("head_bytes", "u8"), ("window_off", "u32"), ("mirror_size", "u32"), ("mirror_stride", "u32"), ("clear_below", "u32"))
ROMSET_MAX_, ROMSET_SIZES_ = 8, 3
RESETW_MAX, REPORT_MAX = 8, 24
RAWKEY_MODS = 8
_RANGES = {"u": lambda b: (0, 2 ** b - 1), "i": lambda b: (-2 ** (b - 1), 2 ** (b - 1) - 1)}


def _field_words(d, fields, what, st):
    out = []
    for k, kind in fields:
        v = d.get(k)
        if kind in ("t", "o"):
            if v is None and kind == "t":
                raise DescError("%s: %r is missing" % (what, k))
            if v is not None and (not isinstance(v, str) or not v):
                raise DescError("%s: %r is a text, not empty" % (what, k))
            out.append(st.add(v))
        elif kind == "k":
            if v not in MOUSE_KINDS:
                raise DescError("%s: %r is one of %s" % (what, k, ", ".join(MOUSE_KINDS)))
            out.append(MOUSE_KINDS.index(v))
        else:
            lo, hi = _RANGES[kind[0]](int(kind[1:]))
            out.append(_int_in(d, k, lo, hi, what) & 0xFFFFFFFF)
    return out


def _field_dict(w, fields, what, s):
    d = {}
    for (k, kind), x in zip(fields, w):
        if kind in ("t", "o"):
            if x == NONE and kind == "o":
                continue
            v = s(x)
            if not v:
                raise DescError("%s: a text is missing" % what)
            d[k] = v
        elif kind == "k":
            if x >= len(MOUSE_KINDS):
                raise DescError("%s: the mouse kind is not known" % what)
            d[k] = MOUSE_KINDS[x]
        else:
            lo, hi = _RANGES[kind[0]](int(kind[1:]))
            v = x - 2 ** 32 if kind[0] == "i" and x >= 2 ** 31 else x
            if not lo <= v <= hi:
                raise DescError("%s: %r is out of range" % (what, k))
            d[k] = v
    return d


def _sec_fields(d, fields, what, st, extra=()):
    _only(d, {k for k, _ in fields} | set(extra), what)
    return struct.pack("<%dI" % len(fields), *_field_words(d, fields, what, st))


def _sec_resetw(lst):
    if not 1 <= len(lst) <= RESETW_MAX:
        raise DescError("reset_write: 1 to %d entries" % RESETW_MAX)
    out = struct.pack("<I", len(lst))
    for r in lst:
        _only(r, {"reg", "value"}, "reset_write")
        out += struct.pack("<2I", _int_in(r, "reg", 0, 65535, "reset_write"), _int_in(r, "value", 0, 255, "reset_write"))
    return out


def _sec_idfeat(d):
    _only(d, {"reg", "mask", "value"}, "id_feature")
    return struct.pack("<3I", _int_in(d, "reg", 0, 65535, "id_feature"), _int_in(d, "mask", 1, 255, "id_feature"), _int_in(d, "value", 0, 255, "id_feature"))


def _sec_report(lst, st):
    if not 1 <= len(lst) <= REPORT_MAX:
        raise DescError("report_reg: 1 to %d entries" % REPORT_MAX)
    out = struct.pack("<I", len(lst))
    for r in lst:
        _only(r, {"name", "reg", "bytes"}, "report_reg")
        by = _int_in(r, "bytes", 1, 4, "report_reg")
        if by == 3:
            raise DescError("report_reg: bytes is 1, 2 or 4")
        out += struct.pack("<3I", st.add(_need(r, "name", str, "report_reg")), _int_in(r, "reg", 0, 65535, "report_reg"), by)
    return out


def _byte_list(v, n_lo, n_hi, what, k):
    if not isinstance(v, list) or not n_lo <= len(v) <= n_hi or not all(isinstance(x, int) and not isinstance(x, bool) and 0 <= x <= 255 for x in v):
        raise DescError("%s: %r is %s numbers 0 .. 255" % (what, k, ("%d to %d" % (n_lo, n_hi)) if n_lo != n_hi else str(n_lo)))
    return bytes(v) + bytes(-len(v) % 4)


def _sec_rawkeys(r):
    _only(r, {"reg", "hid", "mod", "caps_usage", "caps_code", "held"}, "rawkeys")
    held = _opt(r, "held", bool, "rawkeys", False)
    usage = _int_in(r, "caps_usage", 0, 255, "rawkeys")
    if held:       # the core's code reads the keys that are down (DescCode::rawkeys_held); no key queue, no tables
        if any(k in r for k in ("reg", "hid", "mod", "caps_code")):
            raise DescError("rawkeys: held has no reg, hid, mod or caps_code")
        return struct.pack("<6I", 1, 0, usage, 0, 0, 0)
    hid = _byte_list(r.get("hid"), 1, 255, "rawkeys", "hid")
    mod = _byte_list(r.get("mod"), RAWKEY_MODS, RAWKEY_MODS, "rawkeys", "mod")
    return struct.pack("<6I", 0, _int_in(r, "reg", 0, 65535, "rawkeys"), usage, _int_in(r, "caps_code", 0, 255, "rawkeys"), len(r["hid"]), RAWKEY_MODS) + hid + mod


def _sec_romset(r, st):
    _only(r, {k for k, _ in ROMSET_FIELDS} | {"sizes", "version", "pick"}, "romset")
    sizes = _need(r, "sizes", list, "romset")
    if not 1 <= len(sizes) <= ROMSET_SIZES_ or not all(isinstance(x, int) and not isinstance(x, bool) and 0 < x < 2 ** 32 for x in sizes):
        raise DescError("romset: 1 to %d sizes in bytes" % ROMSET_SIZES_)
    if not 1 <= r.get("max", 0) <= ROMSET_MAX_:
        raise DescError("romset: max is 1 to %d" % ROMSET_MAX_)
    flags = (1 if _opt(r, "version", bool, "romset", False) else 0) | (2 if _opt(r, "pick", bool, "romset", False) else 0)
    return struct.pack("<I%dI" % ROMSET_SIZES_, flags, *(sizes + [0] * (ROMSET_SIZES_ - len(sizes)))) + struct.pack("<%dI" % len(ROMSET_FIELDS), *_field_words(r, ROMSET_FIELDS, "romset", st))


def _sec_replace(items, st):
    out = struct.pack("<I", len(items))
    for idx, txt in items:
        out += struct.pack("<2I", idx, st.add(txt))
    return out


# ---- the key table and the register windows (T239.3 base 3.3) --------------------------------------------------------------
# Sections 18 and 19. They belong to two different files of a core (the key table to its UI TOML menu.toml, the windows to its API TOML desc.toml), so they need no [core]:
# a record that has only these is a menu record of version 6 whose sections are these. Both are checked here and in descrec.cc by the same rules.
#
# key rows   u32 count (1 .. KEYS_MAX), then per row 9 words: layout (string, none), node (string, none) - exactly one of them -, lo, hi (both 0xFFFFFFFF: every key; else 0 <= lo <= hi
#            <= 0x1FF), do (0 op, 1 decline, 2 swallow), op (string, none; present exactly for do = 0), value (the operation's argument, 32 bit two's complement; 0 unless flags bit 0),
#            flags (menuintent.h KF_ARG 1, KF_KEYARG 2, KF_STATUS 4, KF_STATUS_OK 8, KF_CLOSE 32, KF_DIRECTIVE 64, KF_DIRECT 128; none for a decline or a swallow), when (string, none)
# windows    u32 count (1 .. WINDOWS_MAX), then per window 3 words: name (string), address, size (bytes)
#
# The rows are tried in the order of the record, where the generic rows of the monitor and Timewarp stood (menukeytab.cc, stage KS_CORE); layout names the layout of the shown
# tab (menuctl.cc layout_is), node the id of the shown tab.
KEYS_MAX = 64
WINDOWS_MAX = 32
WINDOW_SIZE_MAX = 0x100000             # a block of the I/O space is at most 1 MiB
KEY_FLAGS = {"pass_key": 2, "status": 4, "status_ok": 8, "close": 32, "directive": 64, "direct": 128}      # menuintent.h KF_*; `value` is KF_ARG (1)
KF_ARG = 1
KEY_KEYS = {"layout", "node", "key", "codes", "do", "op", "value", "when"} | set(KEY_FLAGS)
KEY_DO = ("op", "decline", "swallow")
KEY_ANY = 0xFFFFFFFF
KEY_CODE_MAX = 0x1FF
# The names of the keys: host-app/software/io/c64/keyboard.h (tools/test_descrec.py reads that file and compares). `ctrl+<letter>` only for the letters the keyboard delivers.
KEY_NAMES = {"up": 0x91, "down": 0x12, "left": 0x9D, "right": 0x1D, "return": 0x0D, "esc": 0x1B, "del": 0x08, "runstop": 0x11, "space": 0x20, "home": 0x13, "pgup": 0x92, "pgdn": 0x95,
             "tab": 0x09, "f1": 0x85, "f2": 0x89, "f3": 0x86, "f4": 0x8A, "f5": 0x87, "f6": 0x8B, "f7": 0x88, "f8": 0x8C, "f10": 0x8E, "scrlock": 0xA3, "menu": 0x1FE}
KEY_CTRL = {"a": 0x01, "b": 0x02, "c": 0x03, "d": 0x04, "e": 0x05, "i": 0x09, "j": 0x0A, "k": 0x0B, "l": 0x0C, "n": 0x0E, "o": 0x0F, "p": 0x10, "r": 0xBA, "v": 0x16, "z": 0x1A}

def _key_code(name, what):
    if not isinstance(name, str) or not name:
        raise DescError("%s: key is a name or one printable character" % what)
    if name in KEY_NAMES:
        return KEY_NAMES[name]
    if name.startswith("ctrl+"):
        if name[5:] not in KEY_CTRL:
            raise DescError("%s: key %r - ctrl+ takes one of the letters %s" % (what, name, ", ".join(sorted(KEY_CTRL))))
        return KEY_CTRL[name[5:]]
    if len(name) == 1 and 0x21 <= ord(name) <= 0x7E:
        return ord(name)
    raise DescError("%s: key %r is not a key name (%s, ctrl+<letter>), \"any\" or one printable character" % (what, name, ", ".join(KEY_NAMES)))


def _key_name(code):
    """The TOML key that makes this code (the reader's answer, so that decoding and compiling again gives the same bytes)."""
    for n, c in KEY_NAMES.items():
        if c == code:
            return {"key": n}
    for n, c in KEY_CTRL.items():
        if c == code:
            return {"key": "ctrl+" + n}
    if 0x21 <= code <= 0x7E:
        return {"key": chr(code)}
    return {"codes": [code, code]}


def _row_checks(layout, node, lo, hi, do, op, value, flags, when, what):
    """The rules both readers hold a key row to (descrec.cc parse_sections, id 18)."""
    if (layout is None) == (node is None):
        raise DescError("%s: exactly one of layout and node" % what)
    if layout is not None and not re.fullmatch(r"[a-z0-9_]{1,23}", layout):
        raise DescError("%s: layout %r is a layout name (a-z, 0-9, _, at most 23)" % (what, layout))
    if node is not None and not re.fullmatch(r"[a-z0-9_]{1,31}", node):
        raise DescError("%s: node %r is a tab id (a-z, 0-9, _, at most 31)" % (what, node))
    if not ((lo == KEY_ANY and hi == KEY_ANY) or (lo <= hi <= KEY_CODE_MAX)):
        raise DescError("%s: the keys are every key or a range of codes 0 .. %d" % (what, KEY_CODE_MAX))
    if do > 2 or flags & ~(KF_ARG | 2 | 4 | 8 | 32 | 64 | 128) or (flags & KF_ARG and flags & 2):
        raise DescError("%s: an unknown action or flag, or both value and pass_key" % what)
    if (do == 0) != (op is not None) or (op is not None and not op) or (not flags & KF_ARG and value) or (do and flags):
        raise DescError("%s: an op row has an operation and the flags for it; a decline or a swallow has neither" % what)
    if when is not None:
        name = when[1:] if when.startswith("!") else when
        if not name or name.startswith("!"):
            raise DescError("%s: when is a state name, \"!\" in front of it for none" % what)


def _sec_keys(rows, st):
    if not isinstance(rows, list) or not 1 <= len(rows) <= KEYS_MAX or not all(isinstance(r, dict) for r in rows):
        raise DescError("key: 1 to %d rows, each a table" % KEYS_MAX)
    out = [struct.pack("<I", len(rows))]
    for i, r in enumerate(rows):
        what = "key row %d" % (i + 1)
        _only(r, KEY_KEYS, what)
        layout = _opt(r, "layout", str, what, None)
        node = _opt(r, "node", str, what, None)
        if ("key" in r) == ("codes" in r):
            raise DescError("%s: exactly one of key and codes" % what)
        if "key" in r:
            k = _need(r, "key", str, what)
            lo = hi = KEY_ANY if k == "any" else _key_code(k, what)
        else:
            c = _need(r, "codes", list, what)
            if len(c) != 2 or not all(isinstance(x, int) and not isinstance(x, bool) and 0 <= x <= KEY_CODE_MAX for x in c) or c[0] > c[1]:
                raise DescError("%s: codes = [lo, hi], 0 .. %d, lo not above hi" % (what, KEY_CODE_MAX))
            lo, hi = c
        op = _opt(r, "op", str, what, None)
        do = _opt(r, "do", str, what, "op" if op is not None else None)
        if do is None:
            raise DescError("%s: do is op, decline or swallow (op alone means do = \"op\")" % what)
        if do not in KEY_DO:
            raise DescError("%s: do %r (one of %s)" % (what, do, ", ".join(KEY_DO)))
        flags = 0
        value = 0
        if "value" in r:
            value = _need(r, "value", int, what)
            if not -2 ** 31 <= value < 2 ** 31:
                raise DescError("%s: value does not fit 32 bits" % what)
            flags |= KF_ARG
        for k, bit in KEY_FLAGS.items():
            if _opt(r, k, bool, what, False):
                flags |= bit
        when = _opt(r, "when", str, what, None)
        _row_checks(layout, node, lo, hi, KEY_DO.index(do), op, value & 0xFFFFFFFF, flags, when, what)
        out.append(struct.pack("<9I", st.add(layout), st.add(node), lo, hi, KEY_DO.index(do), st.add(op), value & 0xFFFFFFFF, flags, st.add(when)))
    return b"".join(out)


def _window_check(name, address, size, what):
    if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]{1,31}", name):
        raise DescError("%s: name is a-z, 0-9 and _, 1 to 31 characters" % what)
    if not 1 <= size <= WINDOW_SIZE_MAX or address + size > 2 ** 32:
        raise DescError("%s (%s): size is 1 .. 0x%X bytes and the block ends below 4 GiB" % (what, name, WINDOW_SIZE_MAX))


def _sec_windows(rows, st):
    if not isinstance(rows, list) or not 1 <= len(rows) <= WINDOWS_MAX or not all(isinstance(r, dict) for r in rows):
        raise DescError("window: 1 to %d entries, each a table" % WINDOWS_MAX)
    out, names = [struct.pack("<I", len(rows))], set()
    for i, r in enumerate(rows):
        what = "window %d" % (i + 1)
        _only(r, {"name", "address", "size"}, what)
        name = _need(r, "name", str, what)
        address = _need(r, "address", int, what)
        size = _need(r, "size", int, what)
        if not 0 <= address < 2 ** 32:
            raise DescError("%s (%s): address is a 32 bit number" % (what, name))
        _window_check(name, address, size, what)
        if name in names:
            raise DescError("%s: the name %r twice" % (what, name))
        names.add(name)
        out.append(struct.pack("<3I", st.add(name), address, size))
    return b"".join(out)


# ---- TOML A: the gateware as capabilities (T239.3 P1-1, D30, D34) ---------------------------------------------------------------
# Record version 7: no menu table, only the sections 20 to 22, each once and all three. Both readers (this file and coreapp/src/descrec.cc) hold a record to the same rules.
# TOML A holds no logic (D30 condition 3): addresses, registers, fields and the states they can take, and the name of the driver library of each block.
#
#   [io]                            the base addresses of the I/O space, each a 32 bit number (IO_KEYS)
#   [identity]                      the standard block (D30 condition 2): id = the four characters of the ID, block = the name of the block that holds the identity
#                                   registers (owner base; it declares the readable properties "id" and "build"); build = the bitstream's build word, optional in the source: cor_pack
#                                   writes the AXSS word of the bitstream into the record (stamp_build, P1-4), and the service compares id and build with the block at start
#   [[block]]                       a capability: map (optional: the name of a module map, see below), name (a-z 0-9 _, 1 .. 31, once), module (the gateware module and so the driver library, the same characters), owner
#                                   ("base" or "core"), address, size (bytes, 1 .. 0x100000; the blocks do not overlap, so a block of the core covers no byte of a block of the base). A module whose registers serve several drivers is several blocks, one per register range (D34).
#     [[block.property]]            the map: name (once per block), offset (bytes from the block, the register lies inside it), width (8, 16 or 32 bits), dir ("r", "w"
#                                   or "rw"; two properties share bytes only when no direction is in both), count (optional, 1 .. PROP_COUNT_MAX, default 1: a run of `count` registers
#                                   of this width at consecutive offsets, element i at offset + i * width / 8; the run lies inside the block, shares bytes as one register
#                                   does, and its fields and states hold for every element)
#       [[block.property.field]]    name (once per property), shift, bits (the field lies inside the width; fields do not overlap)
#         [[block.property.field.state]]   name (once per owner), value (fits the field's bits, once per owner)
#       [[block.property.state]]    the states of the whole register: name, value (fits the width)
# A module map lives with its driver library (a map file `<name>.map.toml`: `map = "<name>"` and `[[property]]` tables as above; the shared drivers' maps are in the SDK, a core's own in its
# repository). A block says `map = "<name>"` instead of its own [[block.property]]; `compile ... --maps DIR` (repeatable) reads the files and the compiler puts the map's properties
# into the block, so the record carries the finished map and the readers need no map file. A block with both is refused. An unused map is allowed
# (a directory holds the maps of all modules); a name that no file has is refused.
# Section 20 io        2 words: iobase, u2p_io_base
# Section 21 identity  4 words: id (string), build flag (0 or 1), build, block (string)
# Section 22 blocks    4 words: nblocks nprops nfields nstates; then the blocks (7 words: name module owner address size first_prop nprop), the properties (9 words: name offset
#                      width dir first_field nfield first_state nstate count; dir 1 r, 2 w, 3 rw; count 1 for a single register), the fields (5 words: name shift bits first_state nstate) and the states (2 words:
#                      name value); first_* count in their own table, an owner's entries are a run
TOMLA_KEYS = ("io", "identity", "block")
IO_KEYS = ("iobase", "u2p_io_base")
BLOCKS_MAX, PROPS_MAX, FIELDS_MAX, STATES_MAX = 64, 512, 512, 1024
PROPS_PER_BLOCK, FIELDS_PER_PROP, STATES_PER_OWNER = 64, 32, 64
BLOCK_NAME_RE = r"[a-z0-9_]{1,31}"
PROP_WIDTHS = (8, 16, 32)
PROP_COUNT_MAX = 0x10000
PROP_DIRS = {"r": 1, "w": 2, "rw": 3}
IDENT_PROPS = ("id", "build")


def _u32(d, k, what):
    v = _need(d, k, int, what)
    if not 0 <= v < 2 ** 32:
        raise DescError("%s: %s is a 32 bit number" % (what, k))
    return v


MAP_SUFFIX = ".map.toml"


def load_maps(paths):
    """The module maps of files or directories (every `*.map.toml` of a directory): {name: [property tables]}. A name twice is refused with both files."""
    files = []
    for pth in paths:
        if os.path.isdir(pth):
            files.extend(sorted(os.path.join(pth, f) for f in os.listdir(pth) if f.endswith(MAP_SUFFIX)))
        else:
            files.append(pth)
    maps, where = {}, {}
    for f in files:
        try:
            with open(f) as fh:
                d = toml_loads(fh.read())
            _only(d, {"map", "property"}, "module map")
            nm = _need(d, "map", str, "module map")
            props = _need(d, "property", list, "module map")
        except (DescError, OSError) as e:
            raise DescError("%s: %s" % (f, e.strerror if isinstance(e, OSError) else e))
        if not re.fullmatch(BLOCK_NAME_RE, nm):
            raise DescError("%s: the map's name is a-z, 0-9 and _, 1 to 31 characters" % f)
        if nm in maps:
            raise DescError("%s and %s both define the map %r" % (where[nm], f, nm))
        maps[nm] = props
        where[nm] = f
    return maps


def resolve_maps(doc, maps):
    """A [[block]] with `map = "name"` gets that map's properties (copied) in place of its own; doc is changed. Without a map table a block that names one is refused."""
    import copy
    for b in doc.get("block", []) if isinstance(doc.get("block"), list) else []:
        if isinstance(b, dict) and "map" in b:
            nm = b.pop("map")
            if "property" in b:
                raise DescError("block %s: a map and its own properties" % b.get("name"))
            if not isinstance(nm, str) or not maps or nm not in maps:
                raise DescError("block %s: the map %r is in none of the map files" % (b.get("name"), nm))
            b["property"] = copy.deepcopy(maps[nm])
    return doc


def _sec_io(io):
    _only(io, set(IO_KEYS), "io")
    return struct.pack("<%dI" % len(IO_KEYS), *(_u32(io, k, "io") for k in IO_KEYS))


def _states_check(owner, bits, what):
    """The states of one owner (a property or a field): at most STATES_PER_OWNER, each a name and a value that fits `bits`, both once."""
    sts = _opt(owner, "state", list, what, [])
    if len(sts) > STATES_PER_OWNER:
        raise DescError("%s: %d states, at most %d" % (what, len(sts), STATES_PER_OWNER))
    names, vals = set(), set()
    for j, x in enumerate(sts):
        w = "%s, state %d" % (what, j + 1)
        if not isinstance(x, dict):
            raise DescError("%s: a state is a table" % w)
        _only(x, {"name", "value"}, w)
        nm = _need(x, "name", str, w)
        if not re.fullmatch(BLOCK_NAME_RE, nm):
            raise DescError("%s: name is a-z, 0-9 and _, 1 to 31 characters" % w)
        v = _need(x, "value", int, w)
        if not 0 <= v < 1 << bits:
            raise DescError("%s (%s): value does not fit %d bits" % (w, nm, bits))
        if nm in names or v in vals:
            raise DescError("%s (%s): the name or the value is in a state of the same owner already" % (w, nm))
        names.add(nm)
        vals.add(v)
    return sts


def _sec_blocks(blocks, st):
    """Section 22 and the names of the blocks in order. The checks here and in descrec.cc are the same."""
    if not 1 <= len(blocks) <= BLOCKS_MAX or not all(isinstance(b, dict) for b in blocks):
        raise DescError("block: 1 to %d entries, each a table" % BLOCKS_MAX)
    bw, pw, fw, sw, names, spans = [], [], [], [], [], []
    for i, b in enumerate(blocks):
        what = "block %d" % (i + 1)
        _only(b, {"name", "module", "owner", "address", "size", "property"}, what)
        name = _need(b, "name", str, what)
        if not re.fullmatch(BLOCK_NAME_RE, name):
            raise DescError("%s: name is a-z, 0-9 and _, 1 to 31 characters" % what)
        what = "block %s" % name
        if name in names:
            raise DescError("%s: the name twice" % what)
        module = _need(b, "module", str, what)
        if not re.fullmatch(BLOCK_NAME_RE, module):
            raise DescError("%s: module is a-z, 0-9 and _, 1 to 31 characters" % what)
        owner = _need(b, "owner", str, what)
        if owner not in ("base", "core"):
            raise DescError("%s: owner is \"base\" or \"core\"" % what)
        address, size = _u32(b, "address", what), _need(b, "size", int, what)
        if not 1 <= size <= WINDOW_SIZE_MAX or address + size > 2 ** 32:
            raise DescError("%s: size is 1 .. 0x%X bytes and the block ends below 4 GiB" % (what, WINDOW_SIZE_MAX))
        for n2, a2, s2 in spans:
            if address < a2 + s2 and a2 < address + size:
                raise DescError("%s: overlaps the block %s" % (what, n2))
        names.append(name)
        spans.append((name, address, size))
        props = _opt(b, "property", list, what, [])
        if len(props) > PROPS_PER_BLOCK:
            raise DescError("%s: %d properties, at most %d" % (what, len(props), PROPS_PER_BLOCK))
        bw.append((st.add(name), st.add(module), 0 if owner == "base" else 1, address, size, len(pw), len(props)))
        seen = []
        for k, p in enumerate(props):
            pwhat = "%s, property %d" % (what, k + 1)
            if not isinstance(p, dict):
                raise DescError("%s: a property is a table" % pwhat)
            _only(p, {"name", "offset", "width", "dir", "count", "field", "state"}, pwhat)
            pname = _need(p, "name", str, pwhat)
            if not re.fullmatch(BLOCK_NAME_RE, pname):
                raise DescError("%s: name is a-z, 0-9 and _, 1 to 31 characters" % pwhat)
            pwhat = "%s, property %s" % (what, pname)
            if any(pname == x[0] for x in seen):
                raise DescError("%s: the name twice" % pwhat)
            off, width = _u32(p, "offset", pwhat), _need(p, "width", int, pwhat)
            if width not in PROP_WIDTHS:
                raise DescError("%s: width is 8, 16 or 32 bits" % pwhat)
            dr = _need(p, "dir", str, pwhat)
            if dr not in PROP_DIRS:
                raise DescError("%s: dir is \"r\", \"w\" or \"rw\"" % pwhat)
            count = _opt(p, "count", int, pwhat, 1)
            if isinstance(count, bool) or not 1 <= count <= PROP_COUNT_MAX:
                raise DescError("%s: count is 1 .. %d" % (pwhat, PROP_COUNT_MAX))
            span = count * (width // 8)
            if off + span > size:
                raise DescError("%s: the register%s lies outside the block (%d bytes)" % (pwhat, " run" if count > 1 else "", size))
            for x in seen:
                if off < x[1] + x[2] and x[1] < off + span and PROP_DIRS[dr] & x[3]:
                    raise DescError("%s: shares a byte and a direction with the property %s" % (pwhat, x[0]))
            seen.append((pname, off, span, PROP_DIRS[dr]))
            flds = _opt(p, "field", list, pwhat, [])
            if len(flds) > FIELDS_PER_PROP:
                raise DescError("%s: %d fields, at most %d" % (pwhat, len(flds), FIELDS_PER_PROP))
            pst = _states_check(p, width, pwhat)
            pw.append((st.add(pname), off, width, PROP_DIRS[dr], len(fw), len(flds), len(sw), len(pst), count))
            sw.extend((st.add(x["name"]), x["value"]) for x in pst)
            used, fnames = 0, set()
            for m_, f in enumerate(flds):
                fwhat = "%s, field %d" % (pwhat, m_ + 1)
                if not isinstance(f, dict):
                    raise DescError("%s: a field is a table" % fwhat)
                _only(f, {"name", "shift", "bits", "state"}, fwhat)
                fname = _need(f, "name", str, fwhat)
                if not re.fullmatch(BLOCK_NAME_RE, fname):
                    raise DescError("%s: name is a-z, 0-9 and _, 1 to 31 characters" % fwhat)
                fwhat = "%s, field %s" % (pwhat, fname)
                if fname in fnames:
                    raise DescError("%s: the name twice" % fwhat)
                fnames.add(fname)
                shift, bits = _need(f, "shift", int, fwhat), _need(f, "bits", int, fwhat)
                if shift < 0 or bits < 1 or shift + bits > width:
                    raise DescError("%s: shift and bits lie inside the %d bit register" % (fwhat, width))
                mask = ((1 << bits) - 1) << shift
                if used & mask:
                    raise DescError("%s: shares a bit with another field" % fwhat)
                used |= mask
                fst = _states_check(f, bits, fwhat)
                fw.append((st.add(fname), shift, bits, len(sw), len(fst)))
                sw.extend((st.add(x["name"]), x["value"]) for x in fst)
    if len(pw) > PROPS_MAX or len(fw) > FIELDS_MAX or len(sw) > STATES_MAX:
        raise DescError("block: %d properties, %d fields and %d states (at most %d, %d and %d)" % (len(pw), len(fw), len(sw), PROPS_MAX, FIELDS_MAX, STATES_MAX))
    out = [struct.pack("<4I", len(bw), len(pw), len(fw), len(sw))]
    for t in (bw, pw, fw, sw):
        out.extend(struct.pack("<%dI" % len(e), *e) for e in t)
    return b"".join(out), names


def _sec_ident(ident, names, blocks, st):
    _only(ident, {"id", "build", "block"}, "identity")
    cid = _need(ident, "id", str, "identity")
    if len(cid) != 4 or not cid.isascii() or not cid.isprintable():
        raise DescError("identity: id is the four characters of the ID")
    bname = _need(ident, "block", str, "identity")
    if bname not in names:
        raise DescError("identity: block %r is no block of this record" % bname)
    blk = blocks[names.index(bname)]
    if blk["owner"] != "base":
        raise DescError("identity: the block %s is owned by the core; the standard block belongs to the base" % bname)
    for pn in IDENT_PROPS:
        if not any(p["name"] == pn and "r" in p["dir"] for p in blk.get("property", [])):
            raise DescError("identity: the block %s has no readable property %r" % (bname, pn))
    build = _u32(ident, "build", "identity") if "build" in ident else 0
    return struct.pack("<4I", st.add(cid), 1 if "build" in ident else 0, build, st.add(bname))


# ---- where the application's rows stand (T239.3 D32) ------------------------------------------------------------------------
# Sections 23 and 24, one table each, read by descrec.cc by the same rules.
#
# routes   u32 count (1 .. ROUTES_MAX), then per route 6 words: group, area (F1 .. F8), tab, heading (strings), label (string, none), flags (b0 page)
# places   u32 count (1 .. PLACES_MAX), then per place 5 words: op (string), key (string, none), route (string, none), group (string, none), order (32 bit two's complement; PLACE_NO_ORDER: none)
ROUTES_MAX = 16
PLACES_MAX = 64
PLACE_NO_ORDER = 0x80000000
ROUTE_KEYS = {"group", "area", "tab", "heading", "label", "page"}
PLACE_KEYS = {"op", "key", "route", "group", "order"}


def _route_check(group, area, tab, heading, label, page, what):
    if not re.fullmatch(r"[a-z0-9_]{1,31}", group or ""):
        raise DescError("%s: group is a-z, 0-9 and _, 1 to 31 characters" % what)
    if not re.fullmatch(r"F[1-8]", area or ""):
        raise DescError("%s (%s): area is F1 .. F8" % (what, group))
    if not re.fullmatch(r"[a-z0-9_]{1,31}", tab or ""):
        raise DescError("%s (%s): tab is the id of a tab (a-z, 0-9, _, at most 31)" % (what, group))
    if not heading or len(heading) > 31:
        raise DescError("%s (%s): heading is 1 to 31 characters" % (what, group))
    if label is not None and (not label or len(label) > 31):
        raise DescError("%s (%s): label is 1 to 31 characters" % (what, group))
    if page and label is None:
        raise DescError("%s (%s): a page needs the label of the row that opens it" % (what, group))


def _sec_routes(rows, st):
    if not isinstance(rows, list) or not 1 <= len(rows) <= ROUTES_MAX or not all(isinstance(r, dict) for r in rows):
        raise DescError("group_route: 1 to %d entries, each a table" % ROUTES_MAX)
    out, names = [struct.pack("<I", len(rows))], set()
    for i, r in enumerate(rows):
        what = "group_route %d" % (i + 1)
        _only(r, ROUTE_KEYS, what)
        group, area, tab, heading = (_need(r, k, str, what) for k in ("group", "area", "tab", "heading"))
        label = _opt(r, "label", str, what, None)
        page = _opt(r, "page", bool, what, False)
        _route_check(group, area, tab, heading, label, page, what)
        if group in names:
            raise DescError("%s: the group %r twice" % (what, group))
        names.add(group)
        out.append(struct.pack("<6I", st.add(group), st.add(area), st.add(tab), st.add(heading), st.add(label), 1 if page else 0))
    return b"".join(out)


def _place_check(op, key, route, group, order, what):
    if not op or len(op) > 63 or re.search(r"\s", op):
        raise DescError("%s: op is the operation of the row (1 to 63 characters, no blanks)" % what)
    if key is not None and (not key or len(key) > 63):
        raise DescError("%s (%s): key is 1 to 63 characters" % (what, op))
    if route is not None and not re.fullmatch(r"[a-z0-9_]{1,31}", route):
        raise DescError("%s (%s): route is the group of a route (a-z, 0-9, _, at most 31)" % (what, op))
    if group is not None and not re.fullmatch(r"[A-Za-z0-9_]{1,31}", group):
        raise DescError("%s (%s): group is 1 to 31 characters of A-Z, a-z, 0-9 and _" % (what, op))
    if order is not None and not -2 ** 31 < order < 2 ** 31:
        raise DescError("%s (%s): order does not fit 31 bits" % (what, op))
    if route is None and group is None and order is None:
        raise DescError("%s (%s): it places nothing (give a route, a group or an order)" % (what, op))


def _sec_places(rows, st):
    if not isinstance(rows, list) or not 1 <= len(rows) <= PLACES_MAX or not all(isinstance(r, dict) for r in rows):
        raise DescError("place: 1 to %d entries, each a table" % PLACES_MAX)
    out, seen = [struct.pack("<I", len(rows))], set()
    for i, r in enumerate(rows):
        what = "place %d" % (i + 1)
        _only(r, PLACE_KEYS, what)
        op = _need(r, "op", str, what)
        key = _opt(r, "key", str, what, None)
        route = _opt(r, "route", str, what, None)
        group = _opt(r, "group", str, what, None)
        order = _opt(r, "order", int, what, None)
        _place_check(op, key, route, group, order, what)
        if (op, (key or "").lower()) in seen:
            raise DescError("%s: the operation %r with the key %r twice" % (what, op, key))
        seen.add((op, (key or "").lower()))
        out.append(struct.pack("<5I", st.add(op), st.add(key), st.add(route), st.add(group), PLACE_NO_ORDER if order is None else order & 0xFFFFFFFF))
    return b"".join(out)


def _sections(doc, setbinds, st, replaces=()):
    """The bytes behind the string table: the sections in the order of their ids. Empty when the document has none of their keys."""
    secs = []
    if "core" in doc:
        secs.append((SEC_CORE, _sec_core(_need(doc, "core", dict, "description"), st)))
    elif any(k in doc for k in SEC_KEYS) or setbinds or replaces:
        raise DescError("a record with a core's data sections needs [core] (its identity)")
    if "video" in doc or "freeze" in doc:
        secs.append((SEC_VIDEO, _sec_video(_need(doc, "video", dict, "description"), _need(doc, "freeze", dict, "description"), st)))
    if "rom" in doc:
        secs.append((SEC_ROMS, _sec_roms(_need(doc, "rom", list, "description"), st)))
    if setbinds:
        if len(setbinds) > SETBIND_MAX:
            raise DescError("%d settings are register bits; the application takes %d" % (len(setbinds), SETBIND_MAX))
        secs.append((SEC_SETBIND, _sec_setbind(setbinds)))
    if "hotkeys" in doc:
        secs.append((SEC_HOTKEYS, _sec_hotkeys(_need(doc, "hotkeys", dict, "description"), st)))
    if "mount" in doc:
        secs.append((SEC_MOUNT, _sec_mount(_need(doc, "mount", dict, "description"), st)))
    if "reset_write" in doc:
        secs.append((SEC_RESETW, _sec_resetw(_need(doc, "reset_write", list, "description"))))
    if "id_feature" in doc:
        secs.append((SEC_IDFEAT, _sec_idfeat(_need(doc, "id_feature", dict, "description"))))
    if "report_reg" in doc:
        secs.append((SEC_REPORT, _sec_report(_need(doc, "report_reg", list, "description"), st)))
    if "rawkeys" in doc:
        secs.append((SEC_RAWKEYS, _sec_rawkeys(_need(doc, "rawkeys", dict, "description"))))
    if "alt_video" in doc:
        w = _video_words(_need(doc, "alt_video", dict, "description"), "alt_video")
        secs.append((SEC_ALTVIDEO, struct.pack("<%dI" % len(w), *w)))
    if "fbwin" in doc:
        secs.append((SEC_FBWIN, _sec_fields(_need(doc, "fbwin", dict, "description"), FBWIN_FIELDS, "fbwin", st)))
    if "mouse" in doc:
        secs.append((SEC_MOUSE, _sec_fields(_need(doc, "mouse", dict, "description"), MOUSE_FIELDS, "mouse", st)))
    if "romset" in doc:
        secs.append((SEC_ROMSET, _sec_romset(_need(doc, "romset", dict, "description"), st)))
    if "window_media" in doc:
        w = _need(doc, "window_media", dict, "description")
        _int_in(w, "nslots", 1, 4, "window_media")
        _int_in(w, "ntracks", 1, 160, "window_media")
        secs.append((SEC_WINMEDIA, _sec_fields(w, WINMEDIA_FIELDS, "window_media", st)))
    if "sector_media" in doc:
        w = _need(doc, "sector_media", dict, "description")
        _int_in(w, "nunits", 1, 2, "sector_media")
        secs.append((SEC_SECMEDIA, _sec_fields(w, SECMEDIA_FIELDS, "sector_media", st)))
    if replaces:
        secs.append((SEC_REPLACE, _sec_replace(replaces, st)))
    if "key" in doc:
        secs.append((SEC_KEYROWS, _sec_keys(doc["key"], st)))
    if "window" in doc:
        secs.append((SEC_WINDOWS, _sec_windows(doc["window"], st)))
    if "block" in doc:
        io, ident, blocks = _need(doc, "io", dict, "TOML A"), _need(doc, "identity", dict, "TOML A"), _need(doc, "block", list, "TOML A")
        bpack, names = _sec_blocks(blocks, st)
        secs.append((SEC_IO, _sec_io(io)))
        secs.append((SEC_IDENT, _sec_ident(ident, names, blocks, st)))
        secs.append((SEC_BLOCKS, bpack))
    if "group_route" in doc:
        secs.append((SEC_ROUTES, _sec_routes(doc["group_route"], st)))
    if "place" in doc:
        secs.append((SEC_PLACES, _sec_places(doc["place"], st)))
    return b"".join(struct.pack("<II", i, len(p)) + p for i, p in secs)


def _parse_sections(blob, off, size, s, ncset, out, ver=6):
    """The sections of a version 6 record, decoded into the TOML shape (out gets the keys of SEC_KEYS and `reg_bind` on the core settings)."""
    pos, end, seen = off, off + size, set()
    while pos < end:
        if end - pos < 8:
            raise DescError("description record: a section header is cut off")
        sid, ln = struct.unpack_from("<II", blob, pos)
        if sid not in SEC_NAMES:
            raise DescError("description record, section %d: this reader does not know it" % sid)
        what = "description record, section %s" % SEC_NAMES[sid]
        if (SEC_IO <= sid <= SEC_BLOCKS) != (ver == 7):
            raise DescError("%s: a version %d record has %s" % (what, ver, "only the sections 20 to 22 of TOML A" if ver == 7 else "no TOML A section"))
        if sid in seen or ln % 4 or pos + 8 + ln > end:
            raise DescError("%s: twice, not a multiple of 4, or longer than the record" % what)
        seen.add(sid)
        p = blob[pos + 8:pos + 8 + ln]
        pos += 8 + ln

        def words(n, at=0):
            if len(p) < at + 4 * n:
                raise DescError("%s: too short" % what)
            return struct.unpack_from("<%dI" % n, p, at)

        def S(o, req=False):
            v = s(o)
            if req and not v:
                raise DescError("%s: a text is missing" % what)
            return v
        if sid == SEC_CORE:
            if len(p) not in (72, 76):
                raise DescError("%s: %d bytes, 72 or 76 expected" % (what, len(p)))
            w = words(len(p) // 4)
            c = {"id": struct.pack("<I", w[0]).decode("ascii", "replace")}
            for k, o in zip(CORE_REQ, w[1:6]):
                c[k] = S(o, True)
            c["min_contract"], c["reset_ms"] = w[6], w[7]
            if w[6] > 255 or w[7] > 65535 or w[9] > 60000 or w[16] > 65535:
                raise DescError("%s: a number is out of range" % what)
            if w[8] != NONE:
                c["service_op"] = S(w[8], True)
            c["service_period_ms"] = w[9]
            if w[10] >> len(CORE_FLAGS) or (w[8] == NONE) != (w[9] == 0):
                raise DescError("%s: unknown flags, or the service operation and its period disagree" % what)
            for i, k in enumerate(CORE_FLAGS):
                c[k] = bool(w[10] >> i & 1)
            for k, o in zip(("needs_program", "program_ext", "type_op", "run_op", "core_id"), w[11:16]):
                if o != NONE:
                    c[k] = S(o, True)
            c["live_ms"] = w[16]
            if w[17] != NONE:
                caps = S(w[17], True).split(",")
                if not all(re.fullmatch(CAP_RE, x) for x in caps) or len(caps) > CAPS_MAX:
                    raise DescError("%s: a capability name is not one, or there are more than %d" % (what, CAPS_MAX))
                c["caps"] = caps
            if len(w) == 19:
                c["hooks"] = S(w[18], True)
                if not re.fullmatch(HOOKS_RE, c["hooks"]):
                    raise DescError("%s: the hooks library name is not one" % what)
            out["core"] = c
        elif sid == SEC_VIDEO:
            if len(p) != 4 * (len(VIDEO_INTS) + 4):
                raise DescError("%s: %d bytes" % (what, len(p)))
            w = words(len(VIDEO_INTS) + 4)
            v = dict(zip(VIDEO_INTS, w))
            for k in ("x0", "y0"):
                v[k] = v[k] - 2 ** 32 if v[k] >= 2 ** 31 else v[k]
            if not _scale_ok(v, True) or v["nbuf"] > 8 or v["bpp"] not in (0, 2, 4) or v["pxfmt"] > 4 or v["h60"] > 65535 or max(v[k] for k in ("reg_latest", "reg_seq", "reg_fb_base", "reg_fb_stride")) > 65535 or w[-4] > 65535 or w[-3] > 255 or w[-2] > 65535 or w[-1] > 1:
                raise DescError("%s: a value is out of range" % what)
            out["video"] = v
            out["freeze"] = {"reg_status": w[-4], "mask": w[-3], "timeout_ms": w[-2], "word": bool(w[-1])}
        elif sid == SEC_ROMS:
            n, = words(1)
            if not 1 <= n <= ROMS_MAX or len(p) != 4 + n * 4 * (3 + ROM_SIZES_MAX + ROM_NAMES_MAX):
                raise DescError("%s: %d entries, or the length does not fit them" % (what, n))
            roms = []
            for i in range(n):
                w = words(3 + ROM_SIZES_MAX + ROM_NAMES_MAX, 4 + i * 4 * (3 + ROM_SIZES_MAX + ROM_NAMES_MAX))
                sizes = [x for x in w[3:3 + ROM_SIZES_MAX] if x]
                nw = w[3 + ROM_SIZES_MAX:]
                if NONE in nw[:sum(x != NONE for x in nw)]:
                    raise DescError("%s: entry %d has a name behind a gap" % (what, i))
                names = [S(x, True) for x in nw if x != NONE]
                if w[2] & ~0xFF07 or not sizes or not names or w[3 + len(sizes):3 + ROM_SIZES_MAX] != (0,) * (ROM_SIZES_MAX - len(sizes)):
                    raise DescError("%s: entry %d has bad flags, sizes or names" % (what, i))
                roms.append({"role": S(w[0], True), "put_op": S(w[1], True), "required": bool(w[2] & 1), "shared": bool(w[2] & 2), "check": bool(w[2] & 4), "set": w[2] >> 8, "sizes": sizes, "names": names})
            out["rom"] = roms
        elif sid == SEC_SETBIND:
            n, = words(1)
            at = 4
            if not 1 <= n <= SETBIND_MAX:
                raise DescError("%s: %d entries" % (what, n))
            for _ in range(n):
                idx, reg, shift, mask, nb = words(5, at)
                at += 20
                if idx >= ncset or reg > 65535 or shift > 7 or not 1 <= mask <= 255 or nb > SETBIND_BYTES_MAX or len(p) < at + nb + (-nb % 4) or (nb and nb != len(out["cset"][idx]["values"])) \
                        or out["cset"][idx]["when"] in ("text", "secret") or out["cset"][idx]["file"]:
                    raise DescError("%s: an entry names no core setting, or has a bad register, shift, mask or bytes" % what)
                by = list(p[at:at + nb])
                at += nb + (-nb % 4)
                d = {"reg": reg, "shift": shift, "mask": mask}
                if by:
                    d["bytes"] = by
                if "reg_bind" in out["cset"][idx]:
                    raise DescError("%s: setting %d twice" % (what, idx))
                out["cset"][idx]["reg_bind"] = d
            if at != len(p):
                raise DescError("%s: the length does not fit its entries" % what)
        elif sid == SEC_HOTKEYS:
            if len(p) != 4 * (2 + HK_ACTIONS + 1 + HOTKEY_TASKS + 8):
                raise DescError("%s: %d bytes" % (what, len(p)))
            w = words(2 + HK_ACTIONS + 1 + HOTKEY_TASKS + 8)
            nt = w[2 + HK_ACTIONS]
            tk = w[3 + HK_ACTIONS:3 + HK_ACTIONS + HOTKEY_TASKS]
            rest = w[3 + HK_ACTIONS + HOTKEY_TASKS:]
            if nt > HOTKEY_TASKS or any(x > 64 and x != 255 for x in w[:2 + HK_ACTIONS] + tk) or rest[7] > 1:
                raise DescError("%s: a key, the task count or the keyboard flag is out of range" % what)
            mask = rest[2] | rest[3] << 32
            h = {"mod": list(w[:2]), "actions": list(w[2:2 + HK_ACTIONS]), "own_keys": [i for i in range(64) if mask >> i & 1]}
            if nt:
                h["tasks"] = list(tk[:nt])
            for k, o in (("vol_key", rest[0]), ("swap_key", rest[1])):
                if o != NONE:
                    h[k] = S(o, True)
            if (rest[4] == NONE) != (rest[5] == NONE) or (rest[4] == NONE) != (rest[6] == NONE):
                raise DescError("%s: the core's own hotkey is cut off" % what)
            if rest[4] != NONE:
                h["core_op"], h["core_key"], h["core_label"] = S(rest[4], True), S(rest[5], True), S(rest[6], True)
            h["pc_keys"] = bool(rest[7])
            out["hotkeys"] = h
        elif sid == SEC_MOUNT:
            n, = words(1)
            if not 1 <= n <= MOUNT_NAMES_MAX or len(p) != 4 + 4 * n:
                raise DescError("%s: %d slots, or the length does not fit them" % (what, n))
            names = [S(x, True) for x in words(n, 4)]
            if not all(1 <= len(x) <= 9 for x in names):
                raise DescError("%s: a slot name is empty or longer than 9" % what)
            out["mount"] = {"names": names}
        elif sid == SEC_RESETW:
            n, = words(1)
            if not 1 <= n <= RESETW_MAX or len(p) != 4 + 8 * n:
                raise DescError("%s: %d entries, or the length does not fit them" % (what, n))
            out["reset_write"] = []
            for i in range(n):
                reg, val = words(2, 4 + 8 * i)
                if reg > 65535 or val > 255:
                    raise DescError("%s: entry %d is out of range" % (what, i))
                out["reset_write"].append({"reg": reg, "value": val})
        elif sid == SEC_IDFEAT:
            if len(p) != 12:
                raise DescError("%s: %d bytes, 12 expected" % (what, len(p)))
            reg, mask, val = words(3)
            if reg > 65535 or not 1 <= mask <= 255 or val > 255:
                raise DescError("%s: a value is out of range" % what)
            out["id_feature"] = {"reg": reg, "mask": mask, "value": val}
        elif sid == SEC_REPORT:
            n, = words(1)
            if not 1 <= n <= REPORT_MAX or len(p) != 4 + 12 * n:
                raise DescError("%s: %d entries, or the length does not fit them" % (what, n))
            out["report_reg"] = []
            for i in range(n):
                nm, reg, by = words(3, 4 + 12 * i)
                if not S(nm) or reg > 65535 or by not in (1, 2, 4):
                    raise DescError("%s: entry %d has no name, or a bad register or width" % (what, i))
                out["report_reg"].append({"name": S(nm), "reg": reg, "bytes": by})
        elif sid == SEC_RAWKEYS:
            flags, reg, usage, code, nh, nm = words(6)
            if flags > 1 or reg > 65535 or usage > 255 or code > 255 or nh > 255 or len(p) != 24 + nh + (-nh % 4) + nm + (-nm % 4):
                raise DescError("%s: a value is out of range, or the length does not fit the tables" % what)
            if flags:
                if reg or code or nh or nm:
                    raise DescError("%s: a held-keys core has no tables" % what)
                out["rawkeys"] = {"held": True, "caps_usage": usage}
            else:
                if not nh or nm != RAWKEY_MODS:
                    raise DescError("%s: the key table is empty, or the modifier table is not %d long" % (what, RAWKEY_MODS))
                out["rawkeys"] = {"reg": reg, "hid": list(p[24:24 + nh]), "mod": list(p[24 + nh + (-nh % 4):24 + nh + (-nh % 4) + nm]), "caps_usage": usage, "caps_code": code}
        elif sid == SEC_ALTVIDEO:
            if len(p) != 4 * len(VIDEO_INTS):
                raise DescError("%s: %d bytes" % (what, len(p)))
            v = dict(zip(VIDEO_INTS, words(len(VIDEO_INTS))))
            for k in ("x0", "y0"):
                v[k] = v[k] - 2 ** 32 if v[k] >= 2 ** 31 else v[k]
            if not _scale_ok(v, False) or v["nbuf"] > 8 or v["bpp"] not in (0, 2, 4) or v["pxfmt"] > 4 or max(v[k] for k in ("reg_latest", "reg_seq", "reg_fb_base", "reg_fb_stride", "h60")) > 65535:
                raise DescError("%s: a value is out of range" % what)
            out["alt_video"] = v
        elif sid in (SEC_FBWIN, SEC_MOUSE, SEC_WINMEDIA, SEC_SECMEDIA):
            key, fields = {SEC_FBWIN: ("fbwin", FBWIN_FIELDS), SEC_MOUSE: ("mouse", MOUSE_FIELDS), SEC_WINMEDIA: ("window_media", WINMEDIA_FIELDS),
                           SEC_SECMEDIA: ("sector_media", SECMEDIA_FIELDS)}[sid]
            if len(p) != 4 * len(fields):
                raise DescError("%s: %d bytes, %d expected" % (what, len(p), 4 * len(fields)))
            out[key] = _field_dict(words(len(fields)), fields, what, s)
            if (key == "window_media" and not 1 <= out[key]["ntracks"] <= 160) or (key == "window_media" and not 1 <= out[key]["nslots"] <= 4) or \
               (key == "sector_media" and not 1 <= out[key]["nunits"] <= 2):
                raise DescError("%s: a count is out of range" % what)
        elif sid == SEC_ROMSET:
            if len(p) != 4 * (1 + ROMSET_SIZES_ + len(ROMSET_FIELDS)):
                raise DescError("%s: %d bytes" % (what, len(p)))
            w = words(1 + ROMSET_SIZES_ + len(ROMSET_FIELDS))
            sizes = [x for x in w[1:1 + ROMSET_SIZES_] if x]
            d = _field_dict(w[1 + ROMSET_SIZES_:], ROMSET_FIELDS, what, s)
            if w[0] > 3 or not sizes or w[1 + len(sizes):1 + ROMSET_SIZES_] != (0,) * (ROMSET_SIZES_ - len(sizes)) or not 1 <= d["max"] <= ROMSET_MAX_:
                raise DescError("%s: flags, sizes or the count are out of range" % what)
            d["sizes"] = sizes
            d["version"], d["pick"] = bool(w[0] & 1), bool(w[0] & 2)
            out["romset"] = d
        elif sid == SEC_REPLACE:
            n, = words(1)
            if not 1 <= n <= len(out["media"]) or len(p) != 4 + 8 * n:
                raise DescError("%s: %d entries, or the length does not fit them" % (what, n))
            last = -1
            for i in range(n):
                idx, o = words(2, 4 + 8 * i)
                if idx <= last or idx >= len(out["media"]) or not S(o):
                    raise DescError("%s: entry %d names no media slot (in order), or has no text" % (what, i))
                last = idx
                out["media"][idx]["replace"] = S(o)
        elif sid == SEC_KEYROWS:
            n, = words(1)
            if not 1 <= n <= KEYS_MAX or len(p) != 4 + 36 * n:
                raise DescError("%s: %d rows, or the length does not fit them" % (what, n))
            rows = []
            for i in range(n):
                lay, node, lo, hi, do, op, val, fl, wh = words(9, 4 + 36 * i)
                rwhat = "%s, row %d" % (what, i + 1)
                if lay != NONE and not S(lay):
                    raise DescError("%s: an empty layout" % rwhat)
                if node != NONE and not S(node):
                    raise DescError("%s: an empty node" % rwhat)
                if op != NONE and not S(op):
                    raise DescError("%s: an empty operation" % rwhat)
                if wh != NONE and not S(wh):
                    raise DescError("%s: an empty condition" % rwhat)
                try:
                    _row_checks(S(lay), S(node), lo, hi, do, S(op), val, fl, S(wh), rwhat)
                except DescError as e:
                    raise DescError("%s (%s)" % (rwhat, e))
                row = {}
                if lay != NONE:
                    row["layout"] = S(lay)
                else:
                    row["node"] = S(node)
                if lo == KEY_ANY:
                    row["key"] = "any"
                elif lo == hi:
                    row.update(_key_name(lo))
                else:
                    row["codes"] = [lo, hi]
                row["do"] = KEY_DO[do]
                if op != NONE:
                    row["op"] = S(op)
                if fl & KF_ARG:
                    row["value"] = val - 2 ** 32 if val >= 2 ** 31 else val
                for k, bit in KEY_FLAGS.items():
                    if fl & bit:
                        row[k] = True
                if wh != NONE:
                    row["when"] = S(wh)
                rows.append(row)
            out["key"] = rows
        elif sid == SEC_IO:
            if len(p) != 4 * len(IO_KEYS):
                raise DescError("%s: %d bytes, %d expected" % (what, len(p), 4 * len(IO_KEYS)))
            out["io"] = dict(zip(IO_KEYS, words(len(IO_KEYS))))
        elif sid == SEC_IDENT:
            if len(p) != 16:
                raise DescError("%s: %d bytes, 16 expected" % (what, len(p)))
            iid, bf, bw_, bk = words(4)
            if bf > 1 or (bf == 0 and bw_ != 0):
                raise DescError("%s: the build flag is 0 or 1, and the build 0 without it" % what)
            d = {"id": S(iid, True), "block": S(bk, True)}
            if bf:
                d["build"] = bw_
            out["identity"] = d
        elif sid == SEC_BLOCKS:
            nb, np_, nf, ns = words(4)
            if not 1 <= nb <= BLOCKS_MAX or np_ > PROPS_MAX or nf > FIELDS_MAX or ns > STATES_MAX or len(p) != 16 + 4 * (7 * nb + 9 * np_ + 5 * nf + 2 * ns):
                raise DescError("%s: the counts or the length do not fit" % what)
            bo = 16
            po = bo + 28 * nb
            fo = po + 36 * np_
            so_ = fo + 20 * nf
            state_at = [0]

            def states(first, n_):
                if first != state_at[0] or first + n_ > ns:
                    raise DescError("%s: the states of an owner are not the next run" % what)
                state_at[0] += n_
                return [{"name": S(a, True), "value": v} for a, v in (words(2, so_ + 8 * (first + j)) for j in range(n_))]
            fld_at, prop_at, blocks = [0], [0], []
            for i in range(nb):
                nm, mod, ow, addr, size, fp, npr = words(7, bo + 28 * i)
                if fp != prop_at[0] or fp + npr > np_ or ow > 1:
                    raise DescError("%s: block %d: its properties are not the next run, or the owner is not known" % (what, i + 1))
                prop_at[0] += npr
                props = []
                for k in range(npr):
                    pn, off, width, dr, ff, nfl, sf, nst, cnt = words(9, po + 36 * (fp + k))
                    if ff != fld_at[0] or ff + nfl > nf or dr not in (1, 2, 3):
                        raise DescError("%s: block %d, property %d: its fields are not the next run, or the direction is not known" % (what, i + 1, k + 1))
                    fld_at[0] += nfl
                    pd = {"name": S(pn, True), "offset": off, "width": width, "dir": {1: "r", 2: "w", 3: "rw"}[dr]}
                    if cnt != 1:
                        pd["count"] = cnt
                    pst = states(sf, nst)
                    if pst:
                        pd["state"] = pst
                    fl = []
                    for m_ in range(nfl):
                        fn, sh, bits, fs, fns = words(5, fo + 20 * (ff + m_))
                        fd = {"name": S(fn, True), "shift": sh, "bits": bits}
                        fst = states(fs, fns)
                        if fst:
                            fd["state"] = fst
                        fl.append(fd)
                    if fl:
                        pd["field"] = fl
                    props.append(pd)
                bd = {"name": S(nm, True), "module": S(mod, True), "owner": "core" if ow else "base", "address": addr, "size": size}
                if props:
                    bd["property"] = props
                blocks.append(bd)
            if prop_at[0] != np_ or fld_at[0] != nf or state_at[0] != ns:
                raise DescError("%s: entries that no block owns" % what)
            _sec_blocks(blocks, _Strings())                       # the rules of the compiler, on what was read
            out["block"] = blocks
        elif sid == SEC_WINDOWS:
            n, = words(1)
            if not 1 <= n <= WINDOWS_MAX or len(p) != 4 + 12 * n:
                raise DescError("%s: %d windows, or the length does not fit them" % (what, n))
            wins, names = [], set()
            for i in range(n):
                nm, addr, size = words(3, 4 + 12 * i)
                _window_check(S(nm), addr, size, "%s, window %d" % (what, i + 1))
                if S(nm) in names:
                    raise DescError("%s: the name %r twice" % (what, S(nm)))
                names.add(S(nm))
                wins.append({"name": S(nm), "address": addr, "size": size})
            out["window"] = wins
        elif sid == SEC_ROUTES:
            n, = words(1)
            if not 1 <= n <= ROUTES_MAX or len(p) != 4 + 24 * n:
                raise DescError("%s: %d routes, or the length does not fit them" % (what, n))
            routes, names = [], set()
            for i in range(n):
                g, a, t, h, lb, fl = words(6, 4 + 24 * i)
                if fl & ~1:
                    raise DescError("%s, route %d: an unknown flag" % (what, i + 1))
                _route_check(S(g), S(a), S(t), S(h), S(lb) if lb != NONE else None, bool(fl & 1), "%s, route %d" % (what, i + 1))
                if S(g) in names:
                    raise DescError("%s: the group %r twice" % (what, S(g)))
                names.add(S(g))
                r = {"group": S(g), "area": S(a), "tab": S(t), "heading": S(h)}
                if lb != NONE:
                    r["label"] = S(lb)
                if fl & 1:
                    r["page"] = True
                routes.append(r)
            out["group_route"] = routes
        elif sid == SEC_PLACES:
            n, = words(1)
            if not 1 <= n <= PLACES_MAX or len(p) != 4 + 20 * n:
                raise DescError("%s: %d places, or the length does not fit them" % (what, n))
            places, seen_p = [], set()
            for i in range(n):
                op, k, rt, g, o = words(5, 4 + 20 * i)
                order = None if o == PLACE_NO_ORDER else (o - 2 ** 32 if o >= 2 ** 31 else o)
                pw = "%s, place %d" % (what, i + 1)
                _place_check(S(op), S(k) if k != NONE else None, S(rt) if rt != NONE else None, S(g) if g != NONE else None, order, pw)
                if (S(op), (S(k) or "").lower()) in seen_p:
                    raise DescError("%s: the operation %r with the key %r twice" % (what, S(op), S(k)))
                seen_p.add((S(op), (S(k) or "").lower()))
                r = {"op": S(op)}
                for name, v in (("key", k), ("route", rt), ("group", g)):
                    if v != NONE:
                        r[name] = S(v)
                if order is not None:
                    r["order"] = order
                places.append(r)
            out["place"] = places
    if ver == 7:
        if seen != {SEC_IO, SEC_IDENT, SEC_BLOCKS}:
            raise DescError("description record: TOML A has the sections io, identity and blocks, each once")
        _sec_ident(out["identity"], [b["name"] for b in out["block"]], out["block"], _Strings())
    if SEC_CORE not in seen and any(i <= SEC_REPLACE for i in seen):
        raise DescError("description record: the data sections have no core section")
    rs = out.get("romset")
    for c in out["cset"]:
        if "reg_bind" not in c and c["apply"] is None:
            # the third form: the rom set's choice (desc.h Setting::bind), whose values are the set's labels
            if not rs or c["key"] != rs["choice_key"] or c["values"] != [rs["auto_label"]] + ["-"] * rs["max"]:
                raise DescError("description record, section setting register bits: a core setting has neither an operation nor register bits, and is not the rom set's choice")


# ---- the reader (the same checks as descrec.cc) ----------------------------------------------------------------------------

def parse_desc(blob):
    """Reads and checks a record; returns a dict of the decoded lists. Raises DescError."""
    n = len(blob)
    if n < HEADER or n > MAX_SIZE:
        raise DescError("description record: %d bytes" % n)
    magic, ver, hsz, total, flags, so, ss = struct.unpack_from("<IHHIIII", blob, 0)
    if magic != MAGIC:
        raise DescError("description record: magic")
    if not VERSION_MIN <= ver <= VERSION:
        raise DescError("description record: version %d, this tool reads %d to %d" % (ver, VERSION_MIN, VERSION))
    esz = dict(ESIZE)
    if ver < 4:
        esz["media"] = ESIZE_V3_MEDIA
    hdr = HEADER_V6 if ver >= 6 else HEADER_V5 if ver >= 5 else HEADER
    if hsz != hdr or total != n or flags & ~(7 if ver >= 6 else 3) or n < hdr or bool(flags & 4) != (ver >= 6) or (ver == 7 and flags != 4):
        raise DescError("description record: header or length does not match")
    if so < hdr or so + ss > n or ss == 0:
        raise DescError("description record: string table outside the record")
    tabs = {}
    for i, k in enumerate(ORDER):
        off, cnt = struct.unpack_from("<II", blob, 24 + 8 * i)
        if cnt > LIMITS[k]:
            raise DescError("description record: %d %s entries" % (cnt, k))
        if cnt and (off < hdr or off + cnt * esz[k] > n):
            raise DescError("description record: table of %s outside the record" % k)
        tabs[k] = [struct.unpack_from("<%dI" % (esz[k] // 4), blob, off + j * esz[k]) for j in range(cnt)]

    def s(o):
        if o == NONE:
            return None
        if o >= ss:
            raise DescError("description record: a string lies outside the table")
        e = blob.find(b"\0", so + o, so + ss)
        if e < 0:
            raise DescError("description record: a string does not end in the table")
        try:
            return blob[so + o:e].decode("utf-8")
        except UnicodeDecodeError:
            raise DescError("description record: a string is not text")

    out = {"route": bool(flags & 1), "own": bool(flags & 2)}
    pop, pms, merged, r2 = struct.unpack_from("<IIII", blob, 24 + 8 * len(ORDER))
    if ver < 4 and r2:
        raise DescError("description record: a bad poll or merged-libraries list")
    out["machine_info"] = s(r2) if ver >= 4 and r2 != NONE else None
    if (pop == NONE) != (pms == 0) or pms > 60000 or (pop != NONE and not s(pop)) or (merged != NONE and not s(merged)):
        raise DescError("description record: a bad poll or merged-libraries list")
    out["merged"] = s(merged).split(",") if merged != NONE else []
    out["poll"] = {"op": s(pop), "period_ms": pms} if pop != NONE else None
    out["ctab"] = []
    for area, tid, name, w, groups, nn, lay in tabs["ctab"]:
        if not re.fullmatch(r"F[1-8]", s(area) or "") or not re.fullmatch(r"[a-z0-9_]{1,31}", s(tid) or "") or not s(name) or w >= len(WIDGETS) or nn > 99 or (lay and not s(lay)):
            raise DescError("description record: a bad contributed tab")
        out["ctab"].append({"area": s(area), "id": s(tid), "name": s(name), "widget": WIDGETS[w], "groups": s(groups), "n": nn, "layout": s(lay) if lay else None})
    out["bind"] = []
    for a, k, i, sv in tabs["bind"]:
        if k > 4 or not s(a) or (k == 2 and s(sv) is None):
            raise DescError("description record: a bad binding")
        out["bind"].append({"arg": s(a), "kind": k, "i": i - 2 ** 32 if i >= 2 ** 31 else i, "s": s(sv)})
    out["ext"] = [s(e[0]) for e in tabs["ext"]]
    out["size"] = [e[0] for e in tabs["size"]]
    out["value"] = [s(e[0]) for e in tabs["value"]]
    if any(not e or not e.startswith(".") for e in out["ext"]) or any(not v for v in out["value"]):
        raise DescError("description record: an extension without a dot or an empty value")
    out["pad"] = None
    if ver >= 5 and struct.unpack_from("<I", blob, HEADER)[0] != NONE:
        kind, r0, r1, fv, nv, dm = struct.unpack_from("<6I", blob, HEADER)
        dmap = s(dm)
        outs = {v: k for k, v in PAD_OUTPUTS.items()}
        if kind & ~0x1FF or (kind & 0xFF) not in outs or r0 > 0xFFFF or r1 > 0xFFFF or not 1 <= nv <= 16 or fv + nv > len(out["value"]) or dmap is None \
                or not re.fullmatch(r"([0-9A-F]{2})*", dmap) or len(dmap) != 2 * nv:
            raise DescError("description record: a bad pad")
        codes = [int(dmap[2 * i:2 * i + 2], 16) for i in range(nv)]
        if any(c > 35 and c != 0xFF and not 0x40 <= c <= 0x47 for c in codes):
            raise DescError("description record: a bad pad")
        out["pad"] = {"output": outs[kind & 0xFF], "per_pad": bool(kind & 0x100), "regs": [r0, r1], "buttons": out["value"][fv:fv + nv],
                      "default": [_pad_token(c) for c in codes]}
    out["restart"] = None
    if ver >= 5:
        clear, rfl = struct.unpack_from("<II", blob, HEADER + 24)
        if rfl & ~1 or clear > RESTART_CLEAR_MAX or clear % 256 or (rfl and not clear):
            raise DescError("description record: a bad restart")
        if clear:
            out["restart"] = {"clear_ram": clear, "ram_only": bool(rfl & 1)}
    out["hide"] = [s(e[0]) for e in tabs["hide"]]
    out["rename"] = [(s(a), s(b)) for a, b in tabs["rename"]]
    if any(not h for h in out["hide"]) or any(not a or not b for a, b in out["rename"]):
        raise DescError("description record: an empty hide or rename entry")
    out["tab"] = []
    for tid, name, nn, w, groups, lay in tabs["tab"]:
        layout = s(lay) if lay else None
        if not re.fullmatch(r"[a-z0-9_]{1,31}", s(tid) or "") or s(name) is None or nn > 99 or w >= len(WIDGETS) or (lay and not layout):
            raise DescError("description record: a bad tab")
        g = s(groups)
        if g == "":
            g = None
        isinfo = WIDGETS[w] == "info"
        if g and (not isinfo and WIDGETS[w] not in GROUP_WIDGETS):
            raise DescError("description record: tab %s lists groups but its widget takes none" % s(tid))
        if g and isinfo and any(x not in INFO_LINES for x in g.split(",")):
            raise DescError("description record: tab %s lists an information line that is not one" % s(tid))
        out["tab"].append({"id": s(tid), "name": s(name), "n": nn, "widget": WIDGETS[w], "groups": None if isinfo else g, "lines": g.split(",") if isinfo and g else None,
                           "layout": layout})
    out["area"] = []
    used = 0
    keys = set()
    for key, name, fl, first, cnt in tabs["area"]:
        k = s(key)
        if not k or not re.fullmatch(r"F[1-8]|APP", k) or fl & ~1 or k in keys:
            raise DescError("description record: a bad area")
        keys.add(k)
        if first != used or cnt == 0 or cnt > TABS_PER_AREA or first + cnt > len(out["tab"]):
            raise DescError("description record: the tabs of area %s are out of place" % k)
        used += cnt
        ids = [t["id"] for t in out["tab"][first:first + cnt]]
        ns = [t["n"] for t in out["tab"][first:first + cnt] if t["n"]]
        if len(set(ids)) != len(ids) or len(set(ns)) != len(ns):
            raise DescError("description record: area %s lists a tab or a number twice" % k)
        out["area"].append({"key": k, "name": s(name), "shifted": bool(fl & 1), "tabs": ids})
    if used != len(out["tab"]) or out["route"] != bool(out["area"]):
        raise DescError("description record: tabs without an area, or the route flag disagrees")
    out["node"] = []
    for anchor, kind, label, op, state, order, when, cap, nid, fb, nb, fl, attr in tabs["node"]:
        if not s(anchor) or not s(label) or kind > 4 or fl & ~1 or (attr and not s(attr)) or fb + nb > len(out["bind"]):
            raise DescError("description record: a bad node")
        out["node"].append({"anchor": s(anchor), "kind": kind, "label": s(label), "op": s(op), "state": s(state),
                            "order": order - 2 ** 32 if order >= 2 ** 31 else order, "when_state": s(when), "capability": s(cap),
                            "id": s(nid), "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1), "ui": s(attr) if attr else None})
    out["action"] = []
    for label, op, fe, ne, fz, nz, fb, nb, fl, rsv in tabs["action"]:
        if not s(label) or not s(op) or ne == 0 or fl & ~1 or rsv or fe + ne > len(out["ext"]) or fz + nz > len(out["size"]) \
                or fb + nb > len(out["bind"]):
            raise DescError("description record: a bad file action")
        out["action"].append({"label": s(label), "op": s(op), "ext": out["ext"][fe:fe + ne], "size_in": out["size"][fz:fz + nz],
                              "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1)})
    out["setting"] = []
    for group, key, fv, nv, lo, hi, df, apply in tabs["setting"]:
        lo, hi, df = [v - 2 ** 32 if v >= 2 ** 31 else v for v in (lo, hi, df)]
        if not s(key) or not s(apply) or fv + nv > len(out["value"]) or (nv == 0 and hi < lo):
            raise DescError("description record: a bad setting")
        out["setting"].append({"group": s(group), "key": s(key), "values": out["value"][fv:fv + nv], "lo": lo, "hi": hi, "default": df,
                               "apply": s(apply)})
    out["task"] = []
    for label, op, fb, nb, fl, ask, ask_state, pick_label, group in tabs["task"]:
        hk = (fl >> 8) & 0xFF
        if not s(label) or not s(op) or fl & 0xFFFF & ~0xFF07 or fb + nb > len(out["bind"]) or (s(ask) is None and (ask_state != NONE or fl & 2 or pick_label != NONE)) or \
                (pick_label != NONE and not fl & 2) or (hk and not (chr(hk).isalnum() and chr(hk).isascii() and chr(hk) == chr(hk).upper())) or (fl & 4 and (hk or fl & 2)):
            raise DescError("description record: a bad task")
        out["task"].append({"label": s(label), "op": s(op), "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1), "pick_dir": bool(fl & 2), "job": bool(fl & 4),
                            "ask": s(ask), "ask_state": s(ask_state), "pick_label": s(pick_label), "group": s(group), "hotkey": chr(hk) if hk else "", "show_state": s((fl >> 16) - 1) if fl >> 16 else None})
    out["media"] = []
    for me in tabs["media"]:
        name, state, eject, fb, nb, group = me[:6]
        ext_ = me[6:] if len(me) > 6 else (NONE, NONE, NONE, NONE, 0)
        if not s(name) or not s(state) or fb + nb > len(out["bind"]) or (eject == NONE and nb) or ext_[4] & ~1:
            raise DescError("description record: a bad media slot")
        out["media"].append({"name": s(name), "state": s(state), "eject_op": s(eject), "eject_bind": out["bind"][fb:fb + nb], "group": s(group),
                             "exts": s(ext_[0]), "mount": s(ext_[1]), "empty_tasks": s(ext_[2]), "mounted_tasks": s(ext_[3]), "eject_closes": bool(ext_[4] & 1)})
    out["fact"] = []
    for label, op, fb, nb, fl, ask, off, fz, nz in tabs["fact"]:
        off = off - 2 ** 32 if off >= 2 ** 31 else off
        if not s(label) or not s(op) or fl & 0xFFFF & ~0xFF07 or fb + nb > len(out["bind"]) or fz + nz > len(out["size"]) or not -32768 <= off <= 32767 or (s(ask) is None and (off or fl & 2)) or (fl & 2 and off):
            raise DescError("description record: a bad file type action")
        out["fact"].append({"label": s(label), "op": s(op), "bind": out["bind"][fb:fb + nb], "closes_menu": bool(fl & 1), "key": chr((fl >> 8) & 0xFF) if (fl >> 8) & 0xFF else "", "ask": None if fl & 2 else s(ask), "ask_off": off,
                            "size_in": out["size"][fz:fz + nz], "show_state": s((fl >> 16) - 1) if fl >> 16 else None, "repeat_op": s(ask) if fl & 2 else None, "show": bool(fl & 4)})
    out["ftype"] = []
    seen = set()
    for ext, inner_ext, fa, na, fi, ni in tabs["ftype"]:
        e = s(ext)
        if not e or not e.startswith(".") or e in seen or na == 0 or fa + na > len(out["fact"]) or fi + ni > len(out["fact"]) or \
                (ni == 0) != (inner_ext == NONE) or (inner_ext != NONE and not (s(inner_ext) or "").startswith(".")):
            raise DescError("description record: a bad file type")
        seen.add(e)
        out["ftype"].append({"ext": e, "inner_ext": s(inner_ext), "actions": out["fact"][fa:fa + na], "inner": out["fact"][fi:fi + ni]})
    out["cset"] = []
    for key, group, fv, nv, df, when, apply, base, label in tabs["cset"]:
        base = base - 2 ** 32 if base >= 2 ** 31 else base
        show = s((when >> 8) - 1) if when >> 8 else None
        when &= 0xFF
        file = bool(when & FILE_FLAG)                                 # T239.3 D17: a file the person selects (Setting::kind 3), live or reset like any other
        when &= ~FILE_FLAG
        lock = None
        if when & 4:                                                  # the last value string is the state that locks the row (T228 R21)
            if nv < 1 or fv + nv > len(out["value"]):
                raise DescError("description record: a bad core setting")
            lock = out["value"][fv + nv - 1]
            nv -= 1
            when &= ~4
        list_pick = None
        if when & 16:                                                 # T239.3 D32: the value string before the lock state names the list that fills the row's values (its pick)
            if nv < 2 or fv + nv > len(out["value"]):
                raise DescError("description record: a bad core setting")
            list_pick = out["value"][fv + nv - 1]
            nv -= 1
            when &= ~16
        if not s(key) or (not s(apply) and (apply != NONE or ver < 6)) or nv < (1 if when >= 2 or file or list_pick else 2) or fv + nv > len(out["value"]) or df >= nv or when > 3 or (file and (when > 1 or nv != 1 or base != 0 or not s(apply))):
            raise DescError("description record: a bad core setting")
        out["cset"].append({"key": s(key), "group": s(group), "values": out["value"][fv:fv + nv], "default": df, "when": {0: "live", 1: "reset", 2: "text", 3: "secret"}[when],
                            "apply": s(apply), "base": base, "show_state": show, "label": s(label), "lock_state": lock, "pick": list_pick, "file": file})
    if out["own"] != bool(out["task"] or out["media"] or out["ftype"] or out["cset"]):
        raise DescError("description record: the own-menu flag and the tables of the own menu disagree")
    out["sections"] = False
    if ver >= 6:
        soff, ssize = struct.unpack_from("<II", blob, HEADER_V5)
        if soff < so + ss or soff + ssize != n or ssize == 0:
            raise DescError("description record: the sections lie outside the record")
        if ver == 7 and any(tabs[k] for k in ORDER):
            raise DescError("description record: a TOML A record (version 7) has no menu table")
        _parse_sections(blob, soff, ssize, s, len(out["cset"]), out, ver)
        out["sections"] = "core" in out                              # the core's data sections; a record of the key table or the windows alone has none
    return out


# ---- the pack-time merge of the libraries' menus (T104.2 D10) -----------------------------------------------------------------------------

def _bind_doc(binds):
    out = []
    for b in binds:
        d = {"arg": b["arg"]}
        k = next(n for n, i in BIND_KINDS.items() if i == b["kind"])
        d[k] = b["i"] if k == "int" else (b["s"] if k == "str" else True)
        out.append(d)
    return out


def decoded_to_doc(rec):
    """The TOML document (a dict, as toml_loads makes it) that compiles to the record `rec` (parse_desc's answer): what the packer needs to put more
    into a record that is already compiled. compile_desc(decoded_to_doc(parse_desc(b)), merged) == b."""
    doc = {}
    if rec.get("machine_info"):
        doc["machine_info"] = rec["machine_info"]
    if rec["area"]:
        doc["area"], used = [], 0
        for a in rec["area"]:
            tabs = []
            for tb in rec["tab"][used:used + len(a["tabs"])]:
                d = {"id": tb["id"], "widget": tb["widget"], "n": tb["n"]}
                if tb["name"]:                                  # T205 D1: no name = named after the area
                    d["name"] = tb["name"]
                if tb["groups"]:
                    d["groups"] = tb["groups"].split(",")
                if tb.get("lines"):
                    d["lines"] = tb["lines"]
                if tb.get("layout"):
                    name, _, params = tb["layout"].partition(";")
                    if name:
                        d["layout"] = name
                    if params:
                        d["ui"] = _ui_table(params, UI_TAB_KEYS)
                tabs.append(d)
            used += len(a["tabs"])
            d = {"key": a["key"], "shifted": a["shifted"], "tab": tabs}
            if a["name"] is not None:
                d["name"] = a["name"]
            doc["area"].append(d)
    if rec["node"]:
        doc["node"] = []
        for n in rec["node"]:
            d = {"anchor": n["anchor"], "kind": next(k for k, i in NODE_KINDS.items() if i == n["kind"]), "label": n["label"], "order": n["order"],
                 "closes_menu": n["closes_menu"]}
            for k in ("op", "state", "when_state", "capability", "id"):
                if n[k] is not None:
                    d[k] = n[k]
            if n["bind"]:
                d["bind"] = _bind_doc(n["bind"])
            if n.get("ui"):
                d["ui"] = _ui_table(n["ui"], UI_NODE_KEYS)
            doc["node"].append(d)
    if rec["action"]:
        doc["file_action"] = []
        for a in rec["action"]:
            d = {"ext": a["ext"], "label": a["label"], "op": a["op"], "closes_menu": a["closes_menu"]}
            if a["bind"]:
                d["bind"] = _bind_doc(a["bind"])
            if a["size_in"]:
                d["size_in"] = a["size_in"]
            doc["file_action"].append(d)
    if rec["setting"]:
        doc["setting"] = []
        for x in rec["setting"]:
            d = {"key": x["key"], "apply": x["apply"], "default": x["default"]}
            if x["group"] is not None:
                d["group"] = x["group"]
            if x["values"]:
                d["values"] = x["values"]
            else:
                d["range"] = [x["lo"], x["hi"]]
            doc["setting"].append(d)
    if rec["hide"]:
        doc["hide"] = rec["hide"]
    if rec["rename"]:
        doc["rename"] = [{"id": a, "label": b} for a, b in rec["rename"]]
    if rec["ctab"]:
        doc["tab"] = []
        for c in rec["ctab"]:
            d = {"area": c["area"], "id": c["id"], "name": c["name"], "widget": c["widget"], "n": c["n"]}
            if c["groups"]:
                d["groups"] = c["groups"].split(",")
            if c.get("layout"):
                name, _, params = c["layout"].partition(";")
                if name:
                    d["layout"] = name
                if params:
                    d["ui"] = _ui_table(params, UI_TAB_KEYS)
            doc["tab"].append(d)
    if rec["poll"]:
        doc["poll"] = dict(rec["poll"])
    if rec.get("pad"):
        p = rec["pad"]
        d = {"output": p["output"], "buttons": list(p["buttons"]), "default": list(p["default"])}
        if p["per_pad"]:
            d["per_pad"] = True
        if p["output"] != "joystick":
            d["regs"] = list(p["regs"])
        doc["pad"] = d
    if rec.get("restart"):
        doc["restart"] = dict(rec["restart"])
    for k in SEC_KEYS + ("key", "window", "group_route", "place") + TOMLA_KEYS:
        if k in rec:
            doc[k] = rec[k]
    if rec["task"]:
        doc["task"] = []
        for t in rec["task"]:
            d = {"label": t["label"], "op": t["op"], "closes_menu": t["closes_menu"]}
            if t["bind"]:
                d["bind"] = _bind_doc(t["bind"])
            for k in ("group", "ask", "ask_state", "pick_label"):
                if t[k] is not None:
                    d[k] = t[k]
            if t["pick_dir"]:
                d["pick_dir"] = True
            if t.get("job"):
                d["job"] = True
            if t.get("hotkey"):
                d["hotkey"] = t["hotkey"]
            if t.get("show_state"):
                d["show_state"] = t["show_state"]
            doc["task"].append(d)
    if rec["media"]:
        doc["media"] = []
        for m in rec["media"]:
            d = {"name": m["name"], "state": m["state"]}
            if m["group"] is not None:
                d["group"] = m["group"]
            for k_ in ("exts", "mount", "empty_tasks", "mounted_tasks", "replace"):
                if m.get(k_) is not None:
                    d[k_] = m[k_]
            if m["eject_closes"]:
                d["eject_closes"] = True
            if m["eject_op"] is not None:
                d["eject_op"] = m["eject_op"]
                if m["eject_bind"]:
                    d["eject_bind"] = _bind_doc(m["eject_bind"])
            doc["media"].append(d)
    if rec["ftype"]:
        doc["filetype"] = []

        def facts(lst):
            out = []
            for a in lst:
                d = {"label": a["label"], "op": a["op"], "closes_menu": a["closes_menu"]}
                if a["bind"]:
                    d["bind"] = _bind_doc(a["bind"])
                if a["ask"] is not None:
                    d["ask"] = a["ask"]
                    d["ask_off"] = a["ask_off"]
                if a.get("repeat_op"):
                    d["repeat_op"] = a["repeat_op"]
                if a.get("show"):
                    d["show"] = True
                if a["size_in"]:
                    d["size_in"] = a["size_in"]
                if a.get("key"):
                    d["key"] = a["key"]                  # the sheet letter (bits 15:8 of the flags)
                if a.get("show_state"):
                    d["show_state"] = a["show_state"]    # bits 31:16 of the flags
                out.append(d)
            return out
        for f in rec["ftype"]:
            d = {"ext": f["ext"], "action": facts(f["actions"])}
            if f["inner_ext"] is not None:
                d["inner_ext"] = f["inner_ext"]
                d["inner"] = facts(f["inner"])
            doc["filetype"].append(d)
    if rec["cset"]:
        doc["core_setting"] = []
        for c in rec["cset"]:
            d = {"key": c["key"], "values": c["values"], "default": c["default"], "when": c["when"], "base": c["base"]}
            if c["apply"] is not None:
                d["apply"] = c["apply"]
            elif not c.get("reg_bind"):                  # the rom set's choice: its values are the set's labels, the compiler makes them again
                d["romset_choice"] = True
                del d["values"], d["base"]
            if c.get("reg_bind"):
                d["reg_bind"] = dict(c["reg_bind"])
            if c.get("file"):                            # a file the person selects: no values of its own (the record's one value is a blank), no default, no base
                d = {"key": c["key"], "apply": c["apply"], "file": True, "when": c["when"]}
            elif c["when"] in ("text", "secret"):          # a text the person types: values[0] is its default (a blank stands for none), base its longest length, values[1] what an empty text shows
                d = {"key": c["key"], "apply": c["apply"], "default": c["values"][0].strip(), "maxlen": c["base"]}
                d["secret" if c["when"] == "secret" else "text"] = True
                if len(c["values"]) > 1:
                    d["empty"] = c["values"][1]
                if len(c["values"]) > 2:
                    d["pick"] = c["values"][2]
            elif c.get("pick"):                          # a choice among values whose list is filled at run time
                d["pick"] = c["pick"]
            if c["group"] is not None:
                d["group"] = c["group"]
            if c.get("label"):
                d["label"] = c["label"]
            if c.get("show_state"):
                d["show_state"] = c["show_state"]
            if c.get("lock_state"):
                d["lock_state"] = c["lock_state"]
            doc["core_setting"].append(d)
    return doc


def merge_lib_menus(desc_blob, libs):
    """The core's description with the menus of its libraries merged in: libs is a list of (name, record bytes). What a library contributes - nodes,
    file actions, settings and tabs - is appended after the description's own, the libraries in the order of their names; the record then lists them as
    merged, and the application does not add their menus again at run time. A library that cannot be merged stays for the run time and is named in the
    second result: one that declares a poll (a record has room for one), or one that carries more than contributions (a route, hide / rename of
    others, the core's own tables). Returns (blob, merged names, [(name, reason)])."""
    base = parse_desc(desc_blob) if desc_blob else None
    doc = decoded_to_doc(base) if base else {}
    merged = list(base["merged"]) if base else []
    kept = []
    for name, blob in sorted(libs, key=lambda x: x[0]):
        rec = parse_desc(blob)
        if rec["route"] or rec["hide"] or rec["rename"] or rec["own"] or rec.get("pad") or rec.get("restart") or rec.get("key") or rec.get("window") or rec.get("block") or rec.get("group_route") or rec.get("place"):
            kept.append((name, "its record gives a route, hides or renames what others contribute, or carries a core's own tables, pad, restart, key table, windows, routes or places"))
            continue
        if rec["poll"]:
            kept.append((name, "it declares a poll, of which a description has one"))
            continue
        if name in merged:
            raise DescError("library %s is merged twice" % name)
        d = decoded_to_doc(rec)
        for k in ("node", "file_action", "setting", "tab"):
            if d.get(k):
                doc.setdefault(k, []).extend(d[k])
        merged.append(name)
    if len(merged) == (len(base["merged"]) if base else 0):
        return desc_blob, merged, kept
    return compile_desc(doc, merged), merged, kept


# ---- the check against what the application exports (T104.2 D10) -------------------------------------------------------------------
# The application exports its registry: one line per operation, one per state value, tab separated (tests/coreapp/ops_export.cc writes it):
#   op <TAB> name <TAB> arg:type:lo:hi:maxlen:label|label ...        type: int bool enum string path slot blob
#   state <TAB> name
# The checks are those of desc_check.cc / bindcheck.cc on the merged descriptor, made on the record, so a menu that names an operation the application
# does not have, binds an argument wrongly or leaves one unbound is refused when the .cor is packed, not when the board starts.

ARG_TYPES = ("int", "bool", "enum", "string", "path", "slot", "blob")


def load_ops(text):
    ops, states = {}, set()
    for ln, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        if f[0] == "state" and len(f) == 2:
            states.add(f[1])
        elif f[0] == "op" and len(f) >= 2:
            args = []
            for a in f[2:]:
                p = a.split(":")
                if len(p) != 6 or p[1] not in ARG_TYPES:
                    raise DescError("ops list line %d: a bad argument %r" % (ln, a))
                args.append({"name": p[0], "type": p[1], "lo": int(p[2]), "hi": int(p[3]), "maxlen": int(p[4]), "labels": p[5].split("|") if p[5] else []})
            ops[f[1]] = args
        else:
            raise DescError("ops list line %d: not an op or a state" % ln)
    return ops, states


def _bind_problem(ops, what, label, op, binds, file_ok, ask_ok, text_ok):
    if "$slot" in op or any("$slot" in (b.get("s") or "") for b in binds):
        return None                              # a declaration made once per drive slot: the names exist only with the core's capabilities
    if op not in ops:
        return '%s: "%s" calls operation "%s", which the application does not export' % (what, label, op)
    args = ops[op]
    seen = set()
    for b in binds:
        a = next((x for x in args if x["name"] == b["arg"]), None)
        if a is None:
            return '%s: "%s" binds "%s", which %s has no argument of' % (what, label, b["arg"], op)
        if b["arg"] in seen:
            return '%s: "%s" binds "%s" twice' % (what, label, b["arg"])
        seen.add(b["arg"])
        textual = a["type"] in ("string", "path", "slot")
        numeric = a["type"] in ("int", "bool", "enum")
        k = b["kind"]
        if k == BIND_KINDS["file"]:
            if not file_ok:
                return '%s: "%s" has no file to bind to "%s"' % (what, label, b["arg"])
            if not textual:
                return '%s: "%s" binds the file to "%s" of %s, which is not text' % (what, label, b["arg"], op)
        elif k == BIND_KINDS["int"]:
            if not numeric:
                return '%s: "%s" binds a number to "%s" of %s, which is not a number' % (what, label, b["arg"], op)
            if not a["lo"] <= b["i"] <= a["hi"]:
                return '%s: "%s" binds %d to "%s" of %s, which takes %d .. %d' % (what, label, b["i"], b["arg"], op, a["lo"], a["hi"])
        elif k == BIND_KINDS["str"]:
            lab = a["type"] == "enum" and any(l.lower() == b["s"].lower() for l in a["labels"])
            if not ((textual and len(b["s"]) <= a["maxlen"]) or lab):
                return '%s: "%s" binds "%s" to "%s" of %s, which does not take it' % (what, label, b["s"], b["arg"], op)
        elif k == BIND_KINDS["text"]:
            if not text_ok:
                return '%s: "%s" binds the typed text to "%s", but the entry asks for none (ask)' % (what, label, b["arg"])
            if a["type"] != "string":
                return '%s: "%s" binds the typed text to "%s" of %s, which is not a string' % (what, label, b["arg"], op)
        elif k == BIND_KINDS["ask"]:
            if not ask_ok:
                return '%s: "%s" binds "%s" to the typed address, but the action asks for none (ask)' % (what, label, b["arg"])
            if not numeric or a["lo"] > 0 or a["hi"] < 0xFFFF:
                return '%s: "%s" binds the typed address to "%s" of %s, which does not take 0 .. 65535' % (what, label, b["arg"], op)
    for a in args:
        if a["name"] not in seen:
            return '%s: "%s" leaves argument "%s" of %s unbound' % (what, label, a["name"], op)
    return None


APP_KEYS = ("picture", "scale", "smoothing", "frame follow")   # the application's own rows (coreapp/src/desc.h PICTURE_KEY, SCALE_KEY, SMOOTH_KEY, FOLLOW_KEY)
APP_OPS = ("picture.apply", "scale.apply", "smooth.apply", "follow.apply")  # and their operations (picture.h); desc_check.cc refuses the same


def _app_owned(what, key, apply):
    """T228 step 2: refuse at pack time what desc_check refuses (a core setting that takes the key or the operation of Picture, Scale, Smoothing or Frame follow)."""
    if key.lower() in APP_KEYS:
        raise DescError("%s: the keys Picture, Scale, Smoothing and Frame follow belong to the application" % what)
    if apply in APP_OPS:
        raise DescError("%s: the operations picture.apply, scale.apply, smooth.apply and follow.apply belong to the application's own Picture, Scale, Smoothing and Frame follow rows" % what)


def _setting_problem(ops, what, key, apply, nvalues, base):
    if "$slot" in apply:
        return None
    if apply not in ops:
        return '%s: "%s" is applied by operation "%s", which the application does not export' % (what, key, apply)
    a = ops[apply]
    if len(a) != 1 or a[0]["name"] != "value" or a[0]["type"] not in ("int", "enum"):
        return '%s: operation "%s" has to take exactly one number or enum named "value"' % (what, apply)
    if a[0]["lo"] > base or a[0]["hi"] < base + nvalues - 1:
        return '%s: "%s" has the values %d .. %d, operation "%s" takes %d .. %d' % (what, key, base, base + nvalues - 1, apply, a[0]["lo"], a[0]["hi"])
    return None


def check_ops(rec, ops, states):
    """rec: parse_desc's answer. Returns a list of sentences, empty when every name and binding is something the application exports."""
    bad = []

    def add(x):
        if x:
            bad.append(x)

    def state_ok(what, label, name):
        if name and "$slot" not in name and name not in states:
            bad.append('%s: "%s" reads state "%s", which the application does not export' % (what, label, name))

    for i, kr in enumerate(rec.get("key") or []):                    # base 3.3: the operation of a key row takes `value` or `key` as its one int argument, or none
        if kr["do"] != "op":
            pass
        elif "$slot" in kr["op"]:
            continue
        elif kr["op"] not in ops:
            bad.append('key row %d: operation "%s" is not exported by the application' % (i + 1, kr["op"]))
        else:
            args = ops[kr["op"]]
            want = ["value"] if "value" in kr else ["key"] if kr.get("pass_key") else []
            if [a["name"] for a in args] != want or any(a["type"] not in ("int", "enum", "bool") for a in args):
                bad.append('key row %d: operation "%s" takes (%s), the row gives (%s)' % (i + 1, kr["op"], ", ".join(a["name"] for a in args), ", ".join(want)))
            elif "value" in kr and not args[0]["lo"] <= kr["value"] <= args[0]["hi"]:
                bad.append('key row %d: value %d is outside %d .. %d of "%s"' % (i + 1, kr["value"], args[0]["lo"], args[0]["hi"], kr["op"]))
        if kr.get("when"):
            state_ok("key row %d" % (i + 1), kr.get("op") or kr["do"], kr["when"].lstrip("!").split()[0])
    for i, pl in enumerate(rec.get("place") or []):                   # D32: a place names an operation the application exports (a typo must not leave a row where it was)
        if pl["op"] not in ops:
            bad.append('place %d: operation "%s" is not exported by the application' % (i + 1, pl["op"]))
    for tk in rec["task"]:
        add(_bind_problem(ops, "task", tk["label"], tk["op"], tk["bind"], False, False, bool(tk["ask"])))
        if tk["ask"]:
            if sum(b["kind"] == BIND_KINDS["text"] for b in tk["bind"]) != 1:
                bad.append('task: "%s" asks for a name but binds it %d times (once)' % (tk["label"], sum(b["kind"] == BIND_KINDS["text"] for b in tk["bind"])))
            state_ok("task", tk["label"], tk["ask_state"])
        if tk.get("show_state"):
            state_ok("task", tk["label"], tk["show_state"].split()[0])
    for m in rec["media"]:
        state_ok("media", m["name"], m["state"])
        if m["eject_op"]:
            add(_bind_problem(ops, "media", m["name"], m["eject_op"], m["eject_bind"], False, False, False))
    for ft in rec["ftype"]:
        for a in ft["actions"]:
            add(_bind_problem(ops, "filetype " + ft["ext"], a["label"], a["op"], a["bind"], True, bool(a["ask"]) or bool(a.get("repeat_op")), False))
            if a.get("repeat_op"):                                       # T228 R21: the counting operation takes the same binding (asked value 0)
                add(_bind_problem(ops, "filetype " + ft["ext"], a["label"], a["repeat_op"], a["bind"], True, True, False))
            if a["ask"] and sum(b["kind"] == BIND_KINDS["ask"] for b in a["bind"]) != 1:
                bad.append('filetype %s: "%s" asks for an address but binds it %d times (once)' % (ft["ext"], a["label"], sum(b["kind"] == BIND_KINDS["ask"] for b in a["bind"])))
        for a in ft["inner"]:
            add(_bind_problem(ops, "filetype %s inner" % ft["ext"], a["label"], a["op"], a["bind"], True, False, False))
    for cs in rec["cset"]:
        if cs["apply"] is not None:                                  # a setting that is register bits (reg_bind) calls no operation
            add(_setting_problem(ops, "core_setting", cs["key"], cs["apply"], len(cs["values"]), cs["base"]))
    for n in rec["node"]:
        ui = _ui_table(n.get("ui"), UI_NODE_KEYS)
        if n["kind"] == NODE_KINDS["op"]:
            if n["op"] or not ("goto" in ui or "off" in ui):
                add(_bind_problem(ops, "node", n["label"], n["op"] or "", n["bind"], False, False, "ask" in ui))     # ui ask: the node prompts for a name first (MenuCtl::begin_ask), the text goes to the argument bound with `text`
        elif n["kind"] == NODE_KINDS["state"]:
            if n["state"] or not ("sub" in ui or "chip" in ui):
                state_ok("node", n["label"], n["state"])
        for k in ("sub", "chip"):
            if ui.get(k, "").startswith("@") and not ui[k].startswith(("@info.", "@set.")):         # @set.<Key> is a setting of the core, not a state value
                state_ok("node", n["label"], ui[k][1:].split("|")[0])
        if n["when_state"]:
            state_ok("node", n["label"], n["when_state"].split()[0])
        if "open" in ui:                         # the page this row stands on runs it when opened: an operation without arguments
            a = ops.get(ui["open"])
            if a is None or a:
                add('node "%s": ui open "%s" is not an exported operation without arguments' % (n["label"], ui["open"]))
    for a in rec["action"]:
        add(_bind_problem(ops, "file_action", a["label"], a["op"], a["bind"], True, False, False))
    for st_ in rec["setting"]:
        add(_setting_problem(ops, "setting", st_["key"], st_["apply"], len(st_["values"]) or st_["hi"] - st_["lo"] + 1, st_["lo"] if not st_["values"] else 0))
    if rec["poll"]:
        a = ops.get(rec["poll"]["op"])
        if a is None or a:
            bad.append('poll: "%s" is not an exported operation without arguments' % rec["poll"]["op"])
    return bad


def _ident_at(blob):
    """(offset of the identity section's four words, the record's decoded identity) of a version 7 record, or None for a record without TOML A. The record is read whole first
    (parse_desc), so a damaged one is the same refusal as everywhere."""
    d = parse_desc(blob)
    if "identity" not in d:
        return None
    soff, ssize = struct.unpack_from("<II", blob, HEADER_V5)
    pos = soff
    while pos < soff + ssize:
        sid, ln = struct.unpack_from("<II", blob, pos)
        if sid == SEC_IDENT:
            return pos + 8, d["identity"]
        pos += 8 + ln
    raise DescError("description record: the identity section is not where the reader found it")


def identity_build(blob):
    """The build word the record of TOML A carries (P1-4), or None: a record without TOML A, or one that no packer has stamped yet."""
    at = _ident_at(blob)
    return None if at is None else at[1].get("build")


def stamp_build(blob, build):
    """T239.3 P1-4 (D30 condition 2, D31): the bitstream's build word into the identity of a version 7 record (cor_pack writes it, the service compares it with the standard block at start).
    A record without TOML A comes back as it was. A record that already carries another build word is refused: the source's number says it was meant for another bitstream.
    The bytes of the record change in the build word and the flag only."""
    at = _ident_at(blob)
    if at is None:
        return blob
    off, ident = at
    if not 0 <= build < 2 ** 32:
        raise DescError("identity: the build word is a 32 bit number")
    if "build" in ident and ident["build"] != build:
        raise DescError("identity: the record carries the build %08x, the bitstream %08x" % (ident["build"], build))
    out = bytearray(blob)
    struct.pack_into("<II", out, off + 4, 1, build)          # words of the section: id, build flag, build, block
    return bytes(out)


def ldfrag(text, maps=None, what="TOML A"):
    """The linker-script fragment of a TOML A source (T239.3 P1-2): `__iobase` and `__u2p_io_base` from [io], `__blk_<block>` (the address) and
    `__reg_<block>_<property>` (the address plus the offset) for every block and property. The headers take the two bases from these symbols and a driver takes
    its registers from them, so no address is a literal in the code. [io] and [[block]] are checked as in a record; [identity] is checked when there is one
    (a fragment of two bases and a few blocks needs none)."""
    return ldfrag_doc(resolve_maps(toml_loads(text), maps), what)


def merge_toml_a(core, base, names, what="TOML A"):
    """The TOML A an app is linked against when the core's own TOML A (`core`, a parsed document) names the blocks of its own gateware only (T239.3 V-4, D30): the base's blocks
    that the app's TOML B asks for by name (`names`, [app] base_blocks) come from `base` (the SDK's gateware/shared.toml), the core's own blocks and its identity are the core's.
    Refused: a name `base` does not have, a name the core's file has too, two different [io] tables. Nothing else of `base` is in the result, so the driver table, the linker
    fragment and the link hold the blocks the two files name and no others. The overlap rule of a record (compile_desc) applies to the result."""
    for doc, n in ((core, "the core's TOML A"), (base, "the base's TOML A")):
        extra = set(doc) - set(TOMLA_KEYS)
        if extra:
            raise DescError("%s: %s: %s is not a table of TOML A" % (what, n, ", ".join(sorted(extra))))
    if core.get("io") != base.get("io"):
        raise DescError("%s: the [io] of the core's TOML A (%s) is not the base's (%s)" % (what, core.get("io"), base.get("io")))
    have = {b["name"]: b for b in base.get("block", [])}
    own = {b["name"] for b in core.get("block", [])}
    blocks = []
    for n in names:
        if n not in have:
            raise DescError("%s: base_blocks names %s, which the base's TOML A does not have (it has: %s)" % (what, n, ", ".join(sorted(have))))
        if n in own:
            raise DescError("%s: base_blocks names %s, which the core's TOML A has too" % (what, n))
        if any(b["name"] == n for b in blocks):
            raise DescError("%s: base_blocks names %s twice" % (what, n))
        blocks.append(have[n])
    out = {"io": core["io"], "block": blocks + list(core.get("block", []))}
    if "identity" in core:
        out["identity"] = core["identity"]
    return out


def ldfrag_doc(doc, what="TOML A"):
    """ldfrag of a document that has been read and had its maps resolved."""
    extra = set(doc) - set(TOMLA_KEYS)
    if extra:
        raise DescError("%s: %s is not a table of TOML A" % (what, ", ".join(sorted(extra))))
    io, blocks = _need(doc, "io", dict, what), _need(doc, "block", list, what)
    _sec_io(io)
    st = _Strings()
    _, names = _sec_blocks(blocks, st)
    if "identity" in doc:
        _sec_ident(_need(doc, "identity", dict, what), names, blocks, st)
    out = ["/* generated by tools/descrec.py ldfrag from %s; do not edit */\n" % os.path.basename(what)]
    for k in IO_KEYS:
        out.append("__%s = 0x%08X;\n" % (k, io[k]))
    seen = {"__" + k for k in IO_KEYS}
    def sym(name, value):
        if name in seen:
            raise DescError("%s: the linker symbol %s is made twice (a block name and a property name join with '_')" % (what, name))
        seen.add(name)
        out.append("%s = 0x%08X;\n" % (name, value))
    for b in blocks:
        sym("__blk_%s" % b["name"], b["address"])
        for p in b.get("property", []):
            sym("__reg_%s_%s" % (b["name"], p["name"]), b["address"] + p["offset"])
    return "".join(out)


def merge_docs(docs):
    """One document of several: `docs` is a list of (file name, parsed TOML dict). A top-level table or key that two files both have is refused with both names
    (T239.3 base 3.3: a core's menu.toml and desc.toml are one record 6; each file has what the other has not)."""
    out, owner = {}, {}
    for name, doc in docs:
        for k, v in doc.items():
            if k in out:
                raise DescError("%s and %s both define %r; a table or key is in one file only" % (owner[k], name, k))
            out[k] = v
            owner[k] = name
    return out


def compile_files(files, ops_text=None, maps=None):
    """The record of one or more TOML files ((name, text) pairs): the files are read one by one and merged (merge_docs), then compiled as one. Errors of a
    file's own syntax name the file; a table in two files names both."""
    docs = []
    for name, text in files:
        try:
            docs.append((name, resolve_toml_a_refs(toml_loads(text), name)))
        except DescError as e:
            raise DescError("%s: %s" % (name, e))
    doc = merge_docs(docs) if len(docs) > 1 else docs[0][1]
    blob = compile_desc(resolve_maps(doc, maps))
    if ops_text is not None:
        ops, states = load_ops(ops_text)
        bad = check_ops(parse_desc(blob), ops, states)
        if bad:
            raise DescError("%s: %d problem(s) against the application's operations:\n  %s" % (" + ".join(n for n, _ in files), len(bad), "\n  ".join(bad)))
    return blob


def compile_checked(text, ops_text=None, *, what="description", maps=None, path=None, app_base=False):
    """compile_text, and - given the application's export - check every name and binding of the result against it. `app_base`: the SDK's base UI TOML (compile_desc)."""
    blob = compile_desc(resolve_maps(resolve_toml_a_refs(toml_loads(text), path), maps), app_base=app_base)
    if ops_text is not None:
        ops, states = load_ops(ops_text)
        bad = check_ops(parse_desc(blob), ops, states)
        if bad:
            raise DescError("%s: %d problem(s) against the application's operations:\n  %s" % (what, len(bad), "\n  ".join(bad)))
    return blob


DRV_CTYPE = {8: "uint8_t", 16: "uint16_t", 32: "uint32_t"}


def drivers_files(doc, source="TOML A"):
    """The driver table of a TOML A document (T239.3 D31, D34), generated at build time and linked: (header text, source text).
    The document is checked by compile_desc first, so the generator and both readers hold the same rules. The header has, per block, its address, size and owner,
    per property its address and an access macro for each direction the map allows (`_RD()` for r and rw, `_WR(v)` for w and rw, of the property's width),
    a property with a count (a run of registers) has `_COUNT` and takes the element's index in its macros (`_RD(i)`, `_WR(i, v)`), and `_ADDR` is element 0,
    per field its shift and mask, and every state's value; a driver names a register only by these macros, so a block the bitstream lacks is a compile error
    and a block of the base can be asked for with `#ifdef DRV_HAS_<block>`. The source holds the table of the blocks (`drv_blocks`)."""
    import copy
    doc = copy.deepcopy(doc)
    compile_desc(doc)
    names, defs = set(), []

    def define(name, value):
        if name in names:
            raise DescError("driver table: the macro %s is made twice (a block, property, field or state name that repeats inside the prefix)" % name)
        names.add(name)
        defs.append("#define %s %s" % (name, value))

    io, ident = doc["io"], doc["identity"]
    define("DRV_IOBASE", "0x%08Xu" % io["iobase"])
    define("DRV_U2P_IO_BASE", "0x%08Xu" % io["u2p_io_base"])
    define("DRV_IDENT_ID", '"%s"' % ident["id"])
    define("DRV_IDENT_BLOCK", '"%s"' % ident["block"])
    rows = []
    for b in doc["block"]:
        bn = "DRV_" + b["name"]
        defs.append("")
        define("DRV_HAS_" + b["name"], "1")
        define(bn + "_ADDR", "0x%08Xu" % b["address"])
        define(bn + "_SIZE", "0x%Xu" % b["size"])
        define(bn + "_OWNER", "%d" % (0 if b["owner"] == "base" else 1))
        rows.append((b["name"], b["module"], 0 if b["owner"] == "base" else 1, b["address"], b["size"]))
        for p in b.get("property", []):
            pn = "%s_%s" % (bn, p["name"])
            ct = DRV_CTYPE[p["width"]]
            define(pn + "_ADDR", "(%s_ADDR + 0x%Xu)" % (bn, p["offset"]))
            if p.get("count", 1) > 1:
                # a run of registers: the element count, and the access macros take the element's index
                define(pn + "_COUNT", "%du" % p["count"])
                if "r" in p["dir"]:
                    define(pn + "_RD(i)", "(*DRV_PTR(%s, %s_ADDR + (uint32_t)(i) * %du))" % (ct, pn, p["width"] // 8))
                if "w" in p["dir"]:
                    define(pn + "_WR(i, v)", "(*DRV_PTR(%s, %s_ADDR + (uint32_t)(i) * %du) = (%s)(v))" % (ct, pn, p["width"] // 8, ct))
            else:
                if "r" in p["dir"]:
                    define(pn + "_RD()", "(*DRV_PTR(%s, %s_ADDR))" % (ct, pn))
                if "w" in p["dir"]:
                    define(pn + "_WR(v)", "(*DRV_PTR(%s, %s_ADDR) = (%s)(v))" % (ct, pn, ct))
            for x in p.get("state", []):
                define("%s_%s" % (pn, x["name"]), "%du" % x["value"] if x["value"] >= 0 else "(%d)" % x["value"])
            for f in p.get("field", []):
                fn = "%s_%s" % (pn, f["name"])
                define(fn + "_SHIFT", "%d" % f["shift"])
                define(fn + "_MASK", "0x%Xu" % (((1 << f["bits"]) - 1) << f["shift"]))
                for x in f.get("state", []):
                    define("%s_%s" % (fn, x["name"]), "%du" % x["value"] if x["value"] >= 0 else "(%d)" % x["value"])
    head = "\n".join([
        "/* Generated by tools/descrec.py drivers from %s (T239.3 D31, D34). Do not edit: the TOML A is the source. */" % source,
        "#ifndef DRVTAB_H", "#define DRVTAB_H", "", "#include <stdint.h>", "",
        "#ifdef DRV_PTR_HEADER                /* a native test redirects the registers (tests/drv_fake.h) */",
        "#include DRV_PTR_HEADER", "#endif", "#ifndef DRV_PTR",
        "#define DRV_PTR(type, addr) ((volatile type *)(uintptr_t)(addr))", "#endif", ""] + defs + [
        "", "#ifdef __cplusplus", 'extern "C" {', "#endif", "",
        "typedef struct DrvBlock { const char *name; const char *module; uint8_t owner; uint32_t address; uint32_t size; } DrvBlock;   /* owner 0 base, 1 core */",
        "extern const DrvBlock drv_blocks[];", "extern const unsigned drv_nblocks;", "",
        "#ifdef __cplusplus", "}", "#endif", "", "#endif", ""])
    src = "\n".join(["/* Generated by tools/descrec.py drivers from %s (T239.3 D31). Do not edit. */" % source, '#include "drvtab.h"', "",
                     "const DrvBlock drv_blocks[] = {"] +
                    ['    { "%s", "%s", %d, 0x%08Xu, 0x%Xu },' % r for r in rows] + ["};", "const unsigned drv_nblocks = %d;" % len(rows), ""])
    return head, src


if __name__ == "__main__":
    import sys
    argv = sys.argv[1:]
    ops_file = None
    if "--ops" in argv:
        i = argv.index("--ops")
        ops_file = argv[i + 1] if i + 1 < len(argv) else None
        del argv[i:i + 2]
    if "--sizes" in argv:                                   # a TOML B: its [sizes] table sets the record maxima before anything is compiled or read
        i = argv.index("--sizes")
        if i + 1 >= len(argv):
            sys.exit("descrec.py: --sizes needs the TOML B file")
        try:
            apply_sizes(toml_loads(open(argv[i + 1]).read()).get("sizes", {}))
        except (DescError, OSError) as e:
            sys.exit("descrec: %s: %s" % (argv[i + 1], e))
        del argv[i:i + 2]
    map_paths = []
    while "--maps" in argv:
        i = argv.index("--maps")
        if i + 1 >= len(argv):
            sys.exit("descrec.py compile desc.toml out.bin [--ops ops.txt] [--maps DIR_OR_FILE ...] [--sizes TOML_B] | descrec.py dump record.bin [--sizes TOML_B] | descrec.py drivers desc.toml OUT_DIR [--maps DIR_OR_FILE ...]")
        map_paths.append(argv[i + 1])
        del argv[i:i + 2]
    if len(argv) == 3 and argv[0] == "drivers":
        try:
            d = resolve_maps(toml_loads(open(argv[1]).read()), load_maps(map_paths))
            h, c = drivers_files(d, os.path.basename(argv[1]))
        except DescError as e:
            sys.exit("descrec: %s: %s" % (argv[1], e))
        os.makedirs(argv[2], exist_ok=True)
        for nm, txt in (("drvtab.h", h), ("drvtab.c", c)):                  # written only when changed, so make does not rebuild what depends on them
            fn = os.path.join(argv[2], nm)
            if not os.path.exists(fn) or open(fn).read() != txt:
                open(fn, "w").write(txt)
        sys.exit(0)
    if len(argv) not in (2, 3) or argv[0] not in ("compile", "dump", "ldfrag") or (argv[0] == "compile" and ops_file is None and "--ops" in sys.argv):
        sys.exit("descrec.py compile desc.toml out.bin [--ops ops.txt] [--maps DIR_OR_FILE ...] [--sizes TOML_B] | descrec.py dump record.bin [--sizes TOML_B] | descrec.py ldfrag toml_a.toml out.ld [--maps DIR_OR_FILE ...] | descrec.py drivers desc.toml OUT_DIR [--maps DIR_OR_FILE ...]")
    try:
        if argv[0] == "compile":
            blob = compile_checked(open(argv[1]).read(), open(ops_file).read() if ops_file else None, what=argv[1], maps=load_maps(map_paths), path=argv[1])
            open(argv[2] if len(argv) > 2 else argv[1] + ".bin", "wb").write(blob)
        elif argv[0] == "ldfrag":
            if len(argv) != 3:
                sys.exit("descrec.py ldfrag toml_a.toml out.ld [--maps DIR_OR_FILE ...]")
            open(argv[2], "w").write(ldfrag(open(argv[1]).read(), load_maps(map_paths), argv[1]))
        else:
            import json
            print(json.dumps(parse_desc(open(argv[1], "rb").read()), indent=1))
    except DescError as e:
        sys.exit("descrec: %s" % e)
