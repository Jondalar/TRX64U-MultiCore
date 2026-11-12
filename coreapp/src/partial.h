// Reload core (T104 5.14): the running core's partial bitstream, loaded again from the .cor the host started, by the core application itself.
// The ICAP loader and the decoupler are registers of the host (the same ones coreboot.cc drives, 0x10060600..), reachable from this CPU; the
// .cor is read through CoreFs. Pure: the registers are a PartialPort (hw.cc on the board, a fake in tests/coreapp/).
#ifndef COREAPP_PARTIAL_H
#define COREAPP_PARTIAL_H

#include <stdint.h>
#include "ctx.h"
#include "cor.h"

class PartialPort {
public:
    virtual ~PartialPort() { }
    virtual bool present() = 0;                           // the host's ICAP loader answers its ID byte
    // The core's edge cut off (true) or given back (false). Cutting off waits until the status bit says so; false when it did not take effect.
    virtual bool decouple(bool on) = 0;
    virtual void begin() = 0;                             // loader enable, word counter cleared
    virtual void write(const uint8_t *p, uint32_t n) = 0; // the file's bytes in file order, n a multiple of 4
    virtual void end() = 0;
    virtual uint32_t words() = 0;                         // 32 bit words the loader has taken since begin()
};

// True when this core came from a .cor on a host with an ICAP loader: F8 says "Reload core" and the restart loads the partial again. False for
// a flat build (a core without a partial, a .cfw) and on the monolith.
bool reload_is_dfx(const CoreCtx *c);

// Step 1, nothing touched: the .cor of the hand-over is opened, its header, contract, host key and name are checked and the partial's SHA-256
// is compared with the manifest. 0, or 1 and the reason in msg; the machine keeps running.
int partial_check(CoreCtx *c, CorInfo *ci, char *msg, int msglen);

// Step 2: the core is held. Cuts the core off, streams the partial through the loader, checks the word count, gives the core back. 0, or 1 and the
// reason; after a failure the core stays cut off unless the decouple never took effect (a half-loaded core must not touch the host).
int partial_stream(CoreCtx *c, const CorInfo *ci, char *msg, int msglen);

#endif
