// Base 3.3, group `settings` (T239.3 section 5; boundary rows 4.1, 11.2), the states `set.<key>` and `net.gated`, and the settings of the generic application.
//
// The settings of the running core, by key, as the Settings object of the core application holds them (settings.h): three layers (the descriptor's default,
// the host's flash, the core's .cfg) and one place to write. The group belongs to the generic application by nature. It joins LibApi as
// `const SvcSettings *settings`.
//
// Index rule: `find` gives an index that is valid while `generation` is unchanged. The core application makes a new Settings object when it merges what the
// libraries contribute (run.cc) and when a config is loaded (rebind), and every such change counts `generation` up. A library that keeps indexes
//  compares the generation before it uses one and finds the keys again when it changed. The base keeps the lookup cache; keys are compared by content.
//
// Contracts of the whole group:
//   - No call takes a CoreCtx.
//   - Task: `generation`, `find`, `value`, `text`, `layer`, `info`, `cfg_value`, `cfg_text`, `extra_get` read and may be called from any task (a read
//     races with a write of the same item only as a plain int read does; `text` is the exception, see there). `set`, `override_value`, `apply` and
//     `extra_set` change the core's state and are called by a task that holds the core lock: a handler, a core hook, a poll. `apply` runs the setting's
//     operation at once on the calling task, as api->ops->call does; from a task without the lock (a job) it is not allowed.
//   - Errors: a bad index gives -1 (int result), false (bool result) or "" (text result); nothing asserts.
#ifndef COREAPP_SVC_SETTINGS_H
#define COREAPP_SVC_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvcSettings {
    uint32_t size;
    // Counts up whenever the set of settings or its indexes changed (a new Settings object, a rebind to another config). Equal generation, equal indexes.
    uint32_t (*generation)(void);
    // The index of the setting named `key`, case insensitive (Settings::find), -1 when the core has none. Task: any.
    int (*find)(const char *key);
    // The value in force: for a list the position of its label, for a range setting the position too (the number the menu shows is base + position: `info`).
    // The override of override_value wins over the stored value. -1 for a bad index. A text setting has no value index (use `text`).
    int (*value)(int ix);
    // The text in force of a text setting (kind 1, 2, 3): the core layer, else the default, leading blanks kept. The pointer is owned by the base and is
    // valid until that setting is written again or `generation` changes: copy it at once. "" for a setting that is no text or a bad index.
    const char *(*text)(int ix);
    // Where the value comes from: 0 the descriptor's default, 1 the host's flash, 2 the core's .cfg (Settings::layer). -1 for a bad index.
    int (*layer)(int ix);
    // Stores `value` (a list position) in the core layer and marks the setting changed (Settings::set). The .cfg is written at once only with Auto Save Config on; with
    // it off the value is held and the file waits for the next save, and the call still answers true (extra_set says the same). It does NOT call the setting's apply
    // operation: `apply` does (the old settings_store). Result: false when the index or value is bad (nothing changes), or when the core has no bundle
    // path or the .cfg could not be written (the value holds in memory all the same). Ends an override of the same item. Task: lock holder.
    bool (*set)(int ix, int value);
    // A value for this run only, never stored and never written into any .cfg or exported config (Settings::override_value; CTRL+P / CTRL+N at power on, the
    // System Mode of a loaded .c64re). value() answers it from now on. A bad index or value does nothing, and so does a text setting. It does not apply: call `apply` after it.
    // Task: lock holder.
    void (*override_value)(int ix, int value);
    // Puts the value in force into the core: the setting's apply operation with that value, or its register commit (regset_apply_setting). Task: lock holder.
    // Result: none; a failure of the operation is logged by the base.
    void (*apply)(int ix);
    // The number of labels (or of range steps) and the base: the menu shows base + position, and the apply operation of a range setting receives it
    // (Setting::nvalues, Setting::base). Result: false for a bad index; *nvalues and *base are then not written. Task: any.
    bool (*info)(int ix, int *nvalues, int *base);
    // A key of the core's own that is no choice from a list and has the prefix "Pad " (the recorded pad maps); the settings file keeps
    // it beside the items. At most 4 keys of at most 23 characters with values of at most 79. extra_get: false when the key is not there (val is not written);
    // a value longer than n is cut to n - 1 characters. extra_set: false for a key without the prefix, a value or key that is too long, no room, or a file that
    // could not be written (the value holds in memory); with Auto Save Config off the file waits for the next save. Task: any / lock holder.
    bool (*extra_get)(const char *key, char *val, int n);
    bool (*extra_set)(const char *key, const char *val);
    // The shorthand of the base's own cfg_value and cfg_text (they move into the core application): the value of the setting named `key`, `def`
    // when the core has no such setting (a list position, as `value`); and the text of a text setting with its leading blanks skipped, never null: "" when the
    // key is no text setting. An enum gives "" from cfg_text; read the label with the state `set.<key>`. The key is matched by content (strcasecmp), not by the
    // address of the string literal. cfg_text's pointer lives as long as `text`'s. Task: any.
    int (*cfg_value)(const char *key, int def);
    const char *(*cfg_text)(const char *key);
} SvcSettings;

