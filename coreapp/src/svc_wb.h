// Base 3.4, group `wb` (T239.3 D51), the write-back service for libraries: what a core's game or program writes into a medium of the machine (a cartridge's flash,
// a battery RAM, a RAM-backed disk) goes back to the user's file, the way the base does it for its own drivers (T154, T180 D4). It joins LibApi as `const SvcWb *wb`,
// after `overlay`; a library built before the group never reads it (its LibApi is smaller), a loader without a core leaves it null.
//
// What the group is:
//   - A library registers ONE writable medium: the user's file, a name for the questions, and a writer that rebuilds the file from the old file and the image. It reports
//     every change of the medium's content with mark() / mark_aux(), and asks for the write with write_now(). The base's one medium per core is the rule: while another owner
//     holds the medium (the cartridge driver of the base, another library), register_medium() refuses and changes nothing.
//   - The person's setting "Save Changed Cartridge" decides, as for the base's own media: Off writes nothing, Ask asks at the next open of the menu, Auto writes (at the
//     moments reset, eject, core change, power-off and menu, when the change has been quiet for 2 s, and when write_now() asks). A call never writes against the setting.
//   - The write is the safe one: the new file under NAME.tmp, the file as first found kept once as NAME.bak, then NAME.tmp becomes NAME.
//   - Applications and the API ask for the write through the operation "writeback.flush" (SvcOps::call, JSON-RPC); they register no medium.
//
// Contracts of the whole group:
//   - No call takes a CoreCtx. register_medium, unregister_medium, write_now and pending are for a task that may block (the file is written); mark and mark_aux may also be
//     called from an interrupt. The writer is called by the base on whatever task writes (the executor, the poll, the menu), with the medium held when `hold` is given.
//   - The medium belongs to the image whose code registered it. When that image is released the base writes what is pending (setting Auto) and drops the medium.
#ifndef COREAPP_SVC_WB_H
#define COREAPP_SVC_WB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The files a writer handles: valid only during the writer's call. Not an `SvcFile` of api->fs: those belong to the loader's account and are closed by their opener.
struct SvcWbFile;

// What has changed since the last write: one bit per block of 1 << block_shift bytes of the image (bit b & 31 of blocks[b >> 5]), `aux` when the medium's second part changed
// (the GMod2's EEPROM). `blocks` holds nblocks bits.
typedef struct SvcWbDirty {
    uint32_t block_shift;
    uint32_t nblocks;
    const uint32_t *blocks;
    int aux;
} SvcWbDirty;

// The writer: rebuilds the file into `out` (NAME.tmp, empty) from `old` (the file now; null when the medium was registered with may_create and the file is not there) and
// the image, with svc_wb_read / write / size only. 0, or non-zero with the reason in msg (msglen bytes, the person sees it).
typedef int (*SvcWbWriter)(void *user, struct SvcWbFile *old, struct SvcWbFile *out, const SvcWbDirty *dirty, char *msg, int msglen);

typedef struct SvcWbMedium {
    uint32_t size;                        // sizeof(SvcWbMedium): the base reads the members this size covers
    const char *path;                     // the file the user has (a full path, fewer than FS_PATH_MAX = 520 characters); copied
    const char *name;                     // what the question and the notes call it (at most 55 characters, cut); copied
    SvcWbWriter write;
    void *user;                           // the writer's and the hold's; kept by the library for as long as the medium is registered
    bool may_create;                      // the file may not exist yet (a battery save the game has never written)
    void (*hold)(void *user, bool on);    // optional: the machine must not change the medium while the writer reads it; called with true before and false after
} SvcWbMedium;

// Results: 0 is done, a positive number says why nothing was written although nothing failed, a negative number is an error. error() says it in words.
enum {
    SVCWB_OK = 0,
    SVCWB_ASK = 1,                        // write_now: a change is pending, the setting is Ask: the person is asked at the next open of the menu
    SVCWB_OFF = 2,                        // write_now: a change is pending, the setting is Off: nothing is written
    SVCWB_E_NOSERVICE = -1,               // no write-back service (no core runs, or the loader has no core)
    SVCWB_E_ARG = -2,                     // a null or too long path, no writer, a size that does not cover the members
    SVCWB_E_TAKEN = -3,                   // another owner holds the medium (the base's cartridge driver, another library)
    SVCWB_E_NOTMINE = -4,                 // the calling image has no registered medium
    SVCWB_E_FAILED = -5                   // the write failed: msg says why, the change stays pending
};

typedef struct SvcWb {
    uint32_t size;
    // Registers the calling image's writable medium: 0, SVCWB_E_NOSERVICE, SVCWB_E_ARG or SVCWB_E_TAKEN (nothing is changed then). An image that registers again replaces its
    // medium; what was pending for the old one is written first (setting Auto) and then dropped.
    int (*register_medium)(const SvcWbMedium *m);
    // Gives the medium back. What is pending is written first when the setting is Auto, else it is dropped. SVCWB_E_NOTMINE when the image has none.
    int (*unregister_medium)(void);
    // The bytes [off, off + len) of the image changed / the medium's second part changed. A call while the image has no registered medium does nothing.
    void (*mark)(uint32_t off, uint32_t len);
    void (*mark_aux)(void);
    // Writes the pending change now, as the setting says: 0 when it was written or nothing is pending, SVCWB_ASK, SVCWB_OFF, SVCWB_E_NOTMINE, or SVCWB_E_FAILED with the
    // reason in msg (msg may be null).
    int (*write_now)(char *msg, int msglen);
    // 1 when the image's medium has a change the file does not have, else 0.
    int (*pending)(void);
    // The files of a writer. Read and write are all or nothing; write at the end makes the file longer.
    bool (*file_read)(struct SvcWbFile *f, uint64_t off, void *buf, uint32_t len);
    bool (*file_write)(struct SvcWbFile *f, uint64_t off, const void *buf, uint32_t len);
    uint64_t (*file_size)(struct SvcWbFile *f);
    // The words of an SVCWB_* code.
    const char *(*error)(int code);
} SvcWb;

#ifdef __cplusplus
}
#endif

#endif
