// The groups of the service tables that are the machine's (T104.3 D2): the network (with the runtime service's announced port), the flash the firmware boots from,
// the hand-over to other firmware. Base API 4.0 has no C64 bus and no TRX block (T239.3 D57). Board code; apphost.cc hands them to the loader.
#ifndef COREAPP_APPSVC_H
#define COREAPP_APPSVC_H

#include "ctx.h"
#include "svctab.h"

void appsvc_init(CoreCtx *c);

const SvcNet *appsvc_net(void);

uint16_t appsvc_rpc_port(void);                         // the TCP port announced through SvcNet::announce (0: none)

int appsvc_flash_read(uint32_t addr, void *buf, int len);
void appsvc_handover(void);

#endif