// ---- states of the registry that a library reads with api->ops->state (SvcOps::state) ---------------------------------------------------------------------
// "set.<key>": the value in force of the setting <key>, as text. A space and an underscore in the key compare equal and the case is folded (the key folding of
// the base's setting_state). An enum or list setting answers the label of its value, a text setting its text with leading blanks kept. The answer is null when
// the core has no such setting. This is the branch LibLoader::svc_state gets in 3.3 (boundary 2.3); the monitor's plat_config reads "set.<item>" through it.
//
// "net.gated": "yes" while the REST token gate is active (a request without the token is refused), else null. It is a state, not a member of SvcNet.

// ---- settings of the generic application (the base owns them as it owns Picture and Volume; any core's menu can show them) -------------------------------
// A library reads them like any setting, by key. It does not apply them: the base does. The key is the stored key of every core's .cfg and the stored values carry over
// when a core's menu stops declaring its own copy. Values are listed by position; the first is position 0 and the default is the position named.
//   "Overlay Transparency"            the menu's transparency (replaces ui_set_look): "0 %", "25 %", "50 %", "75 %"; default 1.
//   "Color Scheme"                    the menu's colours (replaces ui_set_look): "Standard Blue", "Ultimate Black", "C128 Style", "Commodore Blue", "Commodore 1", "Commodore 2",
//                                     "Commodore 3"; default 1.
//   "Filename overflow squeeze"       where a long name is shortened (replaces ui_set_name_squeeze): "None", "Beginning", "Middle", "End"; default 0.
//   "Navigation Style"                how the menu reads letter keys: "Quick Search", "WASD Cursors" (W, A, S, D move the cursor); default 0, and 1 on the
//                                     Commodore product (board.is_commodore) while the person has stored no value.
//   "Joystick Input"                  the keyboard as a joystick: "Normal", "Swapped", "WASD Port 2", "WASD Port 1", "WASD P2 No KB", "WASD P1 No KB"; default 0.
//                                     The base's keyboard driver takes from it which port the keyboard joystick drives and whether W, A, S, D act as its
//                                     directions (and still type); hardware of the core that follows the position is set by the core's library, which reads
//                                     the setting by key.
// The USB mouse, for every core whose descriptor has a mouse (CoreDesc::mouse). The base's mouse sink reads them by key (the USB driver's config callback lives in the base image,
// mousesink.cc), so no library defines it and no key of a core's menu is in generic code:
//   "Mouse Mode"                      "Cursor keys", "Wheel: cursor keys", "Wheel: joystick"; default 0.
//   "Mouse Sensitivity"               range 1 .. 16, default 8 (position 7).
//   "Mouse Acceleration"              "Off", "Adaptive"; default 0.
//   "Mouse Wheel Sensitivity"         range 1 .. 16, default 8 (position 7).
//   "Mouse Wheel Direction"           "Normal", "Reversed"; default 0.
// The base declares the five for every core with a mouse. Two cores declare them today, one in its menu TOML (all five) and the Amiga in cores/amiga.cc
// (Mouse Sensitivity and Mouse Acceleration, with the same keys and values); both drop their declarations when 3.3 is built.

#ifdef __cplusplus
}
#endif

#endif
