// The Ultimate DMA service: the raw command socket on TCP port 64 (FW network/socket_dma.cc:129-232, 426-525; the setting "Ultimate DMA Service", the password guard :117, :484).
// A generic module (no core names): what a command does to the machine is an operation under the names `dma.*`, `stream.*` and `machine.warm_reset`; a core that registers no
// `dma.prg` gets no server. Board code: lwIP sockets and FreeRTOS tasks.
#ifndef COREAPP_DMASVC_H
#define COREAPP_DMASVC_H

#include "ctx.h"

// The `start` entry of the DMA library in the library table (libtab.h), called by netsvc_apply after the network settings were pushed: the first call starts the server task (once); it listens only while the setting is on.
void dmasvc_start(CoreCtx *c);

#endif
