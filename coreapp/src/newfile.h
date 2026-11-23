// Where a new file goes and under what name (T122 D2). First written for the 1541's Create and Save (T104.1 D15, app/lib/cbm1541), taken out
// of the drive class so every core's "Create blank ..." places its image the same way. Pure: CoreFs only.
//
// The rule, in one place: a new file is never written over one that is there. `path` is empty or a folder (a free name `auto_stem`, `auto_stem2`
// ... there, or on the first medium that takes it), a bare name (that name with the extension, in the preferred folder), or a full path
// (exactly that file).
#ifndef COREAPP_NEWFILE_H
#define COREAPP_NEWFILE_H

#include <stdint.h>
#include "fsif.h"

// nf_base and nf_copy are in textcopy.h: generic code needs them and does not link this library
#include "textcopy.h"

// The folders a new file may go to, in this order: `hint` (the one asked for), else `pref` (the menu's folder, the folder of the last disk),
// then the media of the browser. `out` takes six. Returns the count.
int nf_folders(const char *pref, const char *hint, const char **out);

// A free name for a new file without its extension: `stem`, `stem2` ... in the first folder the browser can read. false: none was found and
// `out` holds the stem. The menu starts its name prompt with it.
bool nf_suggest(CoreFs *fs, const char *pref, const char *stem, const char *ext, char *out, int outlen);

// Writes `buf` as a new file. 0 and the path in `full`, or 1 and a sentence in `msg`.
int nf_write_new(CoreFs *fs, const char *pref, const char *path, const char *auto_stem, const char *ext, const uint8_t *buf, uint32_t n,
                 char *full, int fl, char *msg, int ml);

// ---- the same, in chunks (T122 §5) -----------------------------------------------------------------------------------------------------
// A long image is not written by one call that blocks the menu and holds the core lock for as long as the medium takes: the first NF_CHUNK
// bytes create the file (the same placement rule as nf_write_new), every step() writes the next chunk and the last one syncs and closes. The
// caller keeps `buf` alive until the writer is done or aborted. A failed step removes the partial file.
enum { NF_DONE = 0, NF_FAIL = 1, NF_MORE = 2 };
#define NF_CHUNK 16384u

class NfWriter {
public:
    NfWriter();
    ~NfWriter();
    bool active() const { return f_ != 0; }
    int begin(CoreFs *fs, const char *pref, const char *path, const char *auto_stem, const char *ext, const uint8_t *buf, uint32_t n, char *msg,
              int ml);
    int step(char *msg, int ml);
    void abort();                                    // closes and removes what was written
    const char *full() const { return full_; }       // the path of the file, once begin() placed it
    int percent() const;

private:
    int fail(const char *what, char *msg, int ml);
    CoreFs *fs_;
    CoreFile *f_;
    const uint8_t *buf_;
    uint32_t n_, done_;
    char full_[260];
};

#endif
