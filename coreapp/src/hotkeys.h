// Hotkeys while a core runs (T181): a modifier of one or two keys of the 64-key matrix (or RESTORE) plus an action key. The host drops the action
// keys from the core's key vector while the whole modifier is held (host/app_input.v, registers at U64_AUDIO_MIXER + 0x60); this file is the application's
// side: the map (default from the descriptor, the F8 items change it, the .cfg keeps it), the mask registers, the detection of the press, the USB keyboard's
// filter and the actions. Key index: 0-63 = col * 8 + row of the matrix (as MATRIX_KEYB), 64 = RESTORE, HK_NONE = no key.
#ifndef COREAPP_HOTKEYS_H
#define COREAPP_HOTKEYS_H

#include <stdint.h>
#include "desc.h"
#include "ctx.h"

#define HK_NONE 0xFF
#define HK_RESTORE 64
#define HK_NKEYS 65

enum { HKA_MENU, HKA_VOL_UP, HKA_VOL_DOWN, HKA_RESET, HKA_SWAP, HKA_PICTURE, HKA_CORE, HK_NACT };

struct HkMap {
    uint8_t mod_a, mod_b;                 // the modifier: mod_a HK_NONE = the feature is off; mod_b HK_NONE = mod_a alone
    uint8_t act[HK_NACT];                 // the action keys (HK_NONE: that action has no hotkey)
};

// What a descriptor brings (CoreDesc::hotkeys; null = the core has no hotkeys, no items, the registers stay off).
struct HkSpec {
    HkMap def;                            // the default map, harmless in this core (spec T181 section 5 has the reason per core)
    const char *vol_key;                  // the setting the volume keys step (null: VOLUME_KEY)
    const char *swap_key;                 // the setting swap-joystick toggles between its values 0 and 1 (null: "Joystick ports")
    uint64_t own_keys;                    // keys (bit = index) that are functions of this core while the modifier is held: no default may be one
    // T194 D12: a function of the core's own (the C128's 40/80 picture). core_op null: the core has none, no F8 item, act[HKA_CORE] stays HK_NONE.
    // Otherwise the operation (no arguments) the hotkey calls, the item's key in the .cfg and its label in the menu; def.act[HKA_CORE] is its default key.
    const char *core_op;
    const char *core_key;
    const char *core_label;
};

// The names of the keys, for the F8 items and the tables: hk_key_name(i), i = 0..64.
const char *hk_key_name(int i);

// ---- the registers of the host (spec section 5) ---------------------------------------------------------------------------------------
#define HK_REG_BASE 0x60
#define HK_NREGS 11
// The 11 bytes the host takes for a map: MOD_A, MOD_B, mask bytes 0-7, RESTORE-is-action. An action key equal to a modifier key is dropped
// (the modifier must pass); a map with mod_a HK_NONE gives all zeros (off).
void hk_regs(const HkMap &m, uint8_t out[HK_NREGS]);

// ---- the settings (F8) ------------------------------------------------------------------------------------------------------------------
#define HK_OP "hotkeys.apply"
#define HK_KEY_MOD "Hotkey modifier"
// out = *d with the 7 items after d's own settings (modifier, then one key per action), the defaults taken from d->hotkeys; `store` as volume_extend's.
// A descriptor without hotkeys is copied as it is. Returns null, or the reason when there is no room.
const char *hk_extend(const CoreDesc *d, CoreDesc *out, Setting *store);
bool hk_is_generic(const Setting &s);
int hk_modifier_presets();
// The F8 item's index of a map's modifier, -1: the pair is not one of the presets (the item could not show it).
int hk_preset_index(const HkMap &m);
const char *hk_modifier_name(int i);
// The map the settings hold now (c->settings and c->desc), the descriptor's default when the items are not there.
HkMap hk_map_of(const CoreCtx *c);
void hk_register(OpRegistry &r);
// Writes the map's registers (CoreIo::hotkey_regs) and tells the detection and the USB filter; at the core's start and on every change of the map.
void hk_apply(CoreCtx *c);
// The map hk_apply made last (what the registers hold).
const HkMap &hk_current();

