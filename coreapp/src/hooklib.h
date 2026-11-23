// The core's code as a library of its bundle (T239.3 base 3.3, core_hooks.h): finds the named library in the loader, takes the table it lists as the export "corehooks" at major
// 1 and attaches it to hookhost.h with the library's account as the scope of every call into it. Separate from hookhost.cc because it needs the loader.
#ifndef COREAPP_HOOKLIB_H
#define COREAPP_HOOKLIB_H

#include "libload.h"

// `name` is CoreDesc::hooks_lib. 0: attached. -1: nothing attached and `why` says why (no such library loaded, or it does not list the export corehooks at exact major 1).
int hooks_attach_lib(LibLoader *ld, const char *name, char *why, int whylen);

#endif
