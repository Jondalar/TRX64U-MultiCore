// What the core's bundle (.cor) carries beyond the partial bitstream and the application (T104.1 D9, D12): the menu description (record 6) and the
// libraries (record 7), and the merge of everything that is contributed into one menu (contrib.h). Pure: the file is read through CoreFs, so
// tests/coreapp/ runs it on a map of byte vectors.
//
// An old bundle has neither record and gives the menu it always gave: the descriptor compiled into the application, the default route.
// A record that is damaged is refused by name and the core starts without it; it never stops the start (the bundle's own hash is what the
// host checked before it handed over).
#ifndef COREAPP_BUNDLE_H
#define COREAPP_BUNDLE_H

#include "ctx.h"
#include "descrec.h"
#include "libload.h"
#include "libscan.h"
#include "cor.h"
#include "appindex.h"

struct BundleExtras {
    bool probed;                          // the bundle was opened and its layout read
    CorInfo ci;                           // valid when probed (also the offsets of the library record)
    DescRecord *desc;                     // the decoded description, null: none (or refused: see why)
    uint8_t *desc_bytes;                  // the record, which desc points into
    char why[240];                        // what was refused, as a sentence; "" when nothing was
};

// Reads the bundle ctx->cor_path names. A path that is no .cor, a bundle that is not this core's or one the host contract does not accept leaves
// everything null with the reason in why. The description is read, hashed and parsed here; the libraries are only located (lib_load_cor loads them).
void bundle_read(CoreCtx *c, BundleExtras *out);

// The route and contributions of a description, the libraries the loader holds, laid on the descriptor `base`: `out` is the descriptor the menu,
// the settings and the browser use from here on, `mm` what the menu tree needs besides. Names every refusal on the console through `log`.
// `apps` (T104.3 D6): the applications of the core's folder, each a contributor of rank CR_APP with the half of its record that is for the core's menu.
// Returns the number of contributions refused.
int bundle_merge(CoreCtx *c, const CoreDesc *base, const BundleExtras *x, LibLoader *loader, LibLog *log, CoreDesc *out, MenuMerge *mm,
                 const AppIndex *apps = 0);

#endif
