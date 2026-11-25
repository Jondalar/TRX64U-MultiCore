// copy of coreapp/cores/hk_defaults.h at commit 7f2bec40c (T239.5: a file the other cores still use is copied per core, not linked)
// The default hotkey maps of the cores (T181 D3, spec section 5 has the reason per core). Key indices as hotkeys.h: 0-63 = col * 8 + row, 64 = RESTORE.
#ifndef COREAPP_HK_DEFAULTS_H
#define COREAPP_HK_DEFAULTS_H

#include "hotkeys.h"

// matrix indices used below
#define HKK_CTRL 58
#define HKK_CBM 61
#define HKK_STOP 63
#define HKK_R 17
#define HKK_J 34
#define HKK_P 41
#define HKK_PLUS 40
#define HKK_MINUS 43

// The digit keys: CTRL + 1..8 are the colours and CTRL + 9 / 0 reverse on / off in the C64-type cores.
#define HKO_DIGITS ((1ull << 56) | (1ull << 59) | (1ull << 8) | (1ull << 11) | (1ull << 16) | (1ull << 19) | (1ull << 24) | (1ull << 27) | (1ull << 32) | (1ull << 35))

// CTRL + RESTORE menu, + R reset, + "+" / "-" volume, + J swap joystick ports, + P picture mode (Alex 2026-11-04 for the first four).
static const HkSpec hk_cbm = {
    { HKK_CTRL, HK_NONE, { HK_RESTORE, HKK_PLUS, HKK_MINUS, HKK_R, HKK_J, HKK_P, HK_NONE } }, 0, 0, HKO_DIGITS };
// The C16 / Plus/4: the joystick swap is a reset-time item.
static const HkSpec hk_c264 = {
    { HKK_CTRL, HK_NONE, { HK_RESTORE, HKK_PLUS, HKK_MINUS, HKK_R, HKK_J, HKK_P, HK_NONE } }, 0, "Joystick ports (at next reset)", HKO_DIGITS };
// Keyboard computers where CTRL is a typing key (Apple II, Atari, ZX, CPC): the modifier is C= + CTRL, which none of them has a function for.
static const HkSpec hk_typing = {
    { HKK_CBM, HKK_CTRL, { HK_RESTORE, HKK_PLUS, HKK_MINUS, HKK_R, HKK_J, HKK_P, HK_NONE } }, 0, 0, 0, 0, 0, 0, true };
// The Amiga: CTRL + Amiga + Amiga is its reset, so the modifier is C= + RUN/STOP.
static const HkSpec hk_amiga = {
    { HKK_CBM, HKK_STOP, { HK_RESTORE, HKK_PLUS, HKK_MINUS, HKK_R, HKK_J, HKK_P, HK_NONE } }, 0, 0, 0, 0, 0, 0, true };
// The consoles take no key of the keyboard: the C64's map does not collide with anything.
static const HkSpec hk_console = {
    { HKK_CTRL, HK_NONE, { HK_RESTORE, HKK_PLUS, HKK_MINUS, HKK_R, HKK_J, HKK_P, HK_NONE } }, 0, 0, 0, 0, 0, 0, true };

#endif
