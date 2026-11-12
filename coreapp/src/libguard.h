// The rescue from a crashing library (T104.1 5.5, Alex 2026-10-18: "a key for the worst case: C=, the Commodore key; a counter makes total
// sense too ... so both"). A library runs native code before the core starts, so one that crashes would take every start down. Two ways out,
// both of which leave the libraries on the stick and load none of them:
//   - the Commodore key is held while the core starts;
//   - the previous start never finished: a boot-attempt counter is set before the libraries load and cleared once the core has started.
// Pure: the file is a CoreFs, the key is a bool the board reads. The counter is one key, `lib_attempt=N`, in the boot.cfg of the profile
// (BoardProfile::boot_cfg): the host's boot menu file on the MultiCore host (BootCfg, host-app/.../bootcfg.h), the file of its own beside
// c64.cfg on the monolith. It is written when its value changes: set 0 -> 1 just before the first library loads, cleared 1 -> 0 after the start, so two
// writes per start at most, none when no library is found or the libraries are skipped and the counter was 0. A DDR2 marker would not do: the usual reaction to a
// hang is a power cycle and DDR2 does not survive one.
#ifndef COREAPP_LIBGUARD_H
#define COREAPP_LIBGUARD_H

#include "fsif.h"

#define LIBGUARD_LIST_MAX 64             // entries listed to prove that boot.cfg is absent

enum LibSkipReason { LIBSKIP_NONE = 0, LIBSKIP_KEY = 1, LIBSKIP_COUNTER = 2 };

struct LibGuardPlan {
    LibSkipReason skip;
    bool armed;                          // the counter was set by arm(): false before arm(), when no library loaded, or when it could not be written
    char why[200];                       // what the screen and the log say: the libraries were skipped and why, or that the guard could not be armed; "" otherwise
};

class LibGuard {
public:
    explicit LibGuard(CoreFs *fs, const char *path) : fs_(fs), path_(path), stored_(0), known_(false) { }

    // Before the libraries: reads the counter and decides; writes nothing.
    void begin(bool commodore_key_held, LibGuardPlan *out);
    // Just before the first library loads (skip == LIBSKIP_NONE): sets the counter to 1. A start without libraries never calls it.
    void arm(LibGuardPlan *out);
    // After the core has started: clears the counter if it is set. False when it could not be cleared (the next start then skips the libraries).
    bool finished();

private:
    CoreFs *fs_;
    const char *path_;
    int stored_;                         // what the file holds
    bool known_;
    bool read(int *v, bool *exists);
    bool write(int v);
};

#endif
