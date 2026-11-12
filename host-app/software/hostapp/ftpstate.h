// T163 S3 / P3: what the Network page says about the FTP service. The state is the host's own setting (the "Network Settings" store, which
// hostcfg.cc keeps in /Flash/trx/host.cfg): the service switch, the port the listener binds, and whether a network password is set.
#ifndef FTPSTATE_H
#define FTPSTATE_H

#include <stdio.h>

#define FTPSTATE_PORT 21           // the port ftpd.cc binds (its listen task binds a literal 21)

struct FtpState
{
    bool on;           // CFG_NETWORK_FTP_SERVICE
    int  port;
    bool password;     // CFG_NETWORK_PASSWORD is not empty
};

// The store's values (ftpstate_board.cc on the board, the bench supplies its own).
FtpState ftp_state_now(void);
// Switches the service: sets the store item, writes the store (host.cfg) and effectuates it, so the FTP daemon follows at once.
void ftp_state_set(bool on);

// The Network page's FTP row. Off says off whatever the network does; on waits for an address first.
static inline void ftp_state_text(const FtpState &s, bool net, char *out, int n)
{
    if (!s.on) snprintf(out, n, "off");
    else if (!net) snprintf(out, n, "waiting for the network");
    else snprintf(out, n, "on, port %d, %s", s.port, s.password ? "password set" : "no password");
}

#endif
