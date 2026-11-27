// The Library's FTP medium. PROVENANCE: the list parser, the path building and the connect/login/binary sequence follow vendor/gideon/software/filesystem/filesystem_ftp.cc
// (Gideon Zweijtzer's firmware, GPL-3.0); the client and the list parser are the library "ftp" (ftp_exports.cc, ftp_client.cc), reached through its table (ftp_api.h). There, an FTP server
// is a branch of the firmware's file manager tree with a temp file cache; here it is a folder of the application's CoreFs and a file is held in RAM while it is open (a file that does not fit
// in memory is refused).
#include "netfs.h"
#include "ftp_api.h"
#include "ctx.h"
#include "online_api.h"
#include "libload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "FreeRTOS.h"
#include "task.h"

#define DIR_CACHE      256

namespace {

// a RAM file: what was fetched, what is written into it
struct RamBuf {
    uint8_t *b;
    uint32_t len, cap;
    RamBuf() : b(0), len(0), cap(0) { }
    ~RamBuf() { free(b); }
};

struct Host {
    char alias[48], host[64], user[40], pass[40], folder[96];
    uint16_t port;
    bool at_root;                          // set by split(): the path names the host itself
};

class MemFile : public CoreFile {
public:
    RamBuf g;
    bool rw, dirty;
    char path[FS_PATH_MAX];
    uint64_t size() { return g.len; }
    bool writable() { return rw; }
    bool read(uint64_t off, void *buf, uint32_t n)
    {
        if (off > g.len || n > g.len - off) {
            return false;
        }
        memcpy(buf, g.b + off, n);
        return true;
    }
    bool write(uint64_t off, const void *buf, uint32_t n)
    {
        if (!rw || off + n > FTP_MAX_FILE) {
            return false;
        }
        if (off + n > g.cap) {
            uint32_t nc = g.cap ? g.cap : 65536;
            while (nc < off + n) {
                nc *= 2;
            }
            uint8_t *nb = (uint8_t *)realloc(g.b, nc);
            if (!nb) {
                return false;
            }
            g.b = nb;
            g.cap = nc;
        }
        if (off > g.len) {
            memset(g.b + g.len, 0, off - g.len);
        }
        memcpy(g.b + off, buf, n);
        if (off + n > g.len) {
            g.len = off + n;
        }
        dirty = true;
        return true;
    }
    bool sync() { return true; }
};

}   // namespace

// "/Online" and below: the results of the Assembly64 / CommoServe search. The library that has it (TRX64-Ultimate's online, linked through its TOML B) exports the table "online"
// (online_api.h), found among the linked exports (libslot.h); an app not linked with it has none and no folder.
static const OnlineApi *online_api(void)
{
    uint32_t sz = 0;
    const OnlineApi *a = (const OnlineApi *)LibLoader::find_export(ONLINE_API_NAME, U2A3_VERSION(ONLINE_API_MAJOR, 0), &sz);
    return a && sz >= sizeof(OnlineApi) ? a : 0;
}
static bool ol_has_results(void) { const OnlineApi *a = online_api(); return a && a->has_results(); }
static int ol_list(const char *rel, FsEntry *out, int max) { const OnlineApi *a = online_api(); return a ? a->list(rel, out, max) : -1; }
static bool ol_stat(const char *rel, uint64_t *size, bool *dir) { const OnlineApi *a = online_api(); return a && a->stat(rel, size, dir); }
static uint8_t *ol_alloc(uint32_t n) { return (uint8_t *)malloc(n); }
static uint8_t *ol_fetch(const char *rel, uint32_t *len) { const OnlineApi *a = online_api(); return a ? a->fetch(rel, ol_alloc, len) : 0; }
static const char *ol_error(void) { const OnlineApi *a = online_api(); return a ? a->error() : "No online search in this app"; }
static bool ol_loading(void) { const OnlineApi *a = online_api(); return a && a->loading(); }
static bool ol_loading_for(const char *rel) { const OnlineApi *a = online_api(); return a && a->loading_for(rel); }

