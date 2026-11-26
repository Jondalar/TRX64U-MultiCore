// The diag core's panels (T260 P5, D5): a panel is a TOML file on the card (cores/diag/sd/panels/*.toml) whose rows are bound to core registers - the value of a row is
// what the register holds, LEFT / RIGHT step it, a value may have names, presets load and save the rows as TOML. The T259 A/V page is the first panel (panels/avout.toml).
// Pure: the registers are a CoreIo, the card a CoreFs, the person a CoreDialogs; tests/coreapp/test_diag_avtrace.cc and test_panel.cc run all of it on a model.
//
//   kind = "panel"
//   [panel]    name = "...", presets = "presets" (a folder of the core's folder), preset_kind = "avpreset" (the line `kind = ` of a preset file)
//   [[row]]    key = "Burst end"            the label of the row and its key in a preset file
//              reg = "av.bend" | 0x56       a register of the core's TOML A as "block.property" or "block.property.field" (it fixes the bytes, shift and mask), or an offset
//              bytes = 1 | 2, shift, mask   for an offset (or to widen a property): the width in bytes (little endian), the first bit and the mask of the bits
//              signed = true                the value is signed within its bits
//              min, max, step               the range LEFT / RIGHT and a free value stay in (defaults: the bits' range), the step of a row without names
//              names = ["A", "B"], values = [0, 5]   the value names (values default to 0, 1, ...); a register value that has no name shows as "#n"
//              default = "A" | 3            the value Defaults puts back
//              system = true                a write also hands the value to the picture layer as the System Mode (picture_set_system)
//              video_format = [0x00, 0x2B]  a write also gives CoreIo::video_format this byte, the entry of the new value
//   [[row]]    key = "Load preset", action = "load" | "save" | "defaults"       an action row: RETURN does it
#ifndef DIAG_PANEL_H
#define DIAG_PANEL_H

#include <stdint.h>
#include "ctx.h"
#include "coredlg.h"

#define PNL_ROWS   32
#define PNL_NAMES  256
#define PNL_KEY    24
#define PNL_VFMT   32

struct PanelReg {
    uint16_t ofs;
    uint8_t bytes, shift;
    uint32_t mask;                     // of the value after the shift is applied to the register's bits: the bits in place, so 0xFF for a whole byte
    bool sgn;
};
// A register of the core's TOML A by "block.property[.field]"; false when it is none. The application gives it (portdiag.cc).
typedef bool (*PanelResolve)(const char *name, PanelReg *out);

struct PanelRow {
    char key[PNL_KEY];
    char action[12];                   // "" a value row
    PanelReg reg;
    bool system;
    int32_t lo, hi, step, def;         // def: the register value Defaults puts
    uint16_t n;
    const char *const *names;
    const int32_t *vals;
    uint8_t nvfmt;
    const uint8_t *vfmt;
};

struct Panel {
    char name[48], presets[24], preset_kind[16];
    int nrows;
    PanelRow row[PNL_ROWS];
    const char *namep[PNL_NAMES];
    int32_t valp[PNL_NAMES];
    int nn;
    uint8_t vfp[PNL_VFMT];
    int nvf;
    char pool[3072];
    int np;
};

// What a row with `system` does with its value: the application gives the function that hands it to the picture layer (picture_set_system); with none the row only writes its register.
void panel_set_system_hook(void (*fn)(CoreCtx *, int));
// Parses a panel file; 0, else -1 with the line and the reason in err.
int panel_parse(const char *text, uint32_t len, Panel *p, PanelResolve res, char *err, int errn);
// The value a row holds now (the register, signed when the row is), written to the register (clamped to the row's range; a row with system / video_format also tells the picture layer).
int32_t panel_get(CoreCtx *c, const PanelRow &r);
void panel_put(CoreCtx *c, const PanelRow &r, int32_t v);
// The index of the name a register value has, -1 for none.
int panel_name_idx(const PanelRow &r, int32_t v);
// The text a row shows for its value ("#n" for a value without a name).
void panel_text(CoreCtx *c, const PanelRow &r, char *out, int n);
// LEFT (dir -1) or RIGHT (+1) on a value row: the next name (wrapping), or a step of the row (wrapping at the ends).
void panel_step(CoreCtx *c, const PanelRow &r, int dir);
// A preset file's text read into one value per row: have[k] is 0 when the file does not name the row, PNL_HAVE when raw[k] is its value, PNL_BAD when the file names a value the row has no name for;
// the number of those.
#define PNL_HAVE 1
#define PNL_BAD  2
int panel_preset_parse(const Panel &p, const char *text, int32_t raw[PNL_ROWS], uint8_t have[PNL_ROWS]);
// The rows of a preset file's text put into the registers, in the order of the panel; a row the file lacks keeps its value. *bad counts the lines that name a row with a value it does not have.
int panel_preset_load(CoreCtx *c, const Panel &p, const char *text, int *bad);
// A preset file's text (every value row, as the name or "#n"); the length, -1 when it does not fit.
int panel_preset_write(CoreCtx *c, const Panel &p, const char *name, char *out, int cap);
// Every value row to its default.
void panel_defaults(CoreCtx *c, const Panel &p);

// Opens the panel on the person's screen (CoreDialogs::edit_list) and returns when it is closed; on a task of its own. `folder` is the core's folder on the card.
void panel_run(CoreCtx *c, CoreDialogs *dlg, const Panel &p, const char *folder);

#endif
