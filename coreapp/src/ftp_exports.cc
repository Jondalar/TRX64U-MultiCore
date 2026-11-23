// The FTP library's table (T239.3 P3-1): the table "ftp" of ftp_api.h, and with it what was the FTP part of netfs.cc - the connection per host slot, the list parser, the fetch and the
// store through a RAM file. PROVENANCE: the list parser and the connect/login/binary sequence follow vendor/gideon/software/filesystem/filesystem_ftp.cc (Gideon Zweijtzer's firmware,
// GPL-3.0); the client is ftp_client.cc. This file is part of the library: libtab.cc (from the TOML B of the app) calls ftp_bind once, when the loader is made, and the bind gives the
// table to netfs (netfs_set_ftp), which has no other way to the library.
#include "ftp_api.h"
#include "ftp_client.h"
#include "netfs.h"
struct CoreCtx;
class LibLoader;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_BUF 16384

namespace {

// a growing RAM file the client writes into (retr) and reads from (stor)
class GrowFile : public File {
public:
    uint8_t *b;
    uint32_t len, cap, pos, limit;
    bool failed;
    explicit GrowFile(uint32_t max_bytes) : File((FileSystem *)0x100), b(0), len(0), cap(0), pos(0), limit(max_bytes), failed(false) { }
    ~GrowFile() { free(b); }
    FRESULT close(void) { return FR_OK; }                       // the owner deletes it
    FRESULT sync(void) { return FR_OK; }
    FRESULT read(void *buffer, uint32_t n, uint32_t *tr)
    {
        uint32_t k = n > len - pos ? len - pos : n;
        memcpy(buffer, b + pos, k);
        pos += k;
        *tr = k;
        return FR_OK;
    }
    FRESULT write(const void *buffer, uint32_t n, uint32_t *tr)
    {
        if (len + n > limit) {
            failed = true;
            return FR_DENIED;
        }
        if (len + n > cap) {
            uint32_t nc = cap ? cap : 65536;
            while (nc < len + n) {
                nc *= 2;
            }
            uint8_t *nb = (uint8_t *)realloc(b, nc);
            if (!nb) {
                failed = true;
                return FR_NOT_ENOUGH_CORE;
            }
            b = nb;
            cap = nc;
        }
        memcpy(b + len, buffer, n);
        len += n;
        *tr = n;
        return FR_OK;
    }
    FRESULT seek(uint64_t p) { pos = p > len ? len : (uint32_t)p; return FR_OK; }
    uint64_t get_size(void) { return len; }
};

// "drwxr-xr-x  2 user group  4096 Jan 01 12:00 dirname" or the Windows form; filesystem_ftp.cc parse_ftp_list_line
static bool parse_line(const char *line, char *name, int namesize, uint64_t *size, bool *is_dir)
{
    *is_dir = false;
    *size = 0;
    name[0] = 0;
    if (!line || !line[0]) {
        return false;
    }
    if (line[0] == 'd' || line[0] == '-' || line[0] == 'l') {
        *is_dir = line[0] == 'd';
        const char *p = line;
        int fields = 0;
        while (*p && fields < 8) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (!*p) {
                break;
            }
            if (fields == 4) {
                *size = (uint64_t)strtoull(p, NULL, 10);
            }
            while (*p && *p != ' ' && *p != '\t') {
                p++;
            }
            fields++;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p) {
            const char *arrow = strstr(p, " -> ");
            int len = arrow ? (int)(arrow - p) : (int)strlen(p);
            while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n')) {
                len--;
            }
            if (len >= namesize) {
                len = namesize - 1;
            }
            memcpy(name, p, len);
            name[len] = 0;
            return len > 0;
        }
        return false;
    }
    if (line[0] >= '0' && line[0] <= '9') {
        const char *p = line;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (!strncmp(p, "<DIR>", 5)) {
            *is_dir = true;
            p += 5;
        } else {
            *size = (uint64_t)strtoull(p, NULL, 10);
        }
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        int len = (int)strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n')) {
            len--;
        }
        if (len >= namesize) {
            len = namesize - 1;
        }
        memcpy(name, p, len);
        name[len] = 0;
        return len > 0;
    }
    return false;
}

struct Slot {
    FTPClient *cl;
    bool up;
    uint32_t sig;
};
static Slot g_slot[FTP_SESSIONS];

static void drop(int n)
{
    if (g_slot[n].cl) {
        g_slot[n].cl->disconnect();
        delete g_slot[n].cl;
        g_slot[n].cl = 0;
    }
    g_slot[n].up = false;
}

