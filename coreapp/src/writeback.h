// What a game writes to its cartridge or battery RAM goes back to the user's file (T154, D3): one service in the shared base, whatever core
// the medium belongs to. A core registers one writable medium (the file the user has, how to rebuild it) and reports every change of the
// medium's content with wb_mark() / wb_mark_aux(); the service owns the setting "Save Changed Cartridge" (Off / Ask / Auto), the quiet time
// and the maximum age, the moments the file must be written first (reset, eject, core change, power-off request, menu open) and the safe
// write: the new file under NAME.tmp, the file as first found kept once as NAME.bak, then NAME.tmp becomes NAME.
//
// Pure: the machine is a CoreCtx (CoreIo, CoreFs) and a WbPort (time, the critical section against the interrupt that reports a change, the
// interrupt line); tests/coreapp/ drives it with a fake change source.
#ifndef COREAPP_WRITEBACK_H
#define COREAPP_WRITEBACK_H

#include "ctx.h"
#include "ops.h"
#include "svc_wb.h"

#define WB_BLOCK_SHIFT 13                    // the map of changes has one bit per 8 KiB of image
#define WB_BLOCKS_MAX  1024                  // 8 MiB of image: EasyFlash 3's window (the others are addressed with 4 MiB)
#define WB_QUIET_MS    2000                  // T154 Q1: the reference's TRX_QUIET_MS - nothing reported for this long, then the file is written
#define WB_MAX_AGE_MS  30000                 // ... and TRX_MAX_AGE_MS - a change this old is written whatever the machine does
#define WB_RETRY_MS    10000                 // after a failed write

// The values of "Save Changed Cartridge" (list off_ask_auto)
enum { WB_OFF = 0, WB_ASK = 1, WB_AUTO = 2 };

// Why a flush is asked for (the log and the bench say it)
enum WbWhy { WB_WHY_RESET, WB_WHY_EJECT, WB_WHY_CORE, WB_WHY_POWER, WB_WHY_MENU, WB_WHY_TIMER };

// What the service needs of the platform: hw.cc implements it on FreeRTOS and the TRX interrupt, the bench over a fake.
class WbPort {
public:
    virtual ~WbPort() { }
    virtual uint32_t ticks_ms() = 0;                           // the time, callable from the interrupt
    virtual void enter() = 0;                                  // a critical section against the interrupt (map and timestamps)
    virtual void leave() = 0;
    // The interrupt a core reports its changes with: `causes` are IRQ_CAUSE bits, the handler gets the active ones and clears them itself.
    virtual void irq_attach(uint8_t causes, uint8_t (*handler)(uint8_t active)) = 0;
    virtual void irq_enable(uint8_t causes, bool on) = 0;
};

// What has changed since the last write: one bit per 8 KiB block of the image, and `aux` for a medium that has a second part outside it
// (the GMod2's EEPROM).
struct WbDirty {
    uint32_t blocks[WB_BLOCKS_MAX / 32];
    bool aux;
    bool test(uint32_t b) const { return (blocks[b >> 5] >> (b & 31)) & 1; }
};

// The writer of one medium: rebuilds the file into `out` (NAME.tmp, empty) from `old` (the file now) and the machine's image, calling
// nothing but CoreIo / CoreFile. 0 or a reason in msg.
typedef int (*WbWriter)(CoreCtx *c, void *user, CoreFile *old, CoreFile *out, const WbDirty &dirty, char *msg, int msglen);

struct WbMedium {
    char path[FS_PATH_MAX];              // the file the user has
    char name[56];                       // what the question and the notes call it
    WbWriter write;
    void *user;                          // the writer's, kept by the owner for as long as the medium is registered
    bool may_create;                     // the file may not exist yet (a battery save the game has never written): the old file is then null for the writer
    void (*hold)(void *user, bool on);   // optional: the machine must not change the medium while the writer reads it (flash operations wait)
    // optional, with hold (T154 D6): called under the hold right after the changes are taken. 0: the medium has copied what its writer reads of the
    // machine, so the hold ends before the file is written and the machine's flash goes on meanwhile; else the hold lasts the write. unsnap drops the
    // copy after the write (called only after a snap that returned 0).
    int (*snap)(void *user, const WbDirty &dirty);
    void (*unsnap)(void *user);
};

