// Whose code runs (T239.3 S14): an operation or a state getter that a library registered runs on whatever task called it, and the blocks it takes from api->mem
// belong to that library, not to the caller. The registry brackets every handler of a loaded image with enter(slot) and leave(): the loader (libload.cc) makes
// the image in that slot the owner for the calling task until leave(). The scope is the task's own: another task keeps the owner it has, so a handler on one task
// neither takes the allocations of an application on another nor puts back a value another task has changed since. The core's own handlers (no slot) are
// not bracketed at all.
#ifndef COREAPP_OPSCOPE_H
#define COREAPP_OPSCOPE_H

class OpScope {
public:
    virtual ~OpScope() { }
    virtual int enter(int slot) = 0;                // returns what leave() takes
    virtual void leave(int prev) = 0;
};

#endif