static bool is_online(const char *p, const char **rel)
{
    if (strncasecmp(p, "/online", 7) || (p[7] && p[7] != '/')) {
        return false;
    }
    *rel = p + 7;
    return true;
}

// The FTP client: the library "ftp" gives its table (ftp_api.h) with its bind entry; an app linked without it has none, and no FTP host answers.
static const FtpApi *g_ftp;

void netfs_set_ftp(const FtpApi *api)
{
    g_ftp = api;
}

static_assert(NETFS_HOSTS == FTP_SESSIONS, "a host slot of the settings has a connection slot in the library");

static MemFile *g_open[8];

struct NetFs::Impl {
    // the last listing: [host, remote dir]
    int dir_host;
    char dir_path[FS_PATH_MAX];
    FsEntry dir_e[DIR_CACHE];
    int dir_n;
    uint32_t dir_t;
};

NetFs::NetFs(CoreFs *inner) : in_(inner), c_(0), loading_(false), file_loading_(false), impl_(new Impl())
{
    why_[0] = 0;
    note_[0] = 0;
    memset(impl_, 0, sizeof(Impl));
    impl_->dir_host = -1;
    err_[0] = 0;
}

static uint32_t now_ms(void) { return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; }

void NetFs::forget()
{
    for (int i = 0; i < NETFS_HOSTS; i++) {
        if (g_ftp) {
            g_ftp->forget(i);
        }
    }
    impl_->dir_host = -1;
}

static const char *txt(CoreCtx *c, int n, const char *what)
{
    char k[40];
    snprintf(k, sizeof(k), "FTP %d %s", n + 1, what);
    return c && c->settings ? cfg_text(c, k) : "";
}

static bool host_of(CoreCtx *c, int n, Host *h)
{
    memset(h, 0, sizeof(*h));
    snprintf(h->host, sizeof(h->host), "%s", txt(c, n, "Host"));
    if (!h->host[0]) {
        return false;
    }
    snprintf(h->alias, sizeof(h->alias), "%s", txt(c, n, "Alias"));
    if (!h->alias[0]) {
        snprintf(h->alias, sizeof(h->alias), "%s", h->host);
    }
    for (char *p = h->alias; *p; p++) {
        if (*p == '/') {
            *p = '_';
        }
    }
    snprintf(h->user, sizeof(h->user), "%s", txt(c, n, "User")[0] ? txt(c, n, "User") : "anonymous");
    snprintf(h->pass, sizeof(h->pass), "%s", txt(c, n, "Password"));
    snprintf(h->folder, sizeof(h->folder), "%s", txt(c, n, "Folder")[0] ? txt(c, n, "Folder") : "/");
    int port = atoi(txt(c, n, "Port"));
    h->port = port > 0 && port < 65536 ? (uint16_t)port : 21;
    return true;
}

// "/ftp", "/ftp/alias/rest" -> host index and the remote path; false when the path is not below /ftp. host -1: the folder /ftp itself (or an unknown alias: -2)
static bool split(CoreCtx *c, const char *path, int *host, char *remote, int rn, Host *h)
{
    if (strncasecmp(path, "/ftp", 4) || (path[4] && path[4] != '/')) {
        return false;
    }
    const char *p = path + 4;
    while (*p == '/') {
        p++;
    }
    *host = -1;
    if (!*p) {
        return true;
    }
    const char *e = strchr(p, '/');
    int al = e ? (int)(e - p) : (int)strlen(p);
    for (int i = 0; i < NETFS_HOSTS; i++) {
        Host hh;
        if (host_of(c, i, &hh) && (int)strlen(hh.alias) == al && !strncasecmp(hh.alias, p, al)) {
            *host = i;
            *h = hh;
            char f[96];
            snprintf(f, sizeof(f), "%s", hh.folder);
            int fl = (int)strlen(f);
            while (fl > 1 && f[fl - 1] == '/') {
                f[--fl] = 0;
            }
            if (!strcmp(f, "/")) {
                f[0] = 0;
            }
            const char *rest = e ? e : "";
            h->at_root = !rest[0] || !strcmp(rest, "/");
            snprintf(remote, rn, "%s%s", f, rest);
            if (!remote[0]) {
                snprintf(remote, rn, "/");
            }
            return true;
        }
    }
    *host = -2;
    return true;
}

