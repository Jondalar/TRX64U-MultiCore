// The groups of the service tables that are the machine's (T104.3 D2): the C64's bus while it is stopped, the TRX debug block and the cartridge port's slot
// block, the flash the firmware boots from, the hand-over to other firmware, the network. Board code; apphost.cc hands them to the loader.
#ifndef COREAPP_APPSVC_H
#define COREAPP_APPSVC_H

#include "ctx.h"
#include "svctab.h"
#include "c64_api.h"

// What the tables need to reach the core and the registry (the operations a reset goes through).
void appsvc_init(CoreCtx *c);

const SvcC64 *appsvc_c64(void);                         // null when the core is not the C64
const C64Api *appsvc_c64_api(void);                     // the same table as api->c64 as C64Api (c64_api.h), what api->lib->find answers under C64_API_NAME; null when the core is not the C64
const SvcTrx *appsvc_trx(void);                         // null unless the ID block reads "TRX2"
const SvcNet *appsvc_net(void);

// Closes every socket an application left open (it ended); the TCP port an application announced (0: none)
void appsvc_net_cleanup(void);
void appsvc_end(void);                                  // an application ended: its sockets are closed, the debug block released
uint16_t appsvc_rpc_port(void);

int appsvc_flash_read(uint32_t addr, void *buf, int len);
void appsvc_handover(void);

#endif
