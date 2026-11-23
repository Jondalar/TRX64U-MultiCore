#include "rpccontract.h"
#include "svc.h"
#include "ops_glue.h"
#include "rest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gitinfo.h"

const ContractRow rpc_contract[] = {
    { "ping", CK_PING },
    { "daemon/keep_alive", CK_KEEP },
    { "daemon/shutdown", CK_NO_SANDBOX },
    { "project/set", CK_NO_SANDBOX },
    { "session/create", CK_CREATE },
    { "session/models", CK_NO_SANDBOX },
    { "session/model", CK_NO_SANDBOX },
    { "session/list", CK_LIST },
    { "session/close", CK_CLOSE },
    { "session/run", CK_NO_SANDBOX },
    { "session/state", CK_STATE },
    { "session/type", CK_NO_INPUT },
    { "session/joystick_set", CK_NO_INPUT },
    { "session/joystick_clear", CK_NO_INPUT },
    { "session/pot_set", CK_NO_INPUT },
    { "session/pot_clear", CK_NO_INPUT },
    { "session/input_status", CK_NO_INPUT },
    { "session/key_down", CK_NO_INPUT },
    { "session/key_up", CK_NO_INPUT },
    { "session/release_keys", CK_NO_INPUT },
    { "session/load_prg", CK_LOAD },
    { "session/reset", CK_RESET },
    { "session/power", CK_POWER },
    { "session/set_pacing", CK_NO_PACING },
    { "session/drive_status", CK_NO_DEVICE },
    { "session/cart_status", CK_NO_DEVICE },
    { "cart/button", CK_NO_DEVICE },
    { "session/drive_power", CK_NO_DEVICE },
    { "session/drives", CK_NO_DEVICE },
    { "session/drive_reset", CK_DRIVERESET },
    { "session/drive_stop", CK_NO_DEVICE },
    { "session/drive_type", CK_NO_DEVICE },
    { "device/folder_attach", CK_NO_DEVICE },
    { "device/folder_detach", CK_NO_DEVICE },
    { "device/folders", CK_NO_DEVICE },
    { "session/drive_unit", CK_NO_DEVICE },
    { "session/screenshot", CK_NO_SANDBOX },
    { "session/frame_indices", CK_NO_SANDBOX },
    { "session/reu", CK_NO_DEVICE },
    { "session/uci", CK_NO_DEVICE },
    { "session/turbo", CK_NO_DEVICE },
    { "session/read_memory", CK_READ },
    { "session/advance_to_frame", CK_NO_PACING },
    { "session/frame_probe", CK_NO_SANDBOX },
    { "runtime/render_screen", CK_NO_SANDBOX },
    { "monitor/exec", CK_NO_DEBUG },
    { "monitor/state", CK_NO_DEBUG },
    { "fs/complete", CK_NO_DEBUG },
    { "debug/run", CK_NO_DEBUG },
    { "debug/pause", CK_NO_DEBUG },
    { "debug/continue", CK_NO_DEBUG },
    { "debug/step", CK_NO_DEBUG },
    { "debug/state", CK_NO_DEBUG },
    { "debug/break_add", CK_NO_DEBUG },
    { "debug/break_del", CK_NO_DEBUG },
    { "debug/break_list", CK_NO_DEBUG },
    { "api/call", CK_NO_DEBUG },
    { "runtime/run_prg", CK_RUN },
    { "runtime/overlay_run", CK_NO_SANDBOX },
    { "runtime/snapshot_tree", CK_NO_SANDBOX },
    { "runtime/promote_branch", CK_NO_SANDBOX },
    { "runtime/mark", CK_NO_SANDBOX },
    { "runtime/reverse_step", CK_NO_SANDBOX },
    { "runtime/who_wrote", CK_NO_SANDBOX },
    { "runtime/set_reverse_depth", CK_NO_SANDBOX },
    { "runtime/crash_triage", CK_NO_SANDBOX },
    { "runtime/diff_checkpoints", CK_NO_SANDBOX },
    { "runtime/component_diff", CK_NO_SANDBOX },
    { "runtime/find_cheat_candidates", CK_NO_SANDBOX },
    { "runtime/swap_disk_and_continue", CK_NO_PACING },
    { "media/list_paths", CK_NO_MEDIA },
    { "media/browse", CK_NO_MEDIA },
    { "media/ingress", CK_NO_MEDIA },
    { "media/unmount", CK_UNMOUNT },
    { "media/open", CK_OPEN },
    { "media/mount", CK_MOUNT },
    { "media/swap", CK_NO_MEDIA },
    { "media/persist", CK_NO_MEDIA },
    { "media/events", CK_NO_MEDIA },
    { "media/recent", CK_NO_MEDIA },
    { "audio/start", CK_NO_SANDBOX },
    { "audio/stop", CK_NO_SANDBOX },
    { "audio/export", CK_NO_SANDBOX },
    { "batch/start", CK_NO_SANDBOX },
    { "batch/status", CK_NO_SANDBOX },
    { "batch/results", CK_NO_SANDBOX },
    { "trace/start_domains", CK_NO_TRACE },
    { "trace/build_from_ring", CK_NO_TRACE },
    { "trace/definition/validate", CK_NO_TRACE },
    { "trace/definition/put", CK_NO_TRACE },
    { "trace/definition/list", CK_NO_TRACE },
    { "trace/run/start", CK_NO_TRACE },
    { "trace/run/mark", CK_NO_TRACE },
    { "trace/current", CK_NO_TRACE },
    { "trace/index", CK_NO_TRACE },
    { "trace/run/stop", CK_NO_TRACE },
    { "trace/run/status", CK_NO_TRACE },
    { "vic/inspect", CK_NO_SANDBOX },
    { "vic/inspect/provenance", CK_NO_SANDBOX },
    { "vic/inspect/close", CK_NO_SANDBOX },
    { "vic/inspect/evidence", CK_NO_SANDBOX },
    { "vic/inspect/open", CK_NO_SANDBOX },
    { "vic/inspect/at", CK_NO_SANDBOX },
    { "vic/line_trace", CK_NO_SANDBOX },
    { "vic/frame_map", CK_NO_SANDBOX },
    { "checkpoint/read_memory", CK_NO_RING },
    { "vic/inspect/region", CK_NO_SANDBOX },
    { "vic/inspect/at_capture", CK_NO_SANDBOX },
    { "vic/inspect/origin", CK_NO_SANDBOX },
    { "vic/inspect/promote", CK_NO_SANDBOX },
    { "debug/memory_access_map", CK_NO_SANDBOX },
    { "trace/read", CK_NO_TRACE },
    { "checkpoint/list", CK_NO_RING },
    { "checkpoint/capture", CK_NO_RING },
    { "session/tick", CK_NO_PACING },
    { "session/play", CK_NO_PACING },
    { "session/pause", CK_NO_PACING },
    { "session/warp", CK_NO_PACING },
    { "session/input_journal", CK_NO_PACING },
    { "transport/key", CK_NO_RING },
    { "transport/toggle", CK_NO_RING },
    { "sandbox/run", CK_NO_SANDBOX },
    { "sandbox/runMany", CK_NO_SANDBOX },
    { "asm/block", CK_NO_DEBUG },
    { "mark/set", CK_NO_RING },
    { "mark/list", CK_NO_RING },
    { "mark/drop", CK_NO_RING },
    { "mark/goto", CK_NO_RING },
    { "transport/status", CK_NO_RING },
    { "transport/play", CK_NO_RING },
    { "transport/pause", CK_NO_RING },
    { "transport/raw_frame", CK_NO_RING },
    { "transport/frame", CK_NO_RING },
    { "transport/goto", CK_NO_RING },
    { "checkpoint/pin", CK_NO_RING },
    { "checkpoint/unpin", CK_NO_RING },
    { "checkpoint/clear", CK_NO_RING },
    { "checkpoint/restore", CK_NO_RING },
    { "checkpoint/thumbnails", CK_NO_RING },
    { "recorder/start", CK_NO_SANDBOX },
    { "recorder/stop", CK_NO_SANDBOX },
    { "recorder/capture", CK_NO_SANDBOX },
    { "recorder/status", CK_NO_SANDBOX },
    { "recorder/list", CK_NO_SANDBOX },
    { "recorder/dump", CK_NO_SANDBOX },
    { "snapshot/dump", CK_NO_RING },
    { "snapshot/undump", CK_NO_SANDBOX },
    { "undump_media_purge", CK_NO_SANDBOX },
    { "vsf/save", CK_NO_SANDBOX },
    { "vsf/load", CK_NO_SANDBOX },
    { "ringbuffer/dump", CK_NO_RING },
    { "ringbuffer/restore", CK_NO_RING },
    { "runtime/scenario_list", CK_NO_SANDBOX },
    { "runtime/scenario_save", CK_NO_SANDBOX },
    { "runtime/scenario_delete", CK_NO_SANDBOX },
    { "runtime/scenario_load", CK_NO_SANDBOX },
    { "runtime/scenario_run", CK_NO_SANDBOX },
    { "runtime/call", CK_NO_SANDBOX },
};
const int rpc_ncontract = (int)(sizeof(rpc_contract) / sizeof(rpc_contract[0]));

