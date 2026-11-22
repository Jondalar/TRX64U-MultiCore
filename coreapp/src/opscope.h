// Whose code runs (T239.3 S14): an operation or a state getter that a library registered runs on whatever task called it, and the blocks it takes from api->mem
// belong to that library, not to the caller. The registry brackets every handler with enter(owner) and leave(): the loader (libload.cc) makes the library of
// that name the current one for the call and puts the previous one back. An owner that is no loaded image (the core's own handlers) leaves the current one as it is.
#ifndef COREAPP_OPSCOPE_H
#define COREAPP_OPSCOPE_H

class OpScope {
public:
    virtual ~OpScope() { }
    virtual int enter(const char *owner) = 0;       // returns what leave() takes
    virtual void leave(int prev) = 0;
};

#endif
