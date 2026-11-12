// The descriptor pieces of drive A for a core whose description has the capability `drive_a` (T104.1 D15). The menu merge of D8 (a library's
// nodes, settings and file actions merged into the core's tree) is not built, so a core splices these into its own tables; the entries are
// the ones of D15's TOML: Reset Drive, the file actions on .d64 and .g64, the four settings, the Remove Disk of the media slot, the drive
// ROM. Include it once per descriptor, inside the file that defines the tables.
//
// A core whose gateware has the 1581 (the C64U's mm_drive) adds CBM1581_FILETYPE and CBM1581_ROM as well; none of the cores on the generic
// application has it today (T103: the 1571 is out, the 1581 is in cores/c64u only).
#ifndef COREAPP_CBM1541_DESC_H
#define COREAPP_CBM1541_DESC_H

#include "desc.h"

static const char *const c1541_on_names[]  = { "Disabled", "Enabled" };
static const char *const c1541_id_names[]  = { "8", "9", "10", "11" };
static const char *const c1541_swap_names[] = { "100 ms", "200 ms", "300 ms", "400 ms", "500 ms", "600 ms", "700 ms", "800 ms", "900 ms", "1000 ms" };

// The names and the defaults are Gideon's (c1541.cc:46-62): drive on, bus ID 8, no extra RAM, 100 ms swap delay.
static const char *const c1541_led_names[] = { "On", "Off" };   // T159 D-all: the A1200's "LED Load Indicator" (base operation led.load)
#define CBM1541_NSETTINGS 5
#define CBM1541_SETTINGS \
    { "Drive",           c1541_on_names,   2, 1, WHEN_LIVE, "drive.set_power",      0 }, \
    { "Drive Bus ID",    c1541_id_names,   4, 0, WHEN_LIVE, "drive.set_bus_id",     0 }, \
    { "Extra RAM",       c1541_on_names,   2, 0, WHEN_LIVE, "drive.set_ram",        0 }, \
    { "Disk swap delay", c1541_swap_names, 10, 0, WHEN_LIVE, "drive.set_swap_delay", 0 }, \
    { "LED Load Indicator", c1541_led_names, 2, 0, WHEN_LIVE, "led.load", 0 }

static const OpBind c1541_bind_rw[]    = { B_STR("slot", "iec:drive_a"), B_FILE("path"), B_STR("mode", "rw") };
static const OpBind c1541_bind_ro[]    = { B_STR("slot", "iec:drive_a"), B_FILE("path"), B_STR("mode", "ro") };
static const OpBind c1541_bind_ul[]    = { B_STR("slot", "iec:drive_a"), B_FILE("path"), B_STR("mode", "unlinked") };
static const OpBind c1541_bind_run[]   = { B_STR("slot", "iec:drive_a"), B_FILE("path") };
static const OpBind c1541_bind_slot[]  = { B_STR("slot", "iec:drive_a") };
// the name prompt of Create and Save (Task::ask): the typed text is the path argument, a bare name that the library puts into the usual folder
static const OpBind c1541_bind_new_d64[] = { B_TEXT("path"), B_STR("format", "d64"), B_STR("name", "") };
static const OpBind c1541_bind_new_g64[] = { B_TEXT("path"), B_STR("format", "g64"), B_STR("name", "") };
static const OpBind c1541_bind_new_d81[] = { B_TEXT("path"), B_STR("format", "d81"), B_STR("name", "") };
static const OpBind c1541_bind_save_d64[] = { B_STR("slot", "iec:drive_a"), B_TEXT("path"), B_STR("format", "d64") };
static const OpBind c1541_bind_save_g64[] = { B_STR("slot", "iec:drive_a"), B_TEXT("path"), B_STR("format", "g64") };
static const OpBind c1541_bind_browse[] = { B_FILE("path") };
// a PRG inside a disk image (the image is a folder for the browser, d1541_fs.h): Mount & Run lets the core run the file read out of the image,
// Real Run resets the machine and LOADs it from the drive
static const OpBind c1541_bind_in_dma[]  = { B_STR("slot", "iec:drive_a"), B_FILE("path"), B_STR("how", "dma") };
static const OpBind c1541_bind_in_load[] = { B_STR("slot", "iec:drive_a"), B_FILE("path"), B_STR("how", "load") };