// The connection of host slot `n` is up (made or kept). false: no connection, the reason in err. The listing kept from another server of the slot is dropped.
static bool client(NetFs::Impl *im, int n, const Host &h, char *err, int en)
{
    if (!g_ftp) {
        snprintf(err, en, "No FTP in this app");
        return false;
    }
    int r = g_ftp->connect(n, h.host, h.port, h.user, h.pass, err, en);
    if (r < 0) {
        return false;
    }
    if (r > 0) {
        im->dir_host = -1;
    }
    return true;
}

int NetFs::list(const char *dir, FsEntry *out, int max)
{
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    loading_ = false;
    why_[0] = 0;
    if (!strcmp(dir, "/") || !dir[0]) {
        int n = in_->list(dir, out, max);
        if (n < 0 || n >= max) {
            return n;
        }
        for (int i = 0; i < NETFS_HOSTS; i++) {
            Host hh;
            if (g_ftp && host_of(c_, i, &hh)) {                  // an app linked without the library has no folder "FTP"
                memset(&out[n], 0, sizeof(out[n]));
                snprintf(out[n].name, sizeof(out[n].name), "FTP");
                out[n].dir = true;
                n++;
                break;
            }
        }
        if (ol_has_results() && n < max) {
            memset(&out[n], 0, sizeof(out[n]));
            snprintf(out[n].name, sizeof(out[n].name), "Online");
            out[n].dir = true;
            n++;
        }
        return n;
    }
    const char *orel;
    if (is_online(dir, &orel)) {
        int k = ol_list(orel, out, max);
        if (k < 0) {
            snprintf(err_, sizeof(err_), "%s", ol_error());
            loading_ = ol_loading();                           // D41: a job is fetching the folder: the Library waits and lists it again
            if (!loading_) {
                snprintf(why_, sizeof(why_), "%s", err_);
            }
        }
        return k;
    }
    if (!split(c_, dir, &host, remote, sizeof(remote), &h)) {
        return in_->list(dir, out, max);
    }
    if (host == -2) {
        return -1;
    }
    if (host == -1) {                                           // /ftp: the hosts
        int n = 0;
        for (int i = 0; i < NETFS_HOSTS && n < max; i++) {
            Host hh;
            if (host_of(c_, i, &hh)) {
                memset(&out[n], 0, sizeof(out[n]));
                snprintf(out[n].name, sizeof(out[n].name), "%s", hh.alias);
                out[n].dir = true;
                n++;
            }
        }
        return n;
    }
    Impl *im = impl_;
    if (im->dir_host == host && !strcmp(im->dir_path, remote) && now_ms() - im->dir_t < 5000) {
        int n = im->dir_n < max ? im->dir_n : max;
        memcpy(out, im->dir_e, n * sizeof(FsEntry));
        return n;
    }
    if (!client(im, host, h, err_, sizeof(err_))) {
        return -1;
    }
    int n = g_ftp->list(host, remote, im->dir_e, DIR_CACHE, err_, sizeof(err_));
    if (n < 0) {
        return -1;
    }
    im->dir_host = host;
    snprintf(im->dir_path, sizeof(im->dir_path), "%s", remote);
    im->dir_n = n;
    im->dir_t = now_ms();
    int k = n < max ? n : max;
    memcpy(out, im->dir_e, k * sizeof(FsEntry));
    return k;
}

