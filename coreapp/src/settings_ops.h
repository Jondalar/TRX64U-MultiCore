// The settings as operations of the base (T104.1 D3: one service layer): the menu changes a setting by calling `settings.set`, a REST or WS client
// calls the same operation by the same name. What it does is what the menu's row always did - take the value into the .cfg at once, apply a live
// one - written once here instead of once per caller.
#ifndef COREAPP_SETTINGS_OPS_H
#define COREAPP_SETTINGS_OPS_H

#include "ctx.h"

// settings.set(key, value): key is a setting's text (case-insensitive), value one of its labels (case-insensitive).
// settings.get(key): the label now in force, in the message.
// settings.reset_to_host(): removes the core layer (the .cfg), applies the live items again.
void settings_register(OpRegistry &r);

// The menu's call of settings.set (a switch, a choice, a text): the same operation a REST client calls. 0 or an OpErr; r->msg says how it went.
int settings_set(CoreCtx *c, const char *key, const char *value, OpResult *r);

#endif