static int ftp_connect(int n, const char *host, uint16_t port, const char *user, const char *pass, char *err, int en)
{
    if (n < 0 || n >= FTP_SESSIONS) {
        snprintf(err, en, "FTP: no such host");
        return -1;
    }
    Slot &s = g_slot[n];
    uint32_t sg = 2166136261u;                                   // a host whose settings changed starts over
    const char *parts[3] = { host, user, pass };
    for (int pi = 0; pi < 3; pi++) {
        for (const char *t = parts[pi]; *t; t++) {
            sg = (sg ^ (uint8_t)*t) * 16777619u;
        }
        sg *= 31;
    }
    sg ^= port;
    int changed = 0;
    if (s.cl && s.sig != sg) {
        drop(n);
        changed = 1;
    }
    s.sig = sg;
    if (!s.cl) {
        s.cl = new FTPClient();
    }
    if (s.up && s.cl->is_connected()) {
        return changed;
    }
    s.up = false;
    if (s.cl->open(host, port) < 0) {
        snprintf(err, en, "FTP: no connection to %s:%d", host, port);
        return -1;
    }
    if (s.cl->login(user, pass) < 0) {
        snprintf(err, en, "FTP: login to %s refused", host);
        s.cl->disconnect();
        return -1;
    }
    s.cl->type_binary();
    s.up = true;
    return changed;
}

static void ftp_forget(int n)
{
    if (n >= 0 && n < FTP_SESSIONS) {
        drop(n);
    }
}

// the client of a slot that is connected (ftp_connect first), else null
static FTPClient *client(int n)
{
    return n >= 0 && n < FTP_SESSIONS && g_slot[n].up ? g_slot[n].cl : 0;
}

static int ftp_list(int n, const char *remote, FsEntry *out, int max, char *err, int en)
{
    FTPClient *cl = client(n);
    if (!cl) {
        snprintf(err, en, "FTP: not connected");
        return -1;
    }
    char *buf = (char *)malloc(LIST_BUF);
    if (!buf) {
        return -1;
    }
    int got = 0;
    if (cl->list(remote, buf, LIST_BUF - 1, &got) < 0) {
        snprintf(err, en, "FTP: %s cannot be listed", remote);
        printf("[ftp] %s (%s)\n", err, cl->last_response());
        free(buf);
        g_slot[n].up = false;
        return -1;
    }
    buf[got] = 0;
    int k = 0;
    for (int pass = 0; pass < 2; pass++) {                      // folders first
        char *line = buf;
        while (line && *line && k < max) {
            char *next = strchr(line, '\n');
            if (next) {
                *next++ = 0;
            }
            char name[FS_NAME_MAX + 1];
            uint64_t sz;
            bool d;
            if (parse_line(line, name, sizeof(name), &sz, &d) && strcmp(name, ".") && strcmp(name, "..") && (pass == 0) == d) {
                memset(&out[k], 0, sizeof(FsEntry));
                snprintf(out[k].name, sizeof(out[k].name), "%s", name);
                out[k].dir = d;
                out[k].size = sz;
                k++;
            }
            line = next;
        }
    }
    free(buf);
    return k;
}

static bool ftp_fetch(int n, const char *remote, uint8_t **data, uint32_t *len, uint32_t max_bytes, char *err, int en)
{
    FTPClient *cl = client(n);
    if (!cl) {
        snprintf(err, en, "FTP: not connected");
        return false;
    }
    GrowFile g(max_bytes);
    int got = 0;
    if (cl->retr(remote, &g, &got) < 0 || g.failed) {
        snprintf(err, en, "FTP: %s cannot be fetched%s", remote, g.failed ? " (too big for RAM)" : "");
        g_slot[n].up = false;
        return false;
    }
    *data = g.b;                                                 // the caller owns the buffer now
    *len = g.len;
    g.b = 0;
    return true;
}

static bool ftp_store(int n, const char *remote, const uint8_t *data, uint32_t len, char *err, int en)
{
    FTPClient *cl = client(n);
    if (!cl) {
        snprintf(err, en, "FTP: not connected");
        return false;
    }
    GrowFile g(FTP_MAX_FILE);
    uint32_t tr;
    if (len && g.write(data, len, &tr) != FR_OK) {
        return false;
    }
    g.pos = 0;
    if (cl->stor(remote, &g) < 0) {
        snprintf(err, en, "FTP: %s cannot be written", remote);
        g_slot[n].up = false;
        return false;
    }
    return true;
}

static bool ftp_remove(int n, const char *remote, bool dir)
{
    FTPClient *cl = client(n);
    return cl && (dir ? cl->rmd(remote) : cl->dele(remote)) >= 0;
}

static bool ftp_mkdir(int n, const char *remote)
{
    FTPClient *cl = client(n);
    return cl && cl->mkd(remote) >= 0;
}

static bool ftp_rename(int n, const char *from, const char *to)
{
    FTPClient *cl = client(n);
    return cl && cl->rnfr_rnto(from, to) >= 0;
}

static const FtpApi g_ftp = { sizeof(FtpApi), ftp_connect, ftp_forget, ftp_list, ftp_fetch, ftp_store, ftp_remove, ftp_mkdir, ftp_rename };

}   // namespace

const FtpApi *ftp_table(void)
{
    return &g_ftp;
}

void ftp_bind(CoreCtx *, LibLoader &)
{
    netfs_set_ftp(&g_ftp);
}