void wb_attach(CoreCtx *c, WbPort *port);                      // once, at the start: the port, and the mode from the setting
WbPort *wb_port(void);                                         // the port the service was attached with (null before)
void wb_set_mode(int mode);                                    // Off / Ask / Auto, the apply of the setting
int wb_mode(void);

// The owner of a medium: one at a time. Registering drops what was pending for the one before - call wb_flush() first.
void wb_register(const WbMedium &m);
void wb_unregister(void);
bool wb_active(void);
const char *wb_path(void);
void *wb_user(void);                                           // the registered medium's `user` (null when none): who owns the medium is told by it
CoreCtx *wb_ctx(void);                                         // the context the service was attached with

// The change source, callable from the interrupt: the bytes [off, off+len) of the image changed / the second part changed.
void wb_mark(uint32_t off, uint32_t len);
void wb_mark_aux(void);

bool wb_pending(void);                                         // a change the file does not have
bool wb_due(void);                                             // ... and quiet for WB_QUIET_MS, or WB_MAX_AGE_MS old, or a flush was asked for
void wb_request_flush(void);
void wb_forget(void);                                          // the change is not wanted in the file ("No"): the map is cleared, the image stays

// Auto: the file is written now when there is a change; the action that follows runs after. 0 when nothing was to be done or it was written, 1
// when it failed (msg says why, and a note for the screen is set). Off and Ask do nothing: Ask has its question at the menu's open.
int wb_flush(CoreCtx *c, WbWhy why, char *msg, int msglen);

// The menu opens (the machine is held): Off nothing; Ask and a change - question is filled and true is returned, the menu asks;
// Auto and a change - written now. A medium that cannot be written is told once per change, in `note`.
bool wb_menu_open(CoreCtx *c, char *question, int qlen, char *note, int nlen);
// The answer to the question: Yes writes (0 or 1 and a reason), No drops the change.
int wb_answer(CoreCtx *c, bool yes, char *msg, int msglen);

// The background step (an operation polled every WB_POLL_MS): with Auto, a change that is due is written; mounts_pass then stores the slots a library notified.
#define WB_POLL_MS 250
int wb_poll(CoreCtx *c);

void wb_register_ops(OpRegistry &r);                           // writeback.mode (the apply of the setting), writeback.poll, writeback.flush (write the registered medium now, as the setting says)

// ---- a medium that is a raw image, the file being the bytes as they are (a battery RAM save, T154 P4) ---------------------------------------
struct WbRaw {
    int (*read)(void *user, uint32_t off, void *buf, uint32_t n);   // the medium's bytes from the machine; 0 or error
    void *user;
    uint32_t size;
};
int wb_raw_write(CoreCtx *c, void *user, CoreFile *old, CoreFile *out, const WbDirty &dirty, char *msg, int msglen);
// Q2 (T154, decided by the recommendation: T148 D5, the core folder's saves/): "<core folder>/saves/<game>.sav"; with no core folder (the
// monolith has no .cor) beside the game. False when nothing fits.
#define WB_SAVES_IN_CORE_FOLDER 1
bool wb_save_path(CoreCtx *c, const char *game_path, char *out, int outlen);
// At the start of a game: the save file's bytes, up to size. The count read, 0 when there is no file (a fresh game) - the core puts them into the machine.
int wb_raw_load(CoreCtx *c, const char *path, void *buf, uint32_t size);

// A file of a library's writer (svc_wb.h): the base's CoreFile behind a handle; valid during the writer's call.
struct SvcWbFile {
    CoreFile *f;
};

// ---- the service for libraries (T239.3 D51, svc_wb.h): the group api->wb answers through it. The owner is the image whose code makes the call (LibLoader::owner_at);
// the medium a library registers is the service's one medium, so the base's own drivers and the libraries exclude each other.
class WbLibSvc {
public:
    virtual ~WbLibSvc() { }
    virtual int add(int owner, const SvcWbMedium *m) = 0;
    virtual int remove(int owner) = 0;
    virtual int write_now(int owner, char *msg, int msglen) = 0;
    virtual int pending(int owner) = 0;
    virtual void mark(uint32_t off, uint32_t len) = 0;         // of the library's medium only; callable from the interrupt
    virtual void mark_aux() = 0;
    virtual void drop_owner(int owner) = 0;                    // the image goes: Auto writes what is pending, then the medium is dropped
};
WbLibSvc *wb_lib_svc(void);

#endif
