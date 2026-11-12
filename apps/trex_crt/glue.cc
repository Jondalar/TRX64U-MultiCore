// TREX CRT on ABI 3 (T104.3 D0, D9.1): what the screens of app.toml bind to. The state values are the strings the tools publish (src/trex_ui.h); the operations
// put a command in the queue of the application's task (src/trex_main.c) and return at once - a dialog that task asks is answered by the base's menu task, which
// must not be waiting for an operation to end. Nothing here draws.
#include "glue.h"

extern "C" {
#include "trex_ui.h"
#include "cart_writer.h"
}

// ---- state values ---------------------------------------------------------------------------------------------------------------------------------

#define ST(field) static const char *st_##field(CoreCtx *) { return g_ui.field; }
ST(type) ST(chip) ST(banks_chip) ST(slot) ST(slot_short) ST(target) ST(port) ST(gate) ST(is_ef3) ST(not_ef3) ST(dest) ST(file) ST(src) ST(src_type) ST(src_name)
ST(status) ST(title) ST(phase) ST(count) ST(pct) ST(note) ST(lock) ST(screen) ST(map_lo) ST(map_hi) ST(map_lo_d) ST(map_hi_d) ST(confirm) ST(scope) ST(res_prog)
ST(res_verify) ST(res_extra) ST(grid) ST(i_type) ST(i_slot) ST(i_bank) ST(i_cpu) ST(i_de00) ST(i_pins)

#define SD(name, field) { "cart." name, st_##field }
static const LibStateDecl g_states[] = {
    SD("type", type), SD("chip", chip), SD("banks_chip", banks_chip), SD("slot", slot), SD("slot_short", slot_short), SD("target", target), SD("port", port),
    SD("gate", gate), SD("is_ef3", is_ef3), SD("not_ef3", not_ef3), SD("dest", dest), SD("file", file), SD("src", src), SD("src_type", src_type),
    SD("src_name", src_name), SD("status", status), SD("title", title), SD("phase", phase), SD("count", count), SD("pct", pct), SD("note", note), SD("lock", lock),
    SD("screen", screen), SD("map_lo", map_lo), SD("map_hi", map_hi), SD("map_lo_d", map_lo_d), SD("map_hi_d", map_hi_d), SD("confirm", confirm),
    SD("scope", scope), SD("res_prog", res_prog), SD("res_verify", res_verify), SD("res_extra", res_extra), SD("grid", grid), SD("i_type", i_type),
    SD("i_slot", i_slot), SD("i_bank", i_bank), SD("i_cpu", i_cpu), SD("i_de00", i_de00), SD("i_pins", i_pins),
};

// ---- operations -----------------------------------------------------------------------------------------------------------------------------------

static int queued(OpOut &o, int kind, int tool, int key)
{
    if (!trex_post(kind, tool, key)) {
        int n = 0;
        const char *m = "TREX CRT is busy with the last request.";
        while (m[n] && n < o.msglen - 1) {
            o.msg[n] = m[n];
            n++;
        }
        o.msg[n] = 0;
        return OPE_BUSY;
    }
    return OPE_OK;
}

template <int TOOL> static int op_enter(CoreCtx *, const OpArgs &, OpOut &o) { return queued(o, CMD_ENTER, TOOL, 0); }
template <int TOOL, int KEY> static int op_key(CoreCtx *, const OpArgs &, OpOut &o) { return queued(o, CMD_KEY, TOOL, KEY); }
static int op_home(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
static int op_scan(CoreCtx *, const OpArgs &a, OpOut &o) { return queued(o, CMD_KEY, a.i(0), KEY_F1); }
static int op_slot(CoreCtx *, const OpArgs &a, OpOut &o) { return queued(o, CMD_KEY, a.i(0), KEY_F7); }
static int op_inspect_key(CoreCtx *, const OpArgs &a, OpOut &o) { return queued(o, CMD_KEY, TOOL_INSPECT, a.i(0)); }
static int op_dest(CoreCtx *, const OpArgs &, OpOut &o) { return queued(o, CMD_DEST, TOOL_DUMP, 0); }
static int op_source(CoreCtx *, const OpArgs &, OpOut &o) { return queued(o, CMD_SOURCE, TOOL_FLASH, 0); }

static const ArgSpec tool_arg[] = { ARG_INT("tool", 1, 3) };
static const ArgSpec key_arg[] = { ARG_INT("key", 0, 0x1FFFF) };

#define OP(name, args, nargs, fn) { "cart." name, args, nargs, fn }
static const LibOpDecl g_ops[] = {
    OP("enter_home", 0, 0, op_home),
    OP("enter_dump", 0, 0, op_enter<TOOL_DUMP>),
    OP("enter_flash", 0, 0, (op_enter<TOOL_FLASH>)),
    OP("enter_inspect", 0, 0, (op_enter<TOOL_INSPECT>)),
    OP("scan", tool_arg, 1, op_scan),
    OP("dump.start", 0, 0, (op_key<TOOL_DUMP, KEY_F3>)),
    OP("write.confirm", 0, 0, (op_key<TOOL_FLASH, KEY_F3>)),
    OP("write.start", 0, 0, (op_key<TOOL_FLASH, KEY_TREX_WRITE>)),
    OP("verify.start", 0, 0, (op_key<TOOL_FLASH, KEY_F5>)),
    OP("ef3.choose_target_or_erase", tool_arg, 1, op_slot),
    OP("ef3.ask_erase", 0, 0, (op_key<TOOL_FLASH, KEY_TREX_ASKERASE>)),
    OP("ef3.erase_all", 0, 0, (op_key<TOOL_FLASH, KEY_TREX_ERASE>)),
    OP("dest.choose", 0, 0, op_dest),
    OP("source.choose", 0, 0, op_source),
    OP("inspect.goto", 0, 0, (op_key<TOOL_INSPECT, KEY_F1>)),
    OP("inspect.write", 0, 0, (op_key<TOOL_INSPECT, KEY_F3>)),
    OP("inspect.set01", 0, 0, (op_key<TOOL_INSPECT, KEY_F5>)),
    OP("inspect.setde00", 0, 0, (op_key<TOOL_INSPECT, KEY_F7>)),
    OP("inspect.key", key_arg, 1, op_inspect_key),
};

// ---- the entries ----------------------------------------------------------------------------------------------------------------------------------

extern "C" int lib_register(const LibApi *, LibRecord *out)
{
    out->ops = g_ops;
    out->nops = (int)(sizeof(g_ops) / sizeof(g_ops[0]));
    out->states = g_states;
    out->nstates = (int)(sizeof(g_states) / sizeof(g_states[0]));
    return 0;
}

static AppCtx *g_base;

extern "C" int app_main(const LibApi *api, AppCtx *ctx)
{
    g_base = ctx;
    const u2p_api_t *u = u2p_compat_init(api);
    return trex_main(u, ctx->action, ctx->file);
}