static const FileAction c1541_d64_actions[] = {
    { "Mount Disk",           "drive.mount",     c1541_bind_rw,  3, true, 0, 0 },
    { "Mount Disk Read Only", "drive.mount",     c1541_bind_ro,  3, true, 0, 0 },
    { "Mount Disk Unlinked",  "drive.mount",     c1541_bind_ul,  3, true, 0, 0 },
    { "Run Disk",             "drive.mount_run", c1541_bind_run, 2, true, 0, 0 },
    { "Browse Disk",          "menu.browse",     c1541_bind_browse, 1, false, 0, 0 },
};
static const FileAction c1541_g64_actions[] = {
    { "Mount Disk",           "drive.mount",     c1541_bind_rw,  3, true, 0, 0 },
    { "Mount Disk Read Only", "drive.mount",     c1541_bind_ro,  3, true, 0, 0 },
    { "Mount Disk Unlinked",  "drive.mount",     c1541_bind_ul,  3, true, 0, 0 },
    { "Run Disk",             "drive.mount_run", c1541_bind_run, 2, true, 0, 0 },
};
static const FileAction c1541_inner_actions[] = {
    { "Mount & Run",          "drive.mount_run_file", c1541_bind_in_dma,  3, true, 0, 0 },
    { "Real Run",             "drive.mount_run_file", c1541_bind_in_load, 3, true, 0, 0 },
};
// A ROM file picked in the browser (16 or 32 KB, the operation checks): the drive takes it for this session
static const FileAction c1541_rom_actions[] = {
    { "Set as 1541 ROM", "drive.set_rom", c1541_bind_run, 2, false, 0, 0 },
};
#define CBM1541_NFILETYPES 3
#define CBM1541_FILETYPES \
    { ".d64", c1541_d64_actions, 5, ".prg", c1541_inner_actions, 2 }, \
    { ".g64", c1541_g64_actions, 4, 0, 0, 0 }, \
    { ".rom", c1541_rom_actions, 1, 0, 0, 0 }

// the 1581 (a core with CAPAB_MM_DRIVE): .d81 mounts read-write or read only, no unlinked mount
static const FileAction c1581_disk_actions[] = {
    { "Mount Disk",           "drive.mount",     c1541_bind_rw,  3, true, 0, 0 },
    { "Mount Disk Read Only", "drive.mount",     c1541_bind_ro,  3, true, 0, 0 },
    { "Run Disk",             "drive.mount_run", c1541_bind_run, 2, true, 0, 0 },
    { "Browse Disk",          "menu.browse",     c1541_bind_browse, 1, false, 0, 0 },
};
#define CBM1581_FILETYPE { ".d81", c1581_disk_actions, 4, ".prg", c1541_inner_actions, 2 }

#define CBM1541_NTASKS 5
#define CBM1541_TASKS \
    { "Reset Drive",         "drive.reset",  c1541_bind_slot,     1, false, 0, 0 }, \
    { "Create blank D64",    "drive.create", c1541_bind_new_d64,  3, false, "Name of the new D64",  "drive.suggest.d64", true }, \
    { "Create blank G64",    "drive.create", c1541_bind_new_g64,  3, false, "Name of the new G64",  "drive.suggest.g64", true }, \
    { "Save Disk as D64",    "drive.save",   c1541_bind_save_d64, 3, false, "Save as D64, name",    "drive.suggest.save_d64" }, \
    { "Save Disk as G64",    "drive.save",   c1541_bind_save_g64, 3, false, "Save as G64, name",    "drive.suggest.save_g64" }

// A core with a 1581 adds this task to its own table, behind CBM1541_TASKS (T122: the blank disk has header and BAM on track 40 and an empty
// directory, d81_blank in the library). No core of the generic application has the 1581 today, so no menu shows it.
#define CBM1581_NTASKS 1
#define CBM1581_TASKS \
    { "Create blank D81",    "drive.create", c1541_bind_new_d81,  3, false, "Name of the new D81",  "drive.suggest.d81", true }

#define CBM1541_MEDIA { "Drive A:", "drive.drive_a.image", "drive.eject", c1541_bind_slot, 1 }

// The drive's ROM is optional: the machine runs without a drive. 16 or 32 KB, the names of the VICE and Gideon sets. It is a shared device ROM
// (RomSpec::shared): the core's folder first, then <root>/roms where the C64 side keeps it.
#define CBM1541_ROM \
    { "1541 drive", { "1541.rom", "1541.bin", "1541-ii.bin", "1541-ii.rom", "1541ii.rom", "1541c.rom", "dos1541.rom", "dos1541-325302-01+901229-05.bin", 0 }, \
      { 16384, 32768, 0 }, false, 0, "drive.put_rom", 0, true }
#define CBM1581_ROM \
    { "1581 drive", { "1581.rom", "1581.bin", "dos1581.rom", 0 }, { 32768, 0 }, false, 0, "drive.put_rom1581", 0 }

#endif