const char *contract_reason(uint8_t kind)
{
    switch (kind) {
    case CK_NO_SANDBOX: return "not on this application: use a local TRX64 sandbox";
    case CK_NO_RING: return "needs the Timewarp ring (T20): no library of this core registers an operation for it";
    case CK_NO_TRACE: return "needs the trace engine: no library of this core registers an operation for it";
    case CK_NO_DEBUG: return "the debugger and the monitor are a library's or a sub-app's: no library of this core registers an operation for it";
    case CK_NO_PACING: return "the machine runs in real time; it has no pacing, ticks or frame stepping";
    case CK_NO_INPUT: return "no operation of this core presses keys or moves a joystick";
    case CK_NO_MEDIA: return "files and media are the menu's browser (F3) and the drive library's operations (drive.*)";
    case CK_NO_DEVICE: return "set by the core's settings (settings.set) and the drive library's operations (drive.*), not by this method";
    }
    return "not served";
}

namespace {

int put_id(const JsonRpcReq &r, char *out, int cap, int *p)
{
    return json_put(out, cap, p, "\"id\":%s", r.has_id ? r.id : "null");
}

int error(const JsonRpcReq &r, int code, const char *msg, char *out, int cap)
{
    int p = 0;
    if (!json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",") || !put_id(r, out, cap, &p) || !json_put(out, cap, &p, ",\"error\":{\"code\":%d,\"message\":", code) ||
        !json_put_string(out, cap, &p, msg) || !json_put(out, cap, &p, "}}")) {
        return -1;
    }
    return p;
}

