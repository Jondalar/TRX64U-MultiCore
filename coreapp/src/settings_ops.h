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

// The text of a .cfg for settings.load_staged (the command interface's CONTROL LOAD_CONFIG reads the file itself and has the operation apply it on the executor, with the
// firmware's log: Line n: ...). settings_stage copies at most 16 KB and returns the length kept; settings_load_log is the log of the last run.
int settings_stage(const char *text, int len);
const char *settings_load_log(void);

// The menu's call of settings.set (a switch, a choice, a text): the same operation a REST client calls. 0 or an OpErr; r->msg says how it went.
int settings_set(CoreCtx *c, const char *key, const char *value, OpResult *r);

#endif
