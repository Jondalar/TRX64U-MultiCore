// The network services of the Network Settings store that run in the core application (T228 R21): Ultimate ident (UDP 64, FW socket_dma.cc:531-640), the
// syslog forwarder (FW syslog.cc, UDP 514, read once at the start), and what the REST adapter asks of the store (rest.h RestHooks). Board code. The ident
// answer is the one of FW socket_dma.cc, built from the same product helpers; the Ultimate DMA service and the Telnet menu are not here (T228 report).
#ifndef COREAPP_NETSVC_H
#define COREAPP_NETSVC_H

// Called by net.fx after it pushed the settings: the first call starts the ident task and, when "Log to Syslog Server" is set, the syslog forwarder (FW reads it at
// boot only); the REST hooks are set here as well.
void netsvc_apply(void);

// Every byte the application prints (diag.cc's outbyte wrap); a no-op until the syslog forwarder runs.
void netsvc_log_char(int c);

#endif