bool NetFs::stat(const char *path, uint64_t *size, bool *dir)
{
    const char *orel;
    if (is_online(path, &orel)) {
        return ol_stat(orel, size, dir);
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->stat(path, size, dir);
    }
    if (host == -2) {
        return false;
    }
    if (host == -1 || h.at_root) {
        *size = 0;
        *dir = true;
        return true;
    }
    char parent[FS_PATH_MAX], base[FS_NAME_MAX + 1];
    const char *sl = strrchr(path, '/');
    if (!sl || sl == path) {
        return false;
    }
    snprintf(base, sizeof(base), "%s", sl + 1);
    {
        size_t pl = (size_t)(sl - path) < sizeof(parent) - 1 ? (size_t)(sl - path) : sizeof(parent) - 1;
        memcpy(parent, path, pl);                                        // (the firmware's printf has no %.*s)
        parent[pl] = 0;
    }
    FsEntry *e = (FsEntry *)malloc(sizeof(FsEntry) * DIR_CACHE);      // whole FAT long names (T180 #38): 70 KB for this look-up, not for good
    if (!e) {
        return false;
    }
    int n = list(parent, e, DIR_CACHE);
    bool found = false;
    for (int i = 0; i < n && !found; i++) {
        if (!strcasecmp(e[i].name, base)) {
            *size = e[i].size;
            *dir = e[i].dir;
            found = true;
        }
    }
    free(e);
    return found;
}

static bool fetch(NetFs::Impl *im, int host, const Host &h, const char *remote, RamBuf *g, char *err, int en)
{
    if (!client(im, host, h, err, en)) {
        return false;
    }
    if (!g_ftp->fetch(host, remote, &g->b, &g->len, FTP_MAX_FILE, err, en)) {
        return false;
    }
    g->cap = g->len;
    return true;
}

int NetFs::load(const char *path, void *buf, uint32_t max)
{
    const char *orel;
    if (is_online(path, &orel)) {
        uint32_t n = 0;
        uint8_t *b = ol_fetch(orel, &n);
        if (!b || n > max) {
            free(b);
            return -1;
        }
        memcpy(buf, b, n);
        free(b);
        return (int)n;
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->load(path, buf, max);
    }
    if (host < 0) {
        return -1;
    }
    RamBuf g;
    if (!fetch(impl_, host, h, remote, &g, err_, sizeof(err_)) || g.len > max) {
        return -1;
    }
    memcpy(buf, g.b, g.len);
    return (int)g.len;
}

static bool upload(NetFs::Impl *im, int host, const Host &h, const char *remote, const uint8_t *data, uint32_t len, char *err, int en)
{
    if (!client(im, host, h, err, en)) {
        return false;
    }
    if (!g_ftp->store(host, remote, data, len, err, en)) {
        return false;
    }
    im->dir_host = -1;
    return true;
}

bool NetFs::save(const char *path, const void *buf, uint32_t len)
{
    {
        const char *orel;
        if (is_online(path, &orel)) {
            return false;                                       // the results are read only
        }
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->save(path, buf, len);
    }
    return host >= 0 && upload(impl_, host, h, remote, (const uint8_t *)buf, len, err_, sizeof(err_));
}

bool NetFs::remove(const char *path)
{
    {
        const char *orel;
        if (is_online(path, &orel)) {
            return false;                                       // the results are read only
        }
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->remove(path);
    }
    if (host < 0) {
        return false;
    }
    bool up = client(impl_, host, h, err_, sizeof(err_));
    uint64_t sz;
    bool d = false;
    stat(path, &sz, &d);
    impl_->dir_host = -1;
    return up && g_ftp->remove(host, remote, d);
}

