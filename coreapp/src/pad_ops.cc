#include "pad.h"
#include <stdio.h>
#define PAD_DESC_TEXT 900                  // 256 bytes at 3 characters, the name and the ids

static int op_set_map(CoreCtx *, const OpArgs &a, OpOut &)
{
    pad_set_mode(a.i(0));
    return OPE_OK;
}
static const ArgSpec set_map_arg[] = { ARG_INT("value", 0, 1) };

// T228 R10: the setting that picks the register (the joystick port) the first pad drives; the running core's spec first, so that the value is not lost to it.
static int op_port(CoreCtx *c, const OpArgs &a, OpOut &)
{
    pad_ensure(c);
    pad_set_port(a.i(0));
    return OPE_OK;
}

// The service of a core that has no other (NES): the words that changed into the registers.
static int op_poll(CoreCtx *c, const OpArgs &, OpOut &)
{
    pad_pace(c);
    return OPE_OK;
}

static const char *pad_state_text(int p)
{
    static char text[2][80];
    uint32_t b;
    uint8_t d;
    if (!pad_state(p, &b, &d)) {
        return "-";
    }
    snprintf(text[p], sizeof(text[p]), "%s buttons=%08X dpad=%X", pad_name(p), (unsigned)b, d);
    return text[p];
}
static const char *state_pad1(CoreCtx *) { return pad_state_text(0); }
static const char *state_pad2(CoreCtx *) { return pad_state_text(1); }

// "<name> VID:PID <len> bytes: <hex>" - the descriptor of the pad, for REST: the console ring (4 KB) is gone long before somebody reads it.
static const char *pad_desc_text(int p)
{
    static char text[2][PAD_DESC_TEXT];
    int len;
    const uint8_t *d = pad_descriptor(p, &len);
    if (!d) {
        return "-";
    }
    int w = snprintf(text[p], sizeof(text[p]), "%s %04X:%04X %d bytes:", pad_name(p), pad_vid(p), pad_pid(p), len);
    for (int i = 0; i < len && w + 3 < (int)sizeof(text[p]); i++) {
        w += snprintf(text[p] + w, sizeof(text[p]) - w, " %02X", d[i]);
    }
    return text[p];
}
static const char *state_desc1(CoreCtx *) { return pad_desc_text(0); }
static const char *state_desc2(CoreCtx *) { return pad_desc_text(1); }

void pad_register(OpRegistry &r)
{
    r.add_op("pad.set_map", set_map_arg, 1, op_set_map);
    r.add_op("pad.port", set_map_arg, 1, op_port);                  // value 0 / 1: the spec's register index (the port) the first pad drives
    r.add_op("pad.poll", 0, 0, op_poll);
    padwiz_register(r);                                              // the mapping wizard: pad.map.*, menu.pad_map_1 / _2 (padwiz.cc)
    r.add_state("pad.1", state_pad1);
    r.add_state("pad.2", state_pad2);
    r.add_state("pad.1.descriptor", state_desc1);
    r.add_state("pad.2.descriptor", state_desc2);
}
