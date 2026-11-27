// Base 3.3, group `sync` (T239.3 section 5, night of 2026-11-22; boundary rows 4.2, 11.1). Written first so that the TRX64-Ultimate libraries build to it.
//
// A mutex and a short critical section for a library that runs more than one task (the ring: the poll task and the operations; the monitor: the poll
// task and the menu task). It joins LibApi as the member `const SvcSync *sync` (appended after `lib`, or after the last group 3.2 added); a core without
// tasks of its own gets the group all the same, it is the base's own FreeRTOS wrapper and is never null once 3.3 is built.
//
// Rules of the whole table:
//   - `size` first, members only appended (major rule of section 5).
//   - Task context only: no call here is allowed from an interrupt.
//   - S14 (the per-owner account): a mutex belongs to the library that made it; freeing the library frees every mutex it still holds. A mutex that is
//     locked at that moment is freed all the same, so a library must not keep a task waiting on it past its own end.
//   - No call takes a CoreCtx. The base knows its context.
#ifndef COREAPP_SVC_SYNC_H
#define COREAPP_SVC_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SvcMutex;                                     // opaque; the base's FreeRTOS mutex

#define SYNC_FOREVER 0xFFFFFFFFu                     // mutex_lock: wait without a limit

typedef struct SvcSync {
    uint32_t size;
    // A new mutex with priority inheritance, not recursive (a second lock by the task that holds it waits for its own unlock, or times out).
    // Result: the mutex, null when there is no memory. Memory: the base's, on the calling library's account; free it with mutex_free. Task: any.
    struct SvcMutex *(*mutex_new)(void);
    // Takes the mutex. timeout_ms 0 tries once and returns at once; SYNC_FOREVER waits without a limit; any other value waits that long (the base rounds
    // up to its 5 ms tick). Result: true when taken, false on timeout or a null mutex. Task: any task that may block, so not between enter and leave.
    bool (*mutex_lock)(struct SvcMutex *m, uint32_t timeout_ms);
    // Gives it back. Only the task that took it may give it back. A mutex that is not held does nothing. Task: the holder.
    void (*mutex_unlock)(struct SvcMutex *m);
    // Frees it. Not while it is held or a task waits for it (the result is not defined). A null mutex does nothing. Task: any.
    void (*mutex_free)(struct SvcMutex *m);
    // A short critical section: the four byte accesses of one 32-bit TRX register (trx_io.cc), a flag set and tested in one go (the monitor's engine guard).
    // It nests (a counter, as taskENTER_CRITICAL does). Between enter and leave a task may not call anything that blocks: no sleep_ms, yield, delay_us over
    // a few microseconds, mutex call, file, socket, dialog or log call, and no call that enters a library. Keep it to a few register accesses. Every enter
    // has a leave on the same task; a library that ends with the section open is a bug the base cannot repair. Task: any.
    void (*enter)(void);
    void (*leave)(void);
} SvcSync;

#ifdef __cplusplus
}
#endif

#endif