// ---- detection --------------------------------------------------------------------------------------------------------------------------
#define HK_POLL_MS 10
#define HK_REPEAT_FIRST_MS 400
#define HK_REPEAT_MS 120

struct HkKeys {
    uint64_t k;                           // bit = matrix index, set = down
    bool restore;
    bool down(int i) const { return i == HK_RESTORE ? restore : (i >= 0 && i < 64 && ((k >> i) & 1)); }
    HkKeys() : k(0), restore(false) { }
};

bool hk_modifier_held(const HkMap &m, const HkKeys &keys);

// One poll: returns the actions (bit HKA_x) to perform now. An action fires when its key goes down while the whole modifier is held, or the modifier
// completes while the key is down (the host hides the key from the core in both cases); holding does not repeat, except the volume keys.
class HkDetect {
public:
    HkDetect() { reset(); }
    void reset();
    uint32_t poll(const HkMap &m, const HkKeys &keys, uint32_t now_ms);
private:
    uint32_t active_;                     // the actions whose key is down with the modifier held, last poll
    uint32_t next_rep_[2];                // the volume keys' next repeat time
};

// ---- the USB keyboard: the combination is taken out before the core sees it ---------------------------------------------------------------
// The matrix a USB keyboard makes (Keyboard_USB::applyMatrixState; m[0..7] one byte per column, bit = row, 1 = down) with the same rule as the host: the
// action keys are removed while the whole modifier is held, and stay removed until they are released. The raw state is kept for the detection.
class HkFilter {
public:
    HkFilter() { reset(); }
    void reset() { hidden_ = 0; hidden_restore_ = false; }
    void apply(const HkMap &m, uint8_t mtx[8], uint8_t *restore);
private:
    uint64_t hidden_;
    bool hidden_restore_;
};
// The application's shared state for the USB task (the map, the raw USB keys) and the filter the keyboard driver calls (keyboard_usb.cc hook).
void hk_usb_matrix_filter(uint8_t mtx[8], uint8_t *restore);
HkKeys hk_usb_keys();
void hk_usb_set_map(const HkMap &m);
// A USB report (boot protocol, 8 bytes) with the usages of the action keys removed while the whole modifier is held: for a core that takes the USB
// keyboard as raw key events (rawkeys.h). The usage map (hk_usb_set_usage_map) says which matrix key a HID usage makes (HK_NONE: none).
void hk_usb_report_filter(const uint8_t in[8], uint8_t out[8]);
void hk_usb_set_usage_map(uint8_t (*usage_matrix)(uint8_t usage));

// ---- actions ----------------------------------------------------------------------------------------------------------------------------
// Volume: one step of the core's volume setting (hk volume key, clamped); swap: the joystick setting between its values 0 and 1; picture: the next
// Picture value (wraps). The text is what the screen says; false: the core has no such setting (nothing happens, text says so).
bool hk_step_volume(CoreCtx *c, int dir, char *line, int n);
bool hk_swap_joystick(CoreCtx *c, char *line, int n);
bool hk_next_picture(CoreCtx *c, char *line, int n);
bool hk_reset(CoreCtx *c, char *line, int n);
bool hk_core_action(CoreCtx *c, char *line, int n);      // the descriptor's own function (HkSpec::core_op); false: the core has none

// The loop's side (run.cc, the bench): `poll` reads the physical keys (CoreIo::keys_read) and the USB keyboard's and says which actions fired, at most once per HK_POLL_MS;
// `perform` does the actions but the menu with the operations the menu uses, and gives the line for the console (true: tell it; D4: nothing is drawn over the running core).
struct HkLoop {
    HkDetect det;
    uint32_t last_ms;
    HkLoop() : last_ms(0) { }
    uint32_t poll(CoreCtx *c);
    bool perform(CoreCtx *c, uint32_t fired, char *line, int n);
};

#endif
