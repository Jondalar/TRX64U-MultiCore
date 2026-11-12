// What a descriptor must have (T93 D3, S11). The descriptors are C++ constants, so the check is C++ too: tests/coreapp/ runs it over
// the real table and over a made-up core; a shared source file never lists the cores.
#ifndef COREAPP_DESC_CHECK_H
#define COREAPP_DESC_CHECK_H

#include "desc.h"

// null when the descriptor is complete, else the first thing wrong, as a sentence naming the field.
// Every operation and state the descriptor names has to be registered - by the base or by the core's own contributor - and every binding
// has to fit the arguments it fills; otherwise the sentence names the entry and the operation.
const char *desc_check(const CoreDesc *d);
// The same against a registry the caller filled (a library's contributions, a bench).
const char *desc_check_with(const CoreDesc *d, const OpRegistry &reg);

// null when no two cores of the table share an ID block or a manifest name.
const char *desc_check_table(const CoreDesc *const *table);

#endif