int result(const JsonRpcReq &r, const char *json, char *out, int cap)
{
    int p = 0;
    if (!json_put(out, cap, &p, "{\"jsonrpc\":\"2.0\",") || !put_id(r, out, cap, &p) || !json_put(out, cap, &p, ",\"result\":%s}", json)) {
        return -1;
    }
    return p;
}

// An operation failed: the contract's codes. A bad argument is -32602 like the daemon's, anything else carries the OpErr.
int op_error(const JsonRpcReq &r, const char *method, const OpResult &res, char *out, int cap)
{
    char m[OP_MSG_MAX + 80];
    snprintf(m, sizeof(m), "%s: %s", method, res.msg);
    return error(r, res.code == OPE_ARG ? -32602 : res.code, m, out, cap);
}

// Arguments for svc_call, built from text and numbers.
struct Args {
    JsonObj o;
    Args() { memset(&o, 0, sizeof(o)); }
    void str(const char *k, const char *v)
    {
        int l = (int)strlen(v) + 1;
        if (o.n >= JSON_KV_MAX || o.used + l > JSON_BUF_MAX) {
            return;
        }
        memcpy(o.buf + o.used, v, l);
        snprintf(o.key[o.n], JSON_KEY_MAX, "%s", k);
        o.v[o.n].t = J_STR;
        o.v[o.n].s = o.buf + o.used;
        o.used += l;
        o.n++;
    }
    void num(const char *k, int32_t v)
    {
        if (o.n >= JSON_KV_MAX) {
            return;
        }
        snprintf(o.key[o.n], JSON_KEY_MAX, "%s", k);
        o.v[o.n].t = J_NUM;
        o.v[o.n].n = v;
        o.n++;
    }
};

