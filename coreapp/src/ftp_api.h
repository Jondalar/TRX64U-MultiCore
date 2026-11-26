// The table of the library "ftp" (T239.3 P3-1): the FTP medium's client (ftp_client.h) and what the Library's FTP folder needs of it, one connection per host slot. The file system
// of the application (netfs.cc) calls it and no function of the library; an app linked without the library has no table and no folder "FTP". The library gives the table to netfs in
// its `bind` entry (ftp_exports.cc: netfs_set_ftp). Size first; within a major members are only appended. A call that takes `err` and `en` leaves a sentence there when it fails.
#ifndef COREAPP_FTP_API_H
#define COREAPP_FTP_API_H

#include <stdint.h>
#include "fsif.h"

#define FTP_SESSIONS 4                                    // host slots: NETFS_HOSTS
#define FTP_MAX_FILE (6u << 20)                           // the biggest file held in RAM, fetched or stored

typedef struct FtpApi {
    uint32_t size;
    // Slot n is connected to host:port as user (made, or kept when the settings are those of the connection that is up). 0: connected; 1: connected, and the settings of the slot had
    // changed since the last call (what the caller kept of the old server is stale); -1: no connection or the login was refused.
    int (*connect)(int n, const char *host, uint16_t port, const char *user, const char *pass, char *err, int en);
    // Slot n is closed and its client freed (a setting of the host changed).
    void (*forget)(int n);
    // The entries of the remote folder, folders first, "." and ".." left out: at most `max`. -1: the folder cannot be listed.
    int (*list)(int n, const char *remote, FsEntry *out, int max, char *err, int en);
    // The file as a malloc buffer (the caller frees it; *data is null for an empty file), at most `max_bytes` long (FTP_MAX_FILE).
    bool (*fetch)(int n, const char *remote, uint8_t **data, uint32_t *len, uint32_t max_bytes, char *err, int en);
    bool (*store)(int n, const char *remote, const uint8_t *data, uint32_t len, char *err, int en);
    bool (*remove)(int n, const char *remote, bool dir);
    bool (*mkdir)(int n, const char *remote);
    bool (*rename)(int n, const char *from, const char *to);
} FtpApi;

// The table itself (the tests give it to netfs_set_ftp directly; an app of a TOML B gets it by the bind entry).
const FtpApi *ftp_table(void);

#endif
