// The files a core installs on the SD card from its own .cor (record 8, tools/cor_pack.py: its docstring is the format). Alex 2026-11-16 (T175): the diag core's
// example tests are TOML files that ride in diag.cor; no other core's bundle carries them and the application image holds none.
//
// At the start of the core every file of the record that is missing is written to <core folder>/<path>. A file that is there is never touched, except one that
// is byte for byte (size and FNV-1a 64) an earlier version the record lists for it: a tester who never edited it gets the new text. Pure: the bytes come
// through a CorSource and the card through CoreFs, so tests/coreapp/ runs it on a byte vector and a fake card.
#ifndef COREAPP_INSTFILES_H
#define COREAPP_INSTFILES_H

#include <stdint.h>
#include "cor.h"
#include "ctx.h"
#include "fsif.h"

#define INST_FILE_MAX   (256u * 1024u)      // one file of the record (cor_pack.py FILES_MAX)
#define INST_PATH_MAX   100                 // its path (cor_pack.py FILES_PATH_MAX)

// Writes the files of the record (`rec_off`, `rec_size` in `src`) below `folder`. Returns the number written or replaced, -1 when the folder or a file
// cannot be written (read only card) or the record is damaged (*why, when given, says which); a record of 0 bytes gives 0.
int instfiles_install(CoreFs *fs, CorSource &src, uint32_t rec_off, uint32_t rec_size, const char *folder, const char **why = 0);

// The same for the running core: opens ctx->cor_path, takes record 8 if the bundle is this core's, checks its SHA-256, and installs into the core's folder
// (corefolder.h). Returns the number written, -1 on an error, 0 when there is no bundle or no record. Logs "[core] <id>: N file(s) written to <folder>".
int core_files_install(CoreCtx *c);

#endif
