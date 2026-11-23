#include <stdarg.h>
#include "exports_stub.h"
#include "c64_cart.h"
#include "cbm1541.h"
#include "d1541_drive.h"
#include "uci.h"
#include "tempdisk.h"
#include "writeback.h"
#include "appsvc.h"
#include <stdio.h>
#include <string.h>

ExportsStub g_stub;

static void note(const CoreCtx *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void note(const CoreCtx *c, const char *fmt, ...)
{
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    g_stub.log += b;
    g_stub.log += ";";
    g_stub.ctx = c;
}
static void say(char *msg, int msglen, const char *t)
{
    g_stub.msg_null = msg != 0 && msglen > 0;
    if (msg && msglen > 0) {
        snprintf(msg, msglen, "%s", t);
    }
}

// ---- c64_cart.h
bool c64_cart_active(void) { return g_stub.cart_active; }
const char *c64_cart_name(void) { return g_stub.cart_name.c_str(); }
const char *c64_cart_path(void) { return g_stub.cart_path.c_str(); }
int c64_cart_crt_type(void) { return g_stub.crt_type; }
uint32_t c64_cart_image_bytes(void) { return g_stub.image_bytes; }
void c64_cart_image_read(CoreCtx *c, uint32_t off, uint8_t *dst, uint32_t n) { note(c, "image_read(%u,%u)", off, n); memset(dst, 0x5A, n); }
void c64_cart_image_write(CoreCtx *c, uint32_t off, const uint8_t *src, uint32_t n) { note(c, "image_write(%u,%u,%02x)", off, n, n ? src[0] : 0); }
bool c64_cart_ef_clear(CoreCtx *c, uint8_t bank, bool high) { note(c, "ef_clear(%u,%d)", bank, high); return g_stub.ef_ok; }
int c64_cart_for_state(CoreCtx *c, const char *path, char *msg, int msglen) { note(c, "for_state(%s)", path ? path : "null"); say(msg, msglen, "state"); return g_stub.rc; }
int c64_cart_remove(CoreCtx *c, char *msg, int msglen) { note(c, "remove"); say(msg, msglen, "remove"); return g_stub.rc; }
int c64_cart_run(CoreCtx *c, const char *path, char *msg, int msglen) { note(c, "run(%s)", path); say(msg, msglen, "a long reason that is cut"); return g_stub.rc; }
int c64_cart_save(CoreCtx *c, const char *path, char *msg, int msglen) { note(c, "save(%s)", path); say(msg, msglen, "save"); return g_stub.rc; }
int c64_cart_run_image(CoreCtx *c, const char *name, const uint8_t *img, char *msg, int msglen) { note(c, "run_image(%s,%02x)", name, img[0]); say(msg, msglen, "image"); return g_stub.rc; }
void c64_cart_player_end(CoreCtx *c) { note(c, "player_end"); }
bool c64_cart_player_active(void) { return g_stub.player; }
int c64_cart_run_modplayer(CoreCtx *, const uint8_t *, uint32_t, char *, int) { return 1; }

// ---- appsvc.h
const SvcC64 *appsvc_c64(void) { static SvcC64 t; return g_stub.c64core ? &t : 0; }

// ---- writeback.h
bool wb_active(void) { return g_stub.wb_active; }

// ---- cbm1541.h
CbmDrive *cbm1541_drive(CoreCtx *c, char *, int) { note(c, "drive"); return g_stub.drive; }
const char *cbm1541_slot_spec(CoreCtx *c) { note(c, "slot_spec"); return g_stub.slot_spec.empty() ? 0 : g_stub.slot_spec.c_str(); }
int cbm1541_slot_mount(CoreCtx *c, const char *spec, char *msg, int msglen) { note(c, "slot_mount(%s)", spec); say(msg, msglen, "mount"); return g_stub.rc; }
int cbm1541_slot_eject(CoreCtx *c, char *msg, int msglen) { note(c, "slot_eject"); say(msg, msglen, "eject"); return g_stub.rc; }
bool cbm1541_service(CoreCtx *c) { note(c, "service"); return g_stub.service_work; }
int cbm1541_media_flush(CoreCtx *c, char *msg, int msglen) { note(c, "media_flush"); say(msg, msglen, "flush"); return g_stub.rc; }
bool cbm1541_media_pending(CoreCtx *c) { note(c, "media_pending"); return g_stub.pending; }

// ---- d1541_drive.h: the class is not linked; the constructor makes the drive of the test, the two calls that are not inline record themselves
NfWriter::NfWriter() { }
NfWriter::~NfWriter() { }
CbmDrive::CbmDrive(CoreIo *, CoreFs *)
{
    type_ = DT_1581;
    powered_ = true;
    bus_id_ = 9;
    mode_ = MM_RO;
    swap_ms_ = 700;
    snprintf(name_, sizeof(name_), "x.d81");
    snprintf(path_, sizeof(path_), "/usb0/games/x.d81");
    snprintf(last_dir_, sizeof(last_dir_), "/usb0/games");
}
CbmDrive::~CbmDrive() { }
void CbmDrive::set_reset_with_c64(bool on) { note(0, "reset_with_c64(%d)", on); }
int CbmDrive::create(const char *path, int format, const char *name, char *msg, int msglen, char *, int, bool, bool, int *)
{
    note(0, "create(%s,%d,%s)", path, format, name);
    say(msg, msglen, "create");
    return g_stub.rc;
}

// ---- uci.h, tempdisk.h
void uci_start(CoreCtx *c, const UciHost *host) { note(c, "uci_start"); g_stub.uci_starts++; g_stub.host = *host; g_stub.host_live = host; }
void tempdisk_set(bool auto_cleanup, bool subfolders) { g_stub.temp_auto = auto_cleanup; g_stub.temp_sub = subfolders; }
