// Where the loader tells the service layer that a library's poll starts or ends (T239.3 S3, U18 R2). The record a library registers may declare one
// poll (LibPollDecl in libapi.h): an operation without arguments, called every period_ms. The loader owns when the poll begins (the library is
// registered) and when it ends (the library is released or unloaded); the sink owns the tasks. libload.cc has no task of its own, so the same file
// runs in tests/loader/ against the board's LibPolls (service.cc) with no tasks (stack_words 0), whose slots the test steps itself.
#ifndef COREAPP_LIBPOLL_H
#define COREAPP_LIBPOLL_H

#include <stdint.h>

class LibPollSink {
public:
    virtual ~LibPollSink() { }
    // Starts polling `op` every period_ms for `owner` (the name of the library). when_frozen: the poll also runs while the machine is frozen for the menu (the declaration
    // of LibPollDecl, libapi.h). false: refused, `why` says why in a sentence; nothing started.
    // The same owner again replaces its poll.
    virtual bool add(const char *owner, const char *op, uint16_t period_ms, bool when_frozen, char *why, int whylen) = 0;
    // Ends the poll of `owner`; none: nothing happens. The caller holds the core lock, or the machine is frozen (the precondition of the registry's
    // drop_owner, which follows), so no poll of this owner is inside its operation when this returns.
    virtual void drop(const char *owner) = 0;
};

#endif
