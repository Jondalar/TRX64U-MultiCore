#pragma once
#include "keyboard.h"
class GenericHost { public: virtual ~GenericHost() {} virtual bool is_accessible() { return false; } };
class Keyboard_C64 { public: Keyboard_C64(GenericHost *, volatile uint8_t *, volatile uint8_t *, volatile uint8_t *) {} int getch() { return -1; } int getch_nowait() { return -1; } };
