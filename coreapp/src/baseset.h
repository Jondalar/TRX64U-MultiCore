// The settings of the generic application (T239.3 base 3.3, svc_settings.h): the menu's look, the keyboard as a joystick and the USB mouse. The application declares
// them, as it does Picture and Volume, applies them itself and lists them on the APP page (F7) of every core; a core's menu or descriptor declares none of them (desc_check
// refuses a descriptor that does; the one exception is the Amiga cores' own Mouse Sensitivity / Mouse Acceleration with the operations mouse.sens / mouse.accel, T239.3 D25: baseset_extend
// does not add a key the descriptor declares, so the Amiga keeps its rows on F5 and has no second pair on F7). The keys are the stored keys of every .cfg, so the values a core's own menu used to hold carry over. A library reads them by key
// (settings.value) and hears of a change through the core hook base_setting_changed (core_hooks.h, hookhost.h).
//
//   look       Overlay Transparency, Color Scheme, Filename overflow squeeze, Navigation Style
//   input      Joystick Input (the host's keyboard driver takes the port and the WASD mapping from it), and for a core whose descriptor has a mouse (CoreDesc::mouse)
//              Mouse Sensitivity and Mouse Acceleration (the USB driver scales every report with them before it hands it to the core's mouse sink, mousesink.cc)
//
// The application lists a row only where the code can do what it says (T228 R20): a key the core's own settings declare is not added, a key the core's menu description hides (menu.toml `hide`, T228 R7: Color Scheme on
// trx64u) is dropped again when the menu is merged (contrib_merge); a mouse row needs CoreDesc::mouse; and two rows need a capability the core names in CoreDesc::caps.
//   BASE_CAP_KEYBOARD_JOYSTICK  Joystick Input: the bitstream turns the keyboard's WASD keys into joystick lines (MATRIX_WASD_TO_JOY) for the machine. No core of this tree has it:
//                               host/ultimate_sys.vhd decodes page 1 offsets 0 to 10 only, so the store to offset 11 reaches no logic, no layer2 module reads the register, and
//                               U64II_KEYB_JOY only picks the port the application's own scan reads for the menu.
//   BASE_CAP_MOUSE_DRIVER       Mouse Mode, Mouse Wheel Sensitivity, Mouse Wheel Direction: the USB driver's own handling of a report (cursor keys, wheel, port 1). run.cc registers
//                               the mouse sink for every core and mouse_sink_usb_report consumes each report, so that handling never runs and no core has the capability.
#ifndef COREAPP_BASESET_H
#define COREAPP_BASESET_H

#include "desc.h"
#include "ctx.h"

// out = *d with the settings the core has not got after d's own; `store` holds the merged array and has to outlive `out` (as volume_extend's). Null, or the reason when
// there is no room. A key the descriptor declares itself is not added again.
const char *baseset_extend(const CoreDesc *d, CoreDesc *out, Setting *store);

#define BASE_CAP_KEYBOARD_JOYSTICK "kbd_joystick"
#define BASE_CAP_MOUSE_DRIVER "mouse_driver"

// Does baseset_extend add the setting `key` to `d`'s descriptor (a base key that the core does not declare, whose capability it names and, for a mouse row, that has a mouse)? The hotkeys ask it for the swap item before the base's rows are in the descriptor (hotkeys.cc).
bool baseset_adds(const CoreDesc *d, const char *key);

// What the board gives the settings (the menu's own look, the host keyboard's registers and the product). Null members do nothing; the board sets all of them (run.cc).
struct BaseSetPort {
    void (*look)(int transparency, int scheme);               // "Overlay Transparency" position (0 .. 3 = 0, 25, 50, 75 %) and "Color Scheme" position (0 .. 6)
    void (*squeeze)(int quarter);                             // "Filename overflow squeeze": 0 None, 1 Beginning, 2 Middle, 3 End
    bool (*commodore)(void);                                  // the board sold under Commodore's name: its first-run Navigation Style is WASD Cursors
    void (*keyboard_joystick)(uint8_t port, uint8_t wasd);    // "Joystick Input": the port the keyboard's joystick line reads (0: 2, 1: 1) and the WASD mapping register
};
// The context the USB driver's answers (baseset_usb_config) are read from, and the port. Called once at the start of a run (and by every test).
void baseset_attach(CoreCtx *c, const BaseSetPort *port);

// 0 for a setting that is not the application's, 1 for the look group, 2 for the input group (menutree.cc routes both to F7).
int baseset_kind(const Setting &s);
static inline bool baseset_is_generic(const Setting &s) { return baseset_kind(s) != 0; }

// The ten keys in the order of svc_settings.h (the order of the hook calls after the start).
int baseset_count(void);
const char *baseset_key(int i);

// The USB driver's configuration item `key` (usb_hid_config.h: 0x55 mode, 0x56 wheel factor, 0x57 wheel direction, 0x58 menu navigation by mouse, 0x5D sensitivity, 0x5E
// acceleration) answered from the settings of the attached context; `default_value` for anything else or while there is no context (mousesink.cc defines
// u64_get_usb_hid_config_value over this).
int baseset_usb_config(int key, int default_value);

void baseset_register(OpRegistry &r);

#endif