// The first of "a|b" that some contributor registered; null if none.
const char *pick(CoreCtx *c, const char *list, char *buf, int cap)
{
    snprintf(buf, cap, "%s", list);
    for (char *t = strtok(buf, "|"); t; t = strtok(0, "|")) {
        if (ops_of(c)->find_op(t)) {
            return t;
        }
    }
    return 0;
}

int missing(const JsonRpcReq &r, const char *method, const char *ops, char *out, int cap)
{
    char m[200];
    snprintf(m, sizeof(m), "%s needs the operation %s, which this core did not register", method, ops);
    return error(r, -32601, m, out, cap);
}

const char *str_param(const JsonRpcReq &r, const char *k)
{
    const JsonVal *v = r.params.get(k);
    return v && v->t == J_STR ? v->s : 0;
}

// unit 8 is drive A, the only drive the drive library serves
bool unit_slot(const JsonRpcReq &r, const char **slot, char *why, int whylen)
{
    const JsonVal *u = r.params.get("unit");
    *slot = "drive_a";
    if (u && u->t == J_NUM && u->n != 8) {
        snprintf(why, whylen, "no drive at unit %d (drive A is unit 8)", (int)u->n);
        return false;
    }
    return true;
}

}

int rpc_contract_handle(RpcSession *s, const JsonRpcReq &r, char *out, int cap)
{
    const ContractRow *row = 0;
    for (int i = 0; i < rpc_ncontract; i++) {
        if (!strcmp(rpc_contract[i].method, r.method)) {
            row = &rpc_contract[i];
        }
    }
    if (!row) {
        return -2;
    }
    CoreCtx *c = s->c;
    const char *m = r.method;
    char res[1500];
    int rp = 0;
    if (row->kind >= CK_NO_SANDBOX) {
        char msg[200];
        snprintf(msg, sizeof(msg), "%s: %s", m, contract_reason(row->kind));
        return error(r, -32601, msg, out, cap);
    }
    OpResult rr;
    char buf[96], why[120];
    switch (row->kind) {
    case CK_PING: {
        json_put(res, sizeof(res), &rp, "{\"runtime_version\":\"trx64-runtime/2\",\"version\":");
        char v[60];
        snprintf(v, sizeof(v), "coreapp %s", c->core_version[0] ? c->core_version : "?");
        json_put_string(res, sizeof(res), &rp, v);
        json_put(res, sizeof(res), &rp, ",\"backend\":\"coreapp\",\"core_version\":");      // T221: the three SemVer strings by name
        json_put_string(res, sizeof(res), &rp, c->core_version);
        json_put(res, sizeof(res), &rp, ",\"app_version\":");
        json_put_string(res, sizeof(res), &rp, APP_VERSION_STRING);
        json_put(res, sizeof(res), &rp, ",\"host_version\":");
        json_put_string(res, sizeof(res), &rp, c->host_version);
        json_put(res, sizeof(res), &rp, ",\"core\":");
        json_put_string(res, sizeof(res), &rp, c->desc ? c->desc->cor_name : "");
        json_put(res, sizeof(res), &rp, ",\"project\":null,\"idleExit\":null}");
        return result(r, res, out, cap);
    }
    case CK_KEEP:
        return result(r, "{\"armed\":false}", out, cap);                       // there is no idle exit on a board
    case CK_CREATE:
    case CK_LIST:
    case CK_STATE: {
        json_put(res, sizeof(res), &rp, row->kind == CK_LIST ? "{\"sessions\":[{" : row->kind == CK_CREATE ? "{\"attached\":true," : "{");
        json_put(res, sizeof(res), &rp, "\"id\":\"default\",\"model\":");
        json_put_string(res, sizeof(res), &rp, c->desc ? c->desc->cor_name : "");
        if (row->kind == CK_STATE) {
            json_put(res, sizeof(res), &rp, ",\"started\":%s,\"running\":%s,\"paused\":%s,\"menu_open\":%s,\"program_loaded\":%s,\"backend\":\"coreapp\"",
                     c->started ? "true" : "false", c->started && !c->frozen ? "true" : "false", c->frozen ? "true" : "false",
                     c->menu_open ? "true" : "false", c->program_loaded ? "true" : "false");
        }
        json_put(res, sizeof(res), &rp, row->kind == CK_LIST ? "}]}" : "}");
        return result(r, res, out, cap);
    }
    case CK_CLOSE:
        return error(r, -32601, "session/close: the session is the machine on this board; it cannot be closed", out, cap);
    case CK_READ: {
        const JsonVal *rg = r.params.get("ranges");
        if (!rg || rg->t != J_RAW || rg->s[0] != '[') {
            return error(r, -32602, "read_memory: `ranges` must be an array of {addr, len, lens?}", out, cap);
        }
        const int total_max = 6144;                                           // what fits an answer of the board's buffer (base64 is a third larger)
        int total = 0;
        char *body = out + 256;                                               // build the answer behind the envelope's room, then move it
        int bp = 0, bcap = cap - 256 - 64;
        json_put(body, bcap, &bp, "{\"chunks\":[");
        for (int i = 0;; i++) {
            const char *item;
            int il;
            if (!json_array_item(rg->s, rg->n, i, &item, &il)) {
                break;
            }
            JsonObj one;
            if (json_parse_object(item, il, &one, why, sizeof(why))) {
                return error(r, -32602, "read_memory: each range needs `addr` and `len`", out, cap);
            }
            const JsonVal *a = one.get("addr"), *l = one.get("len"), *lens = one.get("lens");
            if (!a || a->t != J_NUM || a->n < 0) {
                return error(r, -32602, "read_memory: each range needs `addr`", out, cap);
            }
            if (!l || l->t != J_NUM || l->n < 1 || l->n > 0x10000) {
                return error(r, -32602, "read_memory: each range needs `len` in 1..=65536", out, cap);
            }
            const char *ln = lens && lens->t == J_STR ? lens->s : "cpu";
            if (strcmp(ln, "cpu") && strcmp(ln, "ram")) {
                snprintf(why, sizeof(why), "read_memory: lens \"%s\" is not on this core (cpu, ram)", ln);
                return error(r, -32602, why, out, cap);
            }
            total += l->n;
            if (total > total_max) {
                snprintf(why, sizeof(why), "read_memory: %d bytes asked for, %d is this application's cap per call", total, total_max);
                return error(r, -32602, why, out, cap);
            }
            if (!ops_of(c)->find_op("machine.read_mem")) {
                return missing(r, m, "machine.read_mem", out, cap);
            }
            if (i && !json_put(body, bcap, &bp, ",")) {
                return -1;
            }
            if (!json_put(body, bcap, &bp, "{\"addr\":%d,\"len\":%d,\"lens\":\"%s\",\"bytes\":\"", (int)a->n, (int)l->n, ln)) {
                return -1;
            }
            // 255 is a multiple of 3: the base64 pieces join without padding in the middle
            for (int done = 0; done < l->n;) {
                int take = l->n - done < 255 ? l->n - done : 255;
                Args args;
                args.num("address", a->n + done);
                args.num("length", take);
                svc_call_net(c, "machine.read_mem", &args.o, &rr);
                if (rr.code != OPE_OK || rr.ndata != take) {
                    return op_error(r, m, rr, out, cap);
                }
                char b64[400];
                b64_encode(rr.data, take, b64, sizeof(b64));
                if (!json_put(body, bcap, &bp, "%s", b64)) {
                    return -1;
                }
                done += take;
            }
            if (!json_put(body, bcap, &bp, "\"}")) {
                return -1;
            }
        }
        if (!json_put(body, bcap, &bp, "],\"c64Cycles\":null,\"rasterLine\":null}")) {
            return -1;
        }
        int p = 0;
        if (!json_put(out, 256, &p, "{\"jsonrpc\":\"2.0\",") || !put_id(r, out, 256, &p) || !json_put(out, 256, &p, ",\"result\":")) {
            return -1;
        }
        memmove(out + p, body, bp);
        out[p + bp] = '}';
        out[p + bp + 1] = 0;
        return p + bp + 1;
    }
    case CK_RESET: {
        const char *mode = str_param(r, "mode");
        bool soft = mode && !strcmp(mode, "soft");
        Args args;
        svc_call_net(c, soft ? "machine.warm_reset" : "machine.cold_reset", &args.o, &rr);
        if (rr.code != OPE_OK) {
            return op_error(r, m, rr, out, cap);
        }
        json_put(res, sizeof(res), &rp, "{\"mode\":\"%s\",\"c64Cycles\":null,\"pc\":null}", soft ? "soft" : "cold");
        return result(r, res, out, cap);
    }
    case CK_POWER: {
        const char *op = str_param(r, "op");
        if (!op || (strcmp(op, "on") && strcmp(op, "off"))) {
            return error(r, -32602, "session/power: op must be \"on\" or \"off\"", out, cap);
        }
        if (!strcmp(op, "off")) {
            return error(r, OPE_UNSUPPORTED, "session/power: the core cannot be powered off from here; the host menu stops it", out, cap);
        }
        Args args;
        svc_call_net(c, "machine.cold_reset", &args.o, &rr);
        if (rr.code != OPE_OK) {
            return op_error(r, m, rr, out, cap);
        }
        json_put(res, sizeof(res), &rp, "{\"op\":\"on\",\"powered\":true,\"pc\":null,\"c64Cycles\":null}");
        return result(r, res, out, cap);
    }
    case CK_LOAD:
    case CK_RUN: {
        const char *path = str_param(r, "prg_path");
        if (!path) {
            return error(r, -32602, r.params.get("bytes_b64") ? "bytes_b64: this application starts a file on a stick; give prg_path" : "prg_path is required", out, cap);
        }
        const char *list = row->kind == CK_LOAD ? "prg.load|c64.load_prg" : "prg.run|c64.run_prg";
        const char *op = pick(c, list, buf, sizeof(buf));
        if (!op) {
            return missing(r, m, list, out, cap);
        }
        Args args;
        args.str("path", path);
        const JsonVal *la = r.params.get(row->kind == CK_LOAD ? "run_addr" : "none");
        if (la && la->t == J_NUM) {
            args.num("start", la->n);
        }
        svc_call_net(c, op, &args.o, &rr);
        if (rr.code != OPE_OK) {
            return op_error(r, m, rr, out, cap);
        }
        json_put(res, sizeof(res), &rp, "{\"ok\":true,\"msg\":");
        json_put_string(res, sizeof(res), &rp, rr.msg);
        json_put(res, sizeof(res), &rp, "}");
        return result(r, res, out, cap);
    }
    case CK_MOUNT:
    case CK_OPEN: {
        const char *path = str_param(r, "path");
        const char *slot;
        if (!path) {
            return error(r, -32602, "path is required", out, cap);
        }
        if (!unit_slot(r, &slot, why, sizeof(why))) {
            return error(r, -32602, why, out, cap);
        }
        const char *op = row->kind == CK_MOUNT ? "drive.mount" : "drive.mount_run";
        if (!ops_of(c)->find_op(op)) {
            return missing(r, m, op, out, cap);
        }
        Args args;
        args.str("slot", slot);
        args.str("path", path);
        if (row->kind == CK_MOUNT) {
            args.str("mode", "rw");
        }
        svc_call_net(c, op, &args.o, &rr);
        if (rr.code != OPE_OK) {
            return op_error(r, m, rr, out, cap);
        }
        json_put(res, sizeof(res), &rp, "{\"ok\":true,\"msg\":");
        json_put_string(res, sizeof(res), &rp, rr.msg);
        json_put(res, sizeof(res), &rp, "}");
        return result(r, res, out, cap);
    }
    case CK_UNMOUNT:
    case CK_DRIVERESET: {
        const char *slot;
        if (!unit_slot(r, &slot, why, sizeof(why))) {
            return error(r, -32602, why, out, cap);
        }
        const char *op = row->kind == CK_UNMOUNT ? "drive.eject" : "drive.reset";
        if (!ops_of(c)->find_op(op)) {
            return missing(r, m, op, out, cap);
        }
        Args args;
        args.str("slot", slot);
        svc_call_net(c, op, &args.o, &rr);
        if (rr.code != OPE_OK) {
            return op_error(r, m, rr, out, cap);
        }
        return result(r, "{\"ok\":true}", out, cap);
    }
    }
    return error(r, -32601, m, out, cap);
}
