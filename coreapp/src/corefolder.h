// The core folder (T148): every core has one folder beside its .cor, "<dir>/<name>/" for "<dir>/<name>.cor", holding its settings
// ("<name>.cfg"), its ROMs ("roms/") and its saves ("saves/"). Nothing in it is shared with another core. Pure: the file system is a CoreFs.
#ifndef COREAPP_COREFOLDER_H
#define COREAPP_COREFOLDER_H

#include "fsif.h"

// "<dir>/<name>.cor" -> "<dir>/<name>". False (out empty) when the path is empty, relative, has no ".cor" extension, has no folder below a
// medium ("/x.cor"), or does not fit (refused, never cut).
bool corefolder_dir(const char *cor_path, char *out, int outlen);
// "<dir>/<name>/<sub>" for sub = "roms" or "saves".
bool corefolder_sub(const char *cor_path, const char *sub, char *out, int outlen);
// "<dir>/<name>/<name>.cfg" (D1).
bool corefolder_cfg(const char *cor_path, char *out, int outlen);
// The old settings place, "<dir>/<name>.cfg" beside the .cor (T93 S6).
bool corefolder_old_cfg(const char *cor_path, char *out, int outlen);
// The old shared folder "<dir>/roms" beside the .cor: a source of copies for a file the core names, never a place a core reads.
bool corefolder_old_roms(const char *cor_path, char *out, int outlen);
// D5: the path of a save file of the core, "<dir>/<name>/saves/<file>" (battery RAM, .c64re snapshot, .c64ring). False when the file name
// is empty or holds a slash, or nothing fits.
bool corefolder_save_path(const char *cor_path, const char *file, char *out, int outlen);

// D1, D5: makes the folder, roms/ and saves/ (what exists stays). False when one of them cannot be made (a read only medium).
bool corefolder_ensure(CoreFs *fs, const char *cor_path);

// D1: the settings of an older layout move into the folder once: "<dir>/<name>.cfg" is copied to the new place, read back and compared,
// and only then removed. Nothing happens when the new file exists or the old one does not. True when it moved.
bool corefolder_move_cfg(CoreFs *fs, const char *cor_path);

// D2: one file copied from `src` to `dst`, verified: the copy is read back and its size and SHA-256 must equal the source's. A copy that
// does not verify is removed. Logs "[core] ROM <name>: copied from <src> to <dst>". False when the file does not fit `max` or on any error.
bool corefolder_copy_verified(CoreFs *fs, const char *src, const char *dst, uint8_t *buf, uint32_t max);

#endif
