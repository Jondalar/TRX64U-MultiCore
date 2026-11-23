// The Library's FTP medium (T104.2 UI): a CoreFs in front of the disk file system that adds the folder "/ftp" - one folder per FTP host of the settings (Settings, FTP hosts) -
// and answers every path below it over FTP (the library "ftp": ftp_api.h, its client is ftp_client.cc, Gideon's). Host-owned.
#ifndef COREAPP_NETFS_H
#define COREAPP_NETFS_H

#include "fsif.h"
#include "ctx.h"

#define NETFS_HOSTS 4

class NetFs : public CoreFs {
public:
    explicit NetFs(CoreFs *inner);
    void bind(CoreCtx *c) { c_ = c; }
    // A setting of a host changed (or the settings were loaded): the connections and the listing are dropped.
    void forget();
    int list(const char *dir, FsEntry *out, int max);
    bool stat(const char *path, uint64_t *size, bool *dir);
    int load(const char *path, void *buf, uint32_t max);
    bool save(const char *path, const void *buf, uint32_t len);
    bool remove(const char *path);
    CoreFile *open(const char *path, bool rw);
    void close(CoreFile *f);
    bool free_bytes(const char *path, uint64_t *out);
    bool medium_info(const char *name, MediumInfo *out);
    bool writable(const char *path);
    bool mkdir(const char *path);
    bool rename(const char *from, const char *to);
    // what the last FTP command said, for a status line
    const char *last_error() const { return err_; }
    struct Impl;
private:
    CoreFs *in_;
    CoreCtx *c_;
    char err_[96];
    Impl *impl_;
};

struct FtpApi;
// The FTP library gives the file system its table (ftp_exports.cc, the library's bind entry). Without one there is no folder "FTP" in the listing of `/` and every path below it fails (T239.3 P3-1).
void netfs_set_ftp(const FtpApi *api);
NetFs *netfs_of(CoreCtx *c);          // the FTP medium of the running core, null when it has none
void netfs_set(NetFs *n);

#endif
