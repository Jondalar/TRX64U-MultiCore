// The start waits for the medium of the hand-over path (T148 D13). Pure apart from the file system and the clock it is given.
#ifndef COREAPP_MEDIUMWAIT_H
#define COREAPP_MEDIUMWAIT_H

#include "ctx.h"

// T148 D13: the core's own folder lives on the medium the hand-over path names. A stick is not up yet when the application starts (the USB task
// has not found the hub; after an IPROG the stack enumerates again), so the steps that read or write the .cor's place wait for that medium
// first - the same way the ROM step waits, but with a limit. The root of a path is its first component ("/Usb0/cores/a.cor" -> "/Usb0").
#define START_MEDIUM_WAIT_MS 10000
class MediumEnv {
public:
    virtual ~MediumEnv() { }
    virtual uint32_t now_ms() = 0;
    virtual void delay_ms(uint32_t ms) = 0;
};
struct MediumWait {
    bool present;                // the root lists (or needs no waiting: /SD, /flash)
    bool waited;
    uint32_t ms;
};
// Waits until the root of `path` lists, at most limit_ms, only for a USB root ("/UsbN", any case). A missing medium after the limit is not
// an error here: the step that follows says what it cannot do, in its own words.
void medium_wait(CoreFs *fs, const char *path, MediumEnv *env, uint32_t limit_ms, MediumWait *out);
// The start's use of the hand-over path (run.cc): waits for the medium, then makes the core folder and moves an old .cfg in.
// The bundle read and the library scan run after it and find the medium up.
void start_core_folder(CoreCtx *c, MediumEnv *env);

#endif
