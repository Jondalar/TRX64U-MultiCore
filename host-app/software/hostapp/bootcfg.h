// The boot menu file Flash:/boot.cfg (T84): countdown, default entry and the cores the user added.
//
//   countdown=5            seconds before the default starts by itself; 0 = never
//   autostart_s=5          T139: the seconds the countdown has when it is on, kept while it is off (countdown=0). A file without the
//                          line gets the countdown's seconds (or 5 while it is 0); countdown > 0 always wins over a stale value.
//   direct=1               T135: the default starts at power-on without the menu (a held C= or port-1 fire, or a return from a core, shows
//                          it); 0 = the menu with the countdown. Absent = 1.
//   default=0              index of the default entry, -1 = none
//   video=<system>,<hdmi>  the picture: system 0 PAL / 1 NTSC, hdmi 0 576p/480p, 1 720p, 2 1080p,
//                          3 800x600, 4 1024x768, 5 1280x1024. Missing or -1 = ask the screen (T105).
//   lib_attempt=1          written by a core application (coreapp/src/libguard.h): a start that loads libraries is under way and has not
//                          finished; absent when 0. The host keeps it across its own saves.
//   core=<path>|<name>|<sha256 of the partial>[|<file size>[|<mtime>[|<header digest hex>]]]
//   Taken when the entry was added, after the one full check (T101 S10, T115 section 7). A start compares the directory
//   entry (size, mtime: FAT date<<16 | time) and the package header (startcheck.h) with these and reads no more of the file;
//   an entry without mtime (older boot.cfg) gets the full check once at the next start.
//   core=...|<header digest hex>|<default config>   T198 D8: the seventh field is the entry's default config (a name in <core folder>/configs/); the countdown and the
//   direct boot start the entry with it, RETURN on the row still starts plain. Written only when there is one; an older host reads the line without it.
#ifndef HOST_BOOTCFG_H
#define HOST_BOOTCFG_H

#include "configlist.h"
#define BOOT_MAX 32                       // T223 D1 (Alex, 2026-11-15): 32 entries; the boot ROM does not read boot.cfg
#define BOOT_COUNTDOWN_DEFAULT 5          // Alex, T84 D4a: the default starts by itself after 5 s
#define BOOT_AUTOSTART_MAX 60
#define BOOT_NAME_MAX 47                  // BootEntry::name without its terminator

struct BootEntry {
    char name[48];
    char path[96];
    char hash[65];
    unsigned size;            // bundle file size at the full check, 0 = unknown
    unsigned mtime;           // directory entry date<<16|time at the full check, 0 = never checked (older boot.cfg)
    unsigned hdr;             // digest of the package header at the full check (CorInfo::hdr_digest), 0 for a firmware image
    char defcfg[CONFIG_NAME_MAX + 1];   // T198 D8: the entry's default config, "" = none
};

struct BootCfg {
    int countdown;            // 0 = the menu does not start the default by itself
    int autostart_s;          // T139: 1..60, what countdown becomes when autostart is switched on again
    int direct;               // T135 D4/D7: 1 = start the default at power-on without the menu
    int def;
    int vid_sys;              // -1 = not chosen
    int vid_hdmi;             // -1 = not chosen: the screen is asked
    int lib_attempt;          // 0 = none under way
    int n;
    BootEntry e[BOOT_MAX];
};

#define BOOTCFG_DIR  "/Flash"
#define BOOTCFG_FILE "boot.cfg"
#define BOOTCFG_PATH BOOTCFG_DIR "/" BOOTCFG_FILE

void bootcfg_init(BootCfg *c);
void bootcfg_parse(BootCfg *c, const char *text);
int  bootcfg_format(const BootCfg *c, char *out, int max);   // length, -1 if it does not fit
bool bootcfg_add(BootCfg *c, const char *name, const char *path, const char *hash, unsigned size = 0, unsigned mtime = 0, unsigned hdr = 0);
void bootcfg_remove(BootCfg *c, int i);
// T139: a new display name for entry i; order, default and everything else of the entry stay. False (nothing changed) for a bad index, an
// empty or blank name, one longer than BOOT_NAME_MAX, or one with '|' or a line break in it.
bool bootcfg_rename(BootCfg *c, int i, const char *name);
// T198 D8: the default config of entry i ("" = none). False (nothing changed) for a bad index or a name config_name_ok refuses.
bool bootcfg_set_defcfg(BootCfg *c, int i, const char *name);
// T139: autostart on/off; secs (1..60, clamped; 0 = keep autostart_s) is what the countdown runs for. Off is countdown=0.
void bootcfg_autostart(BootCfg *c, bool on, int secs);
void bootcfg_move(BootCfg *c, int i, int dir);               // dir -1 up, +1 down; default follows

#endif
