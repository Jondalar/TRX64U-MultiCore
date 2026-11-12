// The USB keyboard as raw key events for a core that has its own keyboard protocol (T141). The USB driver (usb_hid.cc, one hook per combined
// keyboard report) calls rawkeys_usb_report() on the USB task; the core's poll calls rawkeys_pace(), which turns the held keys into press and
// release events of the machine's own key codes and writes them to the core's RAWKEY register, never more than that register has room for.
//   - the report's modifier byte and its six key slots are the keys down; a code that comes up is released once (bit 7), a code that goes
//     down is pressed once, modifiers first; two USB keys with one machine code (the two CTRL keys) are one key.
//   - no typematic: the machine repeats keys itself.
//   - the menu key SCROLL LOCK opens the core menu and is never sent; F10 is the machine's key here (T141 D5). While the menu is open nothing is sent (the machine is
//     held and no poll runs): rawkeys_menu(true) releases what is down and drops the reports; rawkeys_menu(false) takes the keys held at that
//     moment as a baseline - they are ignored until they have been released, so a key held across the menu is not pressed late.
//   - the lock key (CAPS LOCK) is a toggle on the machine: a press on USB flips it, the machine gets a press (on) or a release (off).
// A core with a `held` callback gets the set of usages down instead of events (above, RawKeySpec).
// A core without a rawkeys record gets nothing: the report is dropped and the keyboard works as before (menus only, or the C64 matrix).
#ifndef COREAPP_RAWKEYS_H
#define COREAPP_RAWKEYS_H

#include "ctx.h"

struct RawKeySpec {
    uint16_t reg_key;                     // RAWKEY: a write queues {release, code}; a read says in b0 that 8 more events fit
    const uint8_t *hid;                   // the machine's code for each HID usage 0 .. nhid-1, 0xFF none
    uint8_t nhid;
    const uint8_t *mod;                   // ... for each bit of the report's modifier byte
    uint8_t caps_usage, caps_code;        // the lock key (usage 0xFF: the core has none)
    // Held-keys mode (T141 P2, the C128): the core has no key queue but registers that hold the keys down. With `held` set, reg_key / hid / mod
    // are not used: the application calls held() with the HID usages down (modifier bits as 0xE0 + bit) whenever that set changes, and with
    // none when the menu opens. The menu key is not in the set, F10 stays a menu key (rawkeys_active() is false).
    void (*held)(CoreCtx *c, const uint8_t *usages, int n);
};

// The core's record: made known once, when the engines are set up for the core. Null: no raw keys.
void rawkeys_configure(const RawKeySpec *spec);
// Forgets what is down, queued and ignored (the machine was held or reloaded: it has no key down). The reports not yet taken stay.
void rawkeys_reset();
// The USB driver's hook: the combined boot-protocol report (modifiers, reserved, six usages). USB task. A copy goes into a ring; never blocks.
void rawkeys_usb_report(const uint8_t report[8]);
// The core menu opens (true: release what is down, drop reports and events) or closes (false: the keys held now are the baseline).
void rawkeys_menu(CoreCtx *c, bool open);
// The poll's side: take the reports, queue their events, write as many as the register has room for. Returns true when it wrote.
bool rawkeys_pace(CoreCtx *c);
// Is this HID usage one the application takes for its own (the menu key)?
bool rawkeys_is_menu_key(uint8_t usage);
// Does the running core take the USB keyboard as raw keys? Then F10 is the machine's, not a menu key (T141 D5).
bool rawkeys_active();

#endif