CoreFile *NetFs::open(const char *path, bool rw)
{
    const char *orel;
    if (is_online(path, &orel)) {
        uint32_t n = 0;
        uint8_t *b = ol_fetch(orel, &n);
        file_loading_ = false;
        note_[0] = 0;
        if (!b) {
            snprintf(err_, sizeof(err_), "%s", ol_error());
            if (ol_loading_for(orel)) {                         // D41 (review): the job of this file runs: the caller keeps its action pending and opens again; open itself never waits
                file_loading_ = true;
                snprintf(note_, sizeof(note_), "%s", err_);
            }
            return 0;
        }
        MemFile *m = new MemFile();
        snprintf(m->path, sizeof(m->path), "%s", path);
        m->rw = false;                                          // the library is read only
        m->dirty = false;
        m->g.b = b;
        m->g.len = m->g.cap = n;
        for (int i = 0; i < 8; i++) {
            if (!g_open[i]) {
                g_open[i] = m;
                return m;
            }
        }
        delete m;
        return 0;
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->open(path, rw);
    }
    if (host < 0) {
        return 0;
    }
    MemFile *m = new MemFile();
    snprintf(m->path, sizeof(m->path), "%s", path);
    m->rw = rw;
    m->dirty = false;
    uint64_t sz;
    bool d;
    bool known = stat(path, &sz, &d);
    printf("[netfs] open %s rw %d: %s\n", path, rw ? 1 : 0, known ? (d ? "a folder" : "known") : "unknown");
    if (known && !d) {
        if (!fetch(impl_, host, h, remote, &m->g, err_, sizeof(err_))) {
            delete m;
            return 0;
        }
    } else if (!rw) {
        delete m;
        return 0;
    }
    for (int i = 0; i < 8; i++) {
        if (!g_open[i]) {
            g_open[i] = m;
            return m;
        }
    }
    delete m;                                                    // eight open at once is more than the application ever asks
    return 0;
}

void NetFs::close(CoreFile *f)
{
    MemFile *m = 0;
    for (int i = 0; i < 8; i++) {                                // no RTTI in this build: the files this class made are known by address
        if (g_open[i] == (MemFile *)f) {
            m = g_open[i];
            g_open[i] = 0;
        }
    }
    if (!m) {
        in_->close(f);
        return;
    }
    if (m->dirty) {
        save(m->path, m->g.b, m->g.len);
    }
    delete m;
}

bool NetFs::free_bytes(const char *path, uint64_t *out)
{
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (split(c_, path, &host, remote, sizeof(remote), &h)) {
        return false;
    }
    return in_->free_bytes(path, out);
}

// The media level lists the FTP hosts and the Online results next to the media (list above): what they are, in the panel's words; the media are the inner file system's.
bool NetFs::medium_info(const char *name, MediumInfo *out)
{
    if (!strcmp(name, "FTP") || !strcmp(name, "Online")) {
        memset(out, 0, sizeof(*out));
        out->ready = true;
        out->writable = !strcmp(name, "FTP");
        snprintf(out->contents, sizeof(out->contents), "%s", !strcmp(name, "FTP") ? "FTP host" : "Search results of Online");
        return true;
    }
    return in_->medium_info(name, out);
}

bool NetFs::writable(const char *path)
{
    const char *orel;
    if (is_online(path, &orel)) {
        return false;                                           // the results are read only
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->writable(path);
    }
    return host >= 0;                                           // an FTP host: the server decides
}

bool NetFs::mkdir(const char *path)
{
    {
        const char *orel;
        if (is_online(path, &orel)) {
            return false;                                       // the results are read only
        }
    }
    int host;
    char remote[FS_PATH_MAX];
    Host h;
    if (!split(c_, path, &host, remote, sizeof(remote), &h)) {
        return in_->mkdir(path);
    }
    if (host < 0) {
        return false;
    }
    bool up = client(impl_, host, h, err_, sizeof(err_));
    impl_->dir_host = -1;
    return up && g_ftp->mkdir(host, remote);
}

bool NetFs::rename(const char *from, const char *to)
{
    int hf, ht;
    char rf[FS_PATH_MAX], rt[FS_PATH_MAX];
    Host h1, h2;
    bool a = split(c_, from, &hf, rf, sizeof(rf), &h1), b = split(c_, to, &ht, rt, sizeof(rt), &h2);
    if (!a && !b) {
        return in_->rename(from, to);
    }
    if (!a || !b || hf != ht || hf < 0) {
        return false;                                              // across media: the caller copies and deletes
    }
    bool up = client(impl_, hf, h1, err_, sizeof(err_));
    impl_->dir_host = -1;
    return up && g_ftp->rename(hf, rf, rt);
}

static NetFs *g_net;
NetFs *netfs_of(CoreCtx *) { return g_net; }
void netfs_set(NetFs *n) { g_net = n; }
