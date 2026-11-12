// T163 D5: the host application's own settings store. The stores that register with the ConfigManager ("Network Settings",
// "Ethernet Settings") keep their values in one text file, /Flash/trx/host.cfg, in the [section] "Item text=Value" format of the
// cores' .cfg (coreapp/src/cfgfile.cc is the shared reader/writer). Gideon's and Commodore's firmware never read that file, so
// neither can change ours, and ours never writes their SPI-flash config pages.
//
// First start (D2): while the file does not exist, a store that registers is taken over once from Gideon's flash page (read
// only, config.cc) and its section is written; from then on the file is the only source.
#ifndef HOSTCFG_H
#define HOSTCFG_H

#include "config.h"

#define HOSTCFG_DIR  "/Flash/trx"
#define HOSTCFG_FILE "host.cfg"
#define HOSTCFG_MAX  8192      // the whole file; a save that would not fit is refused and the old file stays

// The file access, supplied by the application (hostcfg_io.cc on the board, memory in the test).
struct HostCfgIo
{
    int  (*read)(char *buf, int max);              // the file's length, 0 for an empty file, -1 when it does not exist
    bool (*write)(const char *buf, int len);       // replaces the file (and makes the directory)
};

// Sets the io and makes the host's ConfigBackend the manager's. Forgets what an earlier call read (a test starts a "boot" with it).
void hostcfg_install(const HostCfgIo *io);

// The ConfigBackend functions, public for the test.
int  hostcfg_load(ConfigStore *s);
void hostcfg_save(ConfigStore *s);

// The board's io (FileManager on /Flash); installs itself with hostcfg_install.
void hostcfg_install_board(void);

#endif
