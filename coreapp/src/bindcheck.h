// The check of one call of an operation by name (T104.1 D14 item 2): the operation is registered and the bindings fill every argument with a value of
// its type. desc_check.cc runs it over the descriptor's entries, contrib.cc over the nodes and file actions that libraries and applications
// contribute; a file of its own so the menu code links it without the whole descriptor check.
#ifndef COREAPP_BINDCHECK_H
#define COREAPP_BINDCHECK_H

#include "ops.h"

// null when the call is sound, else a sentence (valid until the next call). `what` ("tasks", "nodes") and `label` name the entry. `file_ok`: the
// entry has a file for $file; `ask_ok`: it asks for an address (B_ASK); `text_ask_ok`: it asks for a name (B_TEXT).
const char *bind_check(const OpRegistry &reg, const char *what, const char *label, const char *op, const OpBind *b, int nb, bool file_ok,
                       bool ask_ok = false, bool text_ask_ok = false);

#endif
