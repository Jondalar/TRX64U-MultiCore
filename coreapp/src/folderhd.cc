#include "folderhd_impl.h"
#include <strings.h>

using namespace fh;

// ============================================================================================================================================
// counted memory
// ============================================================================================================================================
static uint32_t g_now, g_peak;

void *fh::mem_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p) {
        g_now += (uint32_t)n;
        if (g_now > g_peak) g_peak = g_now;
    }
    return p;
}
void *fh::mem_realloc(void *p, size_t old_n, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (q) {
        g_now = g_now - (uint32_t)old_n + (uint32_t)n;
        if (g_now > g_peak) g_peak = g_now;
    }
    return q;
}
void fh::mem_free(void *p, size_t n)
{
    if (p) {
        free(p);
        g_now -= (uint32_t)n;
    }
}
uint32_t folderhd_heap_now(void) { return g_now; }
uint32_t folderhd_heap_peak(void) { return g_peak; }
void folderhd_heap_reset_peak(void) { g_peak = g_now; }

// ============================================================================================================================================
// names: FS-UAE's rule. Read from FrodeSolheim/fs-uae @ f362278ccd4c60991caac3b4d240d4a3f751bea2, od-fs/fsdb_host.cpp:
//   evil characters  '%' '\\' '*' '?' '"' '/' '|' '<' '>' (line 197), bytes below 32 (250), a last character '.' or ' ' (230), the third character of a
//   Windows device name AUX CON PRN NUL LPT# COM# (216-227), the last character of a name ending in ".uaem" (265); each becomes '%' and two LOWER case
//   hex digits (273, hex_chars line 199); the Latin-1 bytes are then made UTF-8 (286). Back: UTF-8 to Latin-1 (322), then "%xy" -> byte for a '%'
//   with at least two characters after it (336), a character that is no hex digit counts as 0 (295-307).
// ============================================================================================================================================
static const char HEXL[] = "0123456789abcdef";

int folderhd_aname_to_host(const uint8_t *an, int len, char *out, int outlen)
{
    if (len <= 0) {
        return -1;
    }
    unsigned a = an[0], b = len > 1 ? an[1] : 0, c = len > 2 ? an[2] : 0, d = len > 3 ? an[3] : 0;
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    if (c >= 'a' && c <= 'z') c -= 32;
    int ll = 0;
    if (a == 'A' && b == 'U' && c == 'X') ll = 3;
    if (a == 'C' && b == 'O' && c == 'N') ll = 3;
    if (a == 'P' && b == 'R' && c == 'N') ll = 3;
    if (a == 'N' && b == 'U' && c == 'L') ll = 3;
    if (a == 'L' && b == 'P' && c == 'T' && d >= '0' && d <= '9') ll = 4;
    if (a == 'C' && b == 'O' && c == 'M' && d >= '0' && d <= '9') ll = 4;
    int repl1 = -1, repl2 = -1;
    if (ll && (len == ll || (len > ll && an[ll] == '.'))) {
        repl1 = 2;
    }
    if (an[len - 1] == '.' || an[len - 1] == ' ') {
        repl2 = len - 1;
    }
    int o = 0;
    for (int i = 0; i < len; i++) {
        unsigned x = an[i];
        bool repl = false;
        if (i == repl1 || i == repl2 || x < 32) {
            repl = true;
        }
        if (strchr("%\\*?\"/|<>", (int)x) && x) {
            repl = true;
        }
        if (i == len - 1 && len >= 5 && !strncasecmp((const char *)an + len - 5, ".uaem", 5)) {
            repl = true;
        }
        int need = repl ? 3 : (x >= 0x80 ? 2 : 1);
        if (o + need >= outlen) {
            return -1;
        }
        if (repl) {
            out[o++] = '%';
            out[o++] = HEXL[(x >> 4) & 15];
            out[o++] = HEXL[x & 15];
        } else if (x >= 0x80) {
            out[o++] = (char)(0xC0 | (x >> 6));
            out[o++] = (char)(0x80 | (x & 0x3F));
        } else {
            out[o++] = (char)x;
        }
    }
    out[o] = 0;
    return o;
}

static unsigned hexv(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return 0;
}

int folderhd_host_to_aname(const char *host, uint8_t *out, int outmax)
{
    uint8_t l1[128];
    int n = 0;
    const unsigned char *s = (const unsigned char *)host;
    while (*s) {
        if (n >= (int)sizeof(l1)) {
            return -1;
        }
        unsigned c = *s++;
        if (c < 0x80) {
            l1[n++] = (uint8_t)c;
        } else if ((c & 0xE0) == 0xC0 && (*s & 0xC0) == 0x80) {
            unsigned cp = ((c & 0x1F) << 6) | (*s++ & 0x3F);
            if (cp < 0x80 || cp > 0xFF) {
                return -1;                        // not Latin-1 (or an overlong form)
            }
            l1[n++] = (uint8_t)cp;
        } else {
            return -1;
        }
    }
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (o >= outmax) {
            return -1;
        }
        if (l1[i] == '%' && i < n - 2) {
            out[o++] = (uint8_t)((hexv(l1[i + 1]) << 4) | hexv(l1[i + 2]));
            i += 2;
        } else {
            out[o++] = l1[i];
        }
    }
    return o;
}

// ============================================================================================================================================
// the index
// ============================================================================================================================================
uint32_t Impl::pool_add(const uint8_t *s, int n)
{
    if (n <= 0) {
        return 0;
    }
    if (pool_limit && plen + 1 + (uint32_t)n > pool_limit) {
        pool_bad = true;
        return 0;
    }
    if (plen + 1 + (uint32_t)n > pcap) {
        uint32_t nc = pcap ? pcap * 2 : 4096;
        while (nc < plen + 1 + (uint32_t)n) nc *= 2;
        uint8_t *q = (uint8_t *)mem_realloc(pool, pcap, nc);
        if (!q) {
            return 0xFFFFFFFFu;
        }
        pool = q;
        pcap = nc;
    }
    uint32_t off = plen;
    pool[plen++] = (uint8_t)n;
    memcpy(pool + plen, s, (size_t)n);
    plen += (uint32_t)n;
    return off;
}

bool Impl::node_add(Node **out, uint32_t *idx)
{
    if (nn >= ncap) {
        uint32_t nc = ncap + (ncap < 2048 ? (ncap ? ncap : 256) : 2048);
        Node *q = (Node *)mem_realloc(nodes, ncap * sizeof(Node), nc * sizeof(Node));
        if (!q) {
            return false;
        }
        nodes = q;
        ncap = nc;
    }
    memset(&nodes[nn], 0, sizeof(Node));
    nodes[nn].parent = nodes[nn].hparent = nodes[nn].next = FH_NONE16;
    *idx = nn;
    *out = &nodes[nn];
    nn++;
    return true;
}

// The host name of node idx: the stored one, else the FS-UAE encoding of its Amiga name.
void Impl::host_name(uint32_t idx, char *out, int n)
{
    const Node &nd = nodes[idx];
    if (nd.hname) {
        const uint8_t *s = pstr(nd.hname);
        folderhd_cpy(out, n, s + 1, s[0]);
        return;
    }
    const uint8_t *a = pstr(nd.name);
    if (folderhd_aname_to_host(a + 1, a[0], out, n) < 0) {
        out[0] = 0;
    }
}

// The host path of a node, by its host parents (the Amiga parents with host_parent = false). A node in limbo is below <root>/trx64.
bool Impl::path_of(uint32_t idx, char *out, int n, bool host_parent)
{
    uint16_t chain[40];
    int d = 0;
    uint32_t cur = idx;
    const char *prefix = base;
    while (cur != 0) {
        if (d >= 40) {
            return false;
        }
        chain[d++] = (uint16_t)cur;
        uint16_t p = host_parent ? nodes[cur].hparent : nodes[cur].parent;
        if (p == FH_LIMBO) {
            prefix = tmpdir;
            break;
        }
        if (p == FH_NONE16) {
            return false;
        }
        cur = p;
    }
    int o = snprintf(out, (size_t)n, "%s", prefix);
    for (int i = d - 1; i >= 0; i--) {
        char nm[160];
        host_name(chain[i], nm, sizeof(nm));
        if (!nm[0] || o + 1 + (int)strlen(nm) >= n) {
            return false;
        }
        o += snprintf(out + o, (size_t)(n - o), "/%s", nm);
    }
    return true;
}

// ---- the scan ----
namespace {

struct Cand {
    int e;                      // index into the listing
    uint8_t an[34];             // the Amiga name, long ones cut to 27 bytes
    uint8_t alen;
    uint8_t is_long;            // the host name decoded to more than 30 bytes
    uint8_t need_suffix;
    uint16_t sidecar;           // listing index of the .uaem + 1, 0 none
};

const FsEntryShort *g_ents;
int cmp_fold(const uint8_t *a, int al, const uint8_t *b, int bl)
{
    int n = al < bl ? al : bl;
    for (int i = 0; i < n; i++) {
        uint8_t x = ffs_upper(a[i]), y = ffs_upper(b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return al == bl ? 0 : (al < bl ? -1 : 1);
}
int cmp_group(const void *pa, const void *pb)
{
    const Cand *a = (const Cand *)pa, *b = (const Cand *)pb;
    int c = cmp_fold(a->an, a->alen, b->an, b->alen);
    if (c) return c;
    if (a->is_long != b->is_long) return a->is_long ? 1 : -1;               // a name that fits keeps its name, the cut ones are numbered
    return strcmp(g_ents[a->e].name, g_ents[b->e].name);
}
int cmp_final(const void *pa, const void *pb)
{
    const Cand *a = (const Cand *)pa, *b = (const Cand *)pb;
    return cmp_fold(a->an, a->alen, b->an, b->alen);
}
int cmp_uaem(const void *pa, const void *pb)
{
    return strcasecmp(g_ents[*(const int *)pa].name, g_ents[*(const int *)pb].name);
}

struct Report {
    char *out; int cap, len;
    uint32_t hidden, uaem, toolong, big, badname, internal, links;
    char first[6][60]; int nfirst;
    void note(uint32_t *counter, const char *what, const char *name)
    {
        (*counter)++;
        if (nfirst < 6) {
            snprintf(first[nfirst++], sizeof(first[0]), "%s: %s", what, name);
        }
    }
};

}   // namespace

static void parse_sidecar(const char *data, int len, uint8_t *protect, AmDate *date, bool *have_date, uint8_t *comment, int *clen)
{
    // FS-UAE od-fs/fsdb_host.cpp fsdb_get_file_info, lines 453-543: eight flag characters h s p a r w e d ('-' = clear), one separator, a date
    // "YYYY-MM-DD HH:MM:SS.hh " (TIME_LEN 22 must follow), the rest of the line is the comment.
    const char *p = data, *end = data + len;
    if (end - p >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) {
        p += 3;
    }
    *protect = 0;
    *have_date = false;
    *clen = 0;
    if (end - p >= 8) {
        uint8_t pr = 0;
        if (p[0] == 'h') pr |= 0x80;
        if (p[1] == 's') pr |= 0x40;
        if (p[2] == 'p') pr |= 0x20;
        if (p[3] == 'a') pr |= 0x10;
        if (p[4] != 'r') pr |= 0x08;            // rwed: the Amiga's bits are "denied"
        if (p[5] != 'w') pr |= 0x04;
        if (p[6] != 'e') pr |= 0x02;
        if (p[7] != 'd') pr |= 0x01;
        *protect = pr;
        p += 8;
    }
    p++;
    if (end - p >= 22) {
        const char *q = p;
        auto num = [](const char *s, int n, int *v) {
            int r = 0;
            for (int i = 0; i < n; i++) {
                if (s[i] < '0' || s[i] > '9') return false;
                r = r * 10 + (s[i] - '0');
            }
            *v = r;
            return true;
        };
        int y, mo, d, h, mi, s, hu;
        if (num(q, 4, &y) && num(q + 5, 2, &mo) && num(q + 8, 2, &d) && num(q + 11, 2, &h) && num(q + 14, 2, &mi) && num(q + 17, 2, &s) &&
            num(q + 20, 2, &hu) && ffs_date_from_civil(y, mo, d, h, mi, s, hu, date)) {
            *have_date = true;
        }
        p += 23 <= end - p ? 23 : end - p;
    }
    int n = 0;
    while (p < end && *p != '\r' && *p != '\n' && n < FFS_COMMENT_MAX) {
        comment[n++] = (uint8_t)*p++;
    }
    *clen = n;
}


static bool ends_uaem(const char *n)
{
    size_t l = strlen(n);
    return l >= 5 && !strcasecmp(n + l - 5, ".uaem");
}

bool Impl::scan(char *msg, int msglen, char *report, int replen)
{
    pool = (uint8_t *)mem_alloc(4096);
    if (!pool) {
        snprintf(msg, (size_t)msglen, "Out of memory.");
        return false;
    }
    pcap = 4096;
    pool[0] = 0;                                  // offset 0 is the empty string
    plen = 1;
    Node *rn;
    uint32_t ri;
    if (!node_add(&rn, &ri)) {
        snprintf(msg, (size_t)msglen, "Out of memory.");
        return false;
    }
    rn->flags = F_DIR;
    rn->parent = rn->hparent = 0;
    Report rep;
    memset(&rep, 0, sizeof(rep));
    bool base_is_root = !strcasecmp(base, root);
    FsEntryShort *ents = 0;
    int entcap = 0;
    bool ok = true;
    for (uint32_t qi = 0; qi < nn && ok; qi++) {
        if (!(nodes[qi].flags & F_DIR)) {
            continue;
        }
        char dpath[256];
        if (!path_of(qi, dpath, sizeof(dpath))) {
            snprintf(msg, (size_t)msglen, "A path in the folder is too long.");
            ok = false;
            break;
        }
        int cnt;
        for (;;) {
            if (!entcap) {
                entcap = 256;
                ents = (FsEntryShort *)mem_alloc((size_t)entcap * sizeof(FsEntryShort));
                if (!ents) { entcap = 0; break; }
            }
            cnt = fs->list_short(dpath, ents, entcap);
            if (cnt < entcap) {
                break;
            }
            int limit = FH_ENTRIES_MAX + 1024 + 1;
            if (entcap >= limit) {
                snprintf(msg, (size_t)msglen, "%s holds more than %d entries; the limit is %d.", dpath, limit - 1, FH_ENTRIES_MAX);
                ok = false;
                break;
            }
            int nc = entcap * 2 > limit ? limit : entcap * 2;
            FsEntryShort *q = (FsEntryShort *)mem_realloc(ents, (size_t)entcap * sizeof(FsEntryShort), (size_t)nc * sizeof(FsEntryShort));
            if (!q) { cnt = -2; break; }
            ents = q;
            entcap = nc;
        }
        if (!ok) {
            break;
        }
        if (cnt == -2 || !ents) {
            snprintf(msg, (size_t)msglen, "Out of memory.");
            ok = false;
            break;
        }
        if (cnt < 0) {
            if (qi == 0) {
                snprintf(msg, (size_t)msglen, "The folder cannot be read.");
                ok = false;
            } else {
                rep.note(&rep.badname, "folder not readable", dpath);
            }
            continue;
        }
        Cand *cands = (Cand *)mem_alloc((size_t)(cnt ? cnt : 1) * sizeof(Cand));
        int *uaem = (int *)mem_alloc((size_t)(cnt ? cnt : 1) * sizeof(int));
        if (!cands || !uaem) {
            mem_free(cands, (size_t)(cnt ? cnt : 1) * sizeof(Cand));
            mem_free(uaem, (size_t)(cnt ? cnt : 1) * sizeof(int));
            snprintf(msg, (size_t)msglen, "Out of memory.");
            ok = false;
            break;
        }
        int nc = 0, nu = 0;
        for (int e = 0; e < cnt; e++) {
            const FsEntryShort &fe = ents[e];
            const char *nm = fe.name;
            if (!strcmp(nm, ".") || !strcmp(nm, "..")) {
                continue;
            }
            if (nm[0] == '.') {
                rep.note(&rep.hidden, "hidden", nm);
                continue;
            }
            if (fe.attr & (FS_ATTR_HIDDEN | FS_ATTR_SYSTEM)) {
                rep.note(&rep.hidden, "hidden", nm);
                continue;
            }
            if (qi == 0 && base_is_root && fe.dir && !strcasecmp(nm, "trx64")) {
                rep.internal++;
                continue;
            }
            if (!fe.dir && ends_uaem(nm)) {
                uaem[nu++] = e;
                continue;
            }
            if (fe.cut || strlen(nm) >= sizeof(fe.name) - 1) {
                rep.note(&rep.toolong, "name too long for the listing", nm);
                continue;
            }
            if (!fe.dir && fe.size >= 0x100000000ull) {
                rep.note(&rep.big, "file of 4 GiB or more", nm);
                continue;
            }
            Cand c;
            memset(&c, 0, sizeof(c));
            uint8_t full[128];
            int al = folderhd_host_to_aname(nm, full, sizeof(full));
            bool bad = al <= 0;
            for (int i = 0; i < al && !bad; i++) {
                if (full[i] == ':' || full[i] == '/') bad = true;
            }
            if (bad) {
                rep.note(&rep.badname, al <= 0 ? "name not representable on the Amiga" : "name with ':' or '/'", nm);
                continue;
            }
            c.e = e;
            c.is_long = al > FFS_NAME_MAX;
            c.alen = (uint8_t)(c.is_long ? 27 : al);
            memcpy(c.an, full, c.alen);
            cands[nc++] = c;
        }
        // the entries are counted before anything is added
        if ((uint64_t)(nn - 1) + (uint64_t)nc > FH_ENTRIES_MAX) {
            snprintf(msg, (size_t)msglen, "The folder holds at least %u entries; the limit is %u.", (unsigned)(nn - 1 + nc), (unsigned)FH_ENTRIES_MAX);
            ok = false;
        }
        if (ok) {
            g_ents = ents;
            qsort(uaem, (size_t)nu, sizeof(int), cmp_uaem);
            qsort(cands, (size_t)nc, sizeof(Cand), cmp_group);
            // names that clash after case folding or after the cut: all but the first get ~NN (the plain names win the clash)
            uint8_t *suf = (uint8_t *)mem_alloc((size_t)(nc ? nc : 1));
            if (!suf) {
                ok = false;
                snprintf(msg, (size_t)msglen, "Out of memory.");
            } else {
                memset(suf, 0, (size_t)(nc ? nc : 1));
                for (int i = 0; i < nc;) {
                    int j = i + 1;
                    while (j < nc && !cmp_fold(cands[i].an, cands[i].alen, cands[j].an, cands[j].alen)) j++;
                    bool plain_seen = false;
                    int n = 1;
                    for (int k = i; k < j; k++) {
                        if (!cands[k].is_long && !plain_seen) {
                            plain_seen = true;
                            continue;
                        }
                        for (;; n++) {
                            uint8_t cn[34];
                            int bl = cands[k].alen > 27 ? 27 : cands[k].alen;
                            memcpy(cn, cands[k].an, (size_t)bl);
                            cn[bl] = '~';
                            cn[bl + 1] = (uint8_t)('0' + (n / 10) % 10);
                            cn[bl + 2] = (uint8_t)('0' + n % 10);
                            int lo = 0, hi = nc - 1;
                            bool clash = false;
                            while (lo <= hi) {
                                int mid = (lo + hi) / 2;
                                int c = cmp_fold(cands[mid].an, cands[mid].alen, cn, bl + 3);
                                if (c == 0) { while (mid > 0 && !cmp_fold(cands[mid - 1].an, cands[mid - 1].alen, cn, bl + 3)) mid--; clash = !cands[mid].is_long; break; }
                                if (c < 0) lo = mid + 1; else hi = mid - 1;
                            }
                            if (!clash) break;
                        }
                        suf[k] = (uint8_t)n;
                        n++;
                    }
                    i = j;
                }
                for (int k = 0; k < nc; k++) {
                    if (suf[k]) {
                        int bl = cands[k].alen > 27 ? 27 : cands[k].alen;
                        cands[k].an[bl] = '~';
                        cands[k].an[bl + 1] = (uint8_t)('0' + (suf[k] / 10) % 10);
                        cands[k].an[bl + 2] = (uint8_t)('0' + suf[k] % 10);
                        cands[k].alen = (uint8_t)(bl + 3);
                    }
                }
                mem_free(suf, (size_t)(nc ? nc : 1));
                qsort(cands, (size_t)nc, sizeof(Cand), cmp_final);
            }
        }
        uint32_t first = nn;
        for (int k = 0; ok && k < nc; k++) {
            const FsEntryShort &fe = ents[cands[k].e];
            Node *nd;
            uint32_t idx;
            if (!node_add(&nd, &idx)) {
                snprintf(msg, (size_t)msglen, "Out of memory.");
                ok = false;
                break;
            }
            nd->flags = fe.dir ? F_DIR : 0;
            nd->parent = nd->hparent = (uint16_t)qi;
            nd->size = fe.dir ? 0 : (uint32_t)fe.size;
            nd->name = pool_add(cands[k].an, cands[k].alen);
            char enc[160];
            if (folderhd_aname_to_host(cands[k].an, cands[k].alen, enc, sizeof(enc)) < 0 || strcmp(enc, fe.name)) {
                nd->hname = pool_add((const uint8_t *)fe.name, (int)strlen(fe.name));
            }
            AmDate d = ffs_date_from_fat(fe.fdate, fe.ftime);
            nd->protect = (fe.attr & FS_ATTR_RDONLY) ? 0x05 : 0;
            // the side file
            char key[96];
            snprintf(key, sizeof(key), "%s.uaem", fe.name);
            int lo = 0, hi = nu - 1, found = -1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                int c = strcasecmp(ents[uaem[mid]].name, key);
                if (c == 0) { found = uaem[mid]; break; }
                if (c < 0) lo = mid + 1; else hi = mid - 1;
            }
            if (found >= 0) {
                char sp[320], data[400];
                snprintf(sp, sizeof(sp), "%s/%s", dpath, ents[found].name);
                int dl = fs->load(sp, data, sizeof(data));
                if (dl > 0) {
                    uint8_t pr, com[FFS_COMMENT_MAX + 1];
                    int cl;
                    AmDate sd;
                    bool hd;
                    parse_sidecar(data, dl, &pr, &sd, &hd, com, &cl);
                    nd->protect = pr;
                    if (hd) d = sd;
                    if (cl) nd->comment = pool_add(com, cl);
                }
            }
            nd->days = (uint16_t)d.days;
            nd->mins = (uint16_t)d.mins;
            nd->ticks = (uint16_t)d.ticks;
            if (nd->name == 0xFFFFFFFFu || nd->hname == 0xFFFFFFFFu) {
                snprintf(msg, (size_t)msglen, "Out of memory.");
                ok = false;
            }
        }
        if (ok) {
            nodes[qi].first = first;
            nodes[qi].nchild = (uint16_t)(nn - first);
            uint16_t last[FFS_HT];
            for (int s = 0; s < FFS_HT; s++) last[s] = FH_NONE16;
            for (uint32_t c = first; c < nn; c++) {
                const uint8_t *a = pstr(nodes[c].name);
                uint32_t s = ffs_hash(a + 1, a[0]);
                if (last[s] != FH_NONE16) nodes[last[s]].next = (uint16_t)c;
                last[s] = (uint16_t)c;
            }
            if (index_bytes() > FH_INDEX_BYTES) {
                snprintf(msg, (size_t)msglen, "The index would need %u bytes (%u entries); the budget is %u bytes.", (unsigned)index_bytes(), (unsigned)(nn - 1), (unsigned)FH_INDEX_BYTES);
                ok = false;
            }
        }
        mem_free(cands, (size_t)(cnt ? cnt : 1) * sizeof(Cand));
        mem_free(uaem, (size_t)(cnt ? cnt : 1) * sizeof(int));
    }
    mem_free(ents, (size_t)entcap * sizeof(FsEntryShort));
    // the report
    int o = 0;
    auto add = [&](const char *fmt, unsigned v) { if (v && o < replen - 1) o += snprintf(report + o, (size_t)(replen - o), fmt, v); };
    report[0] = 0;
    add("%u hidden; ", rep.hidden);
    add("%u 4 GiB or larger; ", rep.big);
    add("%u names too long; ", rep.toolong);
    add("%u names the Amiga cannot hold; ", rep.badname);
    for (int i = 0; i < rep.nfirst && o < replen - 1; i++) {
        o += snprintf(report + o, (size_t)(replen - o), "[%s] ", rep.first[i]);
    }
    st->skipped = rep.hidden + rep.big + rep.toolong + rep.badname;
    return ok;
}

// ---- the geometry and the layout ----
static uint32_t bm_count(uint32_t nblk) { return nblk > 2 ? (nblk - 2 + FFS_BM_BLOCKS - 1) / FFS_BM_BLOCKS : 0; }
static uint32_t bmx_count(uint32_t nbm) { return nbm > FFS_BM_PER_ROOT ? (nbm - FFS_BM_PER_ROOT + FFS_BM_EXT_PTRS - 1) / FFS_BM_EXT_PTRS : 0; }

bool Impl::layout(const FolderDisk::Cfg &cfg, char *msg, int msglen)
{
    nbase = nn;
    uint64_t used = 0;
    for (uint32_t i = 1; i < nn; i++) {
        used += span_len(nodes[i]);
    }
    uint32_t reach = cfg.max_cylinders > FH_MAX_CYL ? FH_MAX_CYL : cfg.max_cylinders;
    // the smallest legal partition: the tree, the root cluster, the two boot blocks, whole cylinders
    uint64_t N = 0;
    for (int it = 0; it < 6; it++) {
        uint32_t nb = bm_count((uint32_t)(N > 0xFFFFFFFFull ? 0xFFFFFFFFu : N));
        uint64_t need = 2 + used + 1 + nb + bmx_count(nb);
        uint64_t n2 = (need + FFS_CYL_BLOCKS - 1) / FFS_CYL_BLOCKS * FFS_CYL_BLOCKS;
        if (n2 == N) break;
        N = n2;
    }
    uint32_t pc_min = (uint32_t)(N / FFS_CYL_BLOCKS);
    if (N / FFS_CYL_BLOCKS + 1 > reach) {
        snprintf(msg, (size_t)msglen, "The folder holds %u MB; this machine reaches %u MB.", (unsigned)(used * 512 >> 20),
                 (unsigned)((uint64_t)(reach - 1) * FFS_CYL_BLOCKS * 512 >> 20));
        return false;
    }
    uint64_t pc_req = cfg.want_bytes / ((uint64_t)FFS_CYL_BLOCKS * FFS_BLOCK);
    if (pc_req < pc_min) {
        snprintf(msg, (size_t)msglen, "The folder needs a disk of at least %u MB (%u MB is set).", (unsigned)((uint64_t)pc_min * FFS_CYL_BLOCKS * 512 >> 20),
                 (unsigned)(cfg.want_bytes >> 20));
        return false;
    }
    uint64_t fb = 0;
    uint64_t back = 0;
    if (fs->free_bytes(root, &fb) && fb > FH_RESERVE_BYTES) {
        back = (fb - FH_RESERVE_BYTES) / FFS_BLOCK;
    }
    uint64_t pc_back = (pc_min * (uint64_t)FFS_CYL_BLOCKS + back) / FFS_CYL_BLOCKS;
    uint64_t pc = pc_req < pc_back ? pc_req : pc_back;
    if (pc > (uint64_t)reach - 1) pc = reach - 1;
    if (pc < pc_min) pc = pc_min;
    for (;; pc++) {
        if (pc + 1 > reach) {
            snprintf(msg, (size_t)msglen, "The folder does not fit this machine's disk limit.");
            return false;
        }
        nblk = (uint32_t)(pc * FFS_CYL_BLOCKS);
        nbm = bm_count(nblk);
        nbmext = bmx_count(nbm);
        K = 1 + nbm + nbmext;
        R = nblk / 2;
        uint64_t cur = 2;
        for (uint32_t i = 1; i < nn; i++) {
            uint32_t len = span_len(nodes[i]);
            if (cur < (uint64_t)R + K && cur + len > R) {
                cur = (uint64_t)R + K;
            }
            nodes[i].hdr = (uint32_t)cur;
            cur += len;
        }
        if (cur <= nblk) {
            st->used_blocks = (uint32_t)(cur - 2);
            break;
        }
    }
    nodes[0].hdr = R;
    for (uint32_t i = 1; i < nn; i++) nodes[i].hsize = nodes[i].size;
    cyls = (uint32_t)pc + 1;
    size_bytes = (uint64_t)cyls * FFS_CYL_BLOCKS * FFS_BLOCK;
    AmDate d = { 0, 0, 0 };
    for (uint32_t i = 1; i < nn; i++) {
        if (nodes[i].days > d.days || (nodes[i].days == d.days && nodes[i].mins > d.mins)) {
            d.days = nodes[i].days;
            d.mins = nodes[i].mins;
            d.ticks = nodes[i].ticks;
        }
    }
    vol_date = d;
    return true;
}

// ============================================================================================================================================
// synthesis: every block of the partition is a function of the index
// ============================================================================================================================================
// The base node whose span holds block b, or -1.
int Impl::span_find(uint32_t b)
{
    int lo = 1, hi = (int)nbase - 1, r = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (nodes[mid].hdr <= b) { r = mid; lo = mid + 1; } else { hi = mid - 1; }
    }
    if (r < 0 || b >= nodes[r].hdr + span_len(nodes[r])) {
        return -1;
    }
    return r;
}

void Impl::obj_of(uint32_t i, FfsObj *o)
{
    const Node &nd = nodes[i];
    const uint8_t *a = pstr(nd.name);
    o->name = a + 1;
    o->nlen = a[0];
    const uint8_t *c = pstr(nd.comment);
    o->comment = c + 1;
    o->clen = nd.comment ? c[0] : 0;
    o->protect = nd.protect;
    o->date.days = nd.days;
    o->date.mins = nd.mins;
    o->date.ticks = nd.ticks;
    o->size = nd.size;
    o->parent = nd.parent == 0 ? R : nodes[nd.parent].hdr;
    o->hash_next = nd.next == FH_NONE16 ? 0 : nodes[nd.next].hdr;
}

void Impl::dir_heads(uint32_t i, uint32_t heads[FFS_HT])
{
    memset(heads, 0, FFS_HT * sizeof(uint32_t));
    const Node &d = nodes[i];
    for (uint32_t c = d.first; c < d.first + d.nchild; c++) {
        const uint8_t *a = pstr(nodes[c].name);
        uint32_t s = ffs_hash(a + 1, a[0]);
        if (!heads[s]) heads[s] = nodes[c].hdr;
    }
}

void Impl::bitmap_block(uint32_t k, uint8_t *out)
{
    uint32_t lo = 2 + k * FFS_BM_BLOCKS, hi = lo + FFS_BM_BLOCKS;
    if (hi > nblk) hi = nblk;
    uint32_t w[FFS_BM_PER_BLOCK];
    memset(w, 0, sizeof(w));
    for (uint32_t b = lo; b < hi;) {                 // every block of the window is free first; whole words where possible
        uint32_t i = (b - lo) / 32;
        if ((b - lo) % 32 == 0 && b + 32 <= hi) { w[i] = 0xFFFFFFFFu; b += 32; }
        else { w[i] |= 1u << ((b - lo) % 32); b++; }
    }
    auto clear = [&](uint32_t s, uint32_t n) {
        uint32_t e = s + n;
        if (s < lo) s = lo;
        if (e > hi) e = hi;
        for (uint32_t b = s; b < e;) {
            uint32_t i = (b - lo) / 32;
            if ((b - lo) % 32 == 0 && b + 32 <= e) { w[i] = 0; b += 32; }
            else { w[i] &= ~(1u << ((b - lo) % 32)); b++; }
        }
    };
    clear(R, K);
    int lo2 = 1, hi2 = (int)nbase - 1, r = (int)nbase;     // the first span that ends after the window starts
    while (lo2 <= hi2) {
        int mid = (lo2 + hi2) / 2;
        if (nodes[mid].hdr + span_len(nodes[mid]) > lo) { r = mid; hi2 = mid - 1; } else { lo2 = mid + 1; }
    }
    for (int i = r; i >= 1 && i < (int)nbase && nodes[i].hdr < hi; i++) {
        clear(nodes[i].hdr, span_len(nodes[i]));
    }
    ffs_build_bitmap(out, w);
}

void Impl::synth_root_cluster(uint32_t b, uint8_t *out)
{
    uint32_t off = b - R;
    if (off == 0) {
        uint32_t heads[FFS_HT];
        dir_heads(0, heads);
        uint32_t pages[FFS_BM_PER_ROOT];
        int np = nbm < FFS_BM_PER_ROOT ? (int)nbm : FFS_BM_PER_ROOT;
        for (int k = 0; k < np; k++) pages[k] = R + 1 + k;
        ffs_build_root(out, volname, vol_date, heads, 0xFFFFFFFFu, pages, np, nbmext ? R + 1 + nbm : 0);
    } else if (off <= nbm) {
        bitmap_block(off - 1, out);
    } else {
        uint32_t j = off - 1 - nbm;
        uint32_t ptrs[FFS_BM_EXT_PTRS];
        int n = 0;
        for (uint32_t k = FFS_BM_PER_ROOT + j * FFS_BM_EXT_PTRS; k < nbm && n < FFS_BM_EXT_PTRS; k++) ptrs[n++] = R + 1 + k;
        ffs_build_bm_ext(out, ptrs, n, j + 1 < nbmext ? b + 1 : 0);
    }
}

bool Impl::data_read(uint32_t i, uint32_t fblk, uint8_t *out)
{
    memset(out, 0, FFS_BLOCK);
    const Node &nd = nodes[i];
    if ((nd.flags & F_DEAD) || (uint64_t)fblk * FFS_BLOCK >= nd.hsize) {
        return false;
    }
    uint32_t n = nd.hsize - fblk * FFS_BLOCK;
    if (n > FFS_BLOCK) n = FFS_BLOCK;
    Handle *h = hopen(i, false);
    if (!h || !hread(h, (uint64_t)fblk * FFS_BLOCK, out, n)) {
        memset(out, 0, FFS_BLOCK);
        static uint32_t said;
        if (said++ < 20) {                                  // a file that went from the medium would make a line for every request
            printf("[folderhd] unit %d: cannot read block %u of node %u\n", unit, (unsigned)fblk, (unsigned)i);
        }
        return false;
    }
    return true;
}

void Impl::synth_node(uint32_t i, uint32_t off, uint8_t *out)
{
    const Node &nd = nodes[i];
    FfsObj o;
    obj_of(i, &o);
    if (nd.flags & F_DIR) {
        uint32_t heads[FFS_HT];
        dir_heads(i, heads);
        ffs_build_dir(out, nd.hdr, o, heads);
        return;
    }
    uint32_t nd_ = file_blocks(nd), ne = file_ext(nd);
    if (off == 0) {
        ffs_build_file(out, nd.hdr, o, nd.hdr + 1, nd_, nd_ > FFS_HT ? nd.hdr + 1 + nd_ : 0);
    } else if (off <= nd_) {
        data_read(i, off - 1, out);
    } else {
        uint32_t e = off - 1 - nd_;
        ffs_build_ext(out, nd.hdr + off, nd.hdr, nd.hdr + 1, nd_, e, e + 1 < ne ? nd.hdr + off + 1 : 0);
    }
}

void Impl::synth(uint32_t b, uint8_t *out, bool *known)
{
    *known = true;
    if (b < 2) {
        uint8_t boot[1024];
        ffs_build_boot(boot, R);
        memcpy(out, boot + b * FFS_BLOCK, FFS_BLOCK);
        return;
    }
    if (b >= R && b < R + K) {
        synth_root_cluster(b, out);
        return;
    }
    int x = xr_find(b);
    if (x >= 0) {
        const XRun &r = xr[x];
        data_read(r.node, r.fblk + (b - r.start), out);
        return;
    }
    int i = span_find(b);
    if (i >= 0) {
        uint32_t off = b - nodes[i].hdr;
        if (off == 0 && (nodes[i].flags & F_DIR) && b == memo_blk) { memcpy(out, memo, FFS_BLOCK); return; }
        synth_node((uint32_t)i, off, out);
        if (off == 0 && (nodes[i].flags & F_DIR)) {
            memo_blk = b;
            memcpy(memo, out, FFS_BLOCK);
        }
        return;
    }
    memset(out, 0, FFS_BLOCK);
    *known = false;
}

bool Impl::view_read(uint32_t b, uint8_t *out)
{
    if (b >= nblk) {
        memset(out, 0, FFS_BLOCK);
        return false;
    }
    if (delta_read(b, out)) {
        return true;
    }
    bool known;
    synth(b, out, &known);
    return true;
}

// ============================================================================================================================================
// handles: a small table of open host files, least recently used closed first
// ============================================================================================================================================
Handle *Impl::hopen(uint32_t node, bool rw)
{
    Handle *free_h = 0, *lru_h = 0;
    for (int i = 0; i < FH_HANDLES; i++) {
        Handle *h = &hd[i];
        if (h->node == (int32_t)node && h->f) {
            if (rw && !h->rw) {
                hclose(h);                          // reopened for writing
                break;
            }
            h->lru = ++lru_clock;
            return h;
        }
    }
    for (int i = 0; i < FH_HANDLES; i++) {
        Handle *h = &hd[i];
        if (!h->f) {
            if (!free_h) free_h = h;
        } else if (!lru_h || h->lru < lru_h->lru) {
            lru_h = h;
        }
    }
    Handle *h = free_h;
    if (!h) {
        hclose(lru_h);
        h = lru_h;
    }
    char path[300];
    if (!path_of(node, path, sizeof(path))) {
        return 0;
    }
    CoreFile *f = fs->open(path, rw && host_rw);
    if (!f) {
        return 0;
    }
    h->f = f;
    h->node = (int32_t)node;
    h->rw = rw && f->writable();
    h->wrote = false;
    h->ra_len = 0;
    h->next_off = ~0ull;
    h->lru = ++lru_clock;
    st->handle_opens++;
    uint32_t open = 0;
    for (int i = 0; i < FH_HANDLES; i++) open += hd[i].f != 0;
    st->handles_open = open;
    if (open > st->handle_peak) st->handle_peak = open;
    return h;
}

void Impl::hclose(Handle *h)
{
    if (!h->f) {
        return;
    }
    if (h->wrote) {
        h->f->sync();
    }
    fs->close(h->f);
    h->f = 0;
    h->node = -1;
    h->wrote = false;
    h->ra_len = 0;
    uint32_t open = 0;
    for (int i = 0; i < FH_HANDLES; i++) open += hd[i].f != 0;
    st->handles_open = open;
}

void Impl::hclose_all()
{
    for (int i = 0; i < FH_HANDLES; i++) {
        hclose(&hd[i]);
    }
}

bool Impl::hsync_all()
{
    bool ok = true;
    for (int i = 0; i < FH_HANDLES; i++) {
        if (hd[i].f && hd[i].wrote) {
            ok = hd[i].f->sync() && ok;
            hd[i].wrote = false;
        }
    }
    return ok;
}

bool Impl::hread(Handle *h, uint64_t off, uint8_t *buf, uint32_t n)
{
    bool sequential = off == h->next_off;                   // the read-ahead is for a file that is read on from where it was: a random request reads its own sector
    h->next_off = off + n;
    if (h->ra_len && off >= h->ra_off && off + n <= h->ra_off + h->ra_len) {
        memcpy(buf, h->ra + (off - h->ra_off), n);
        st->ra_hits++;
        return true;
    }
    uint64_t fsz = h->f->size();
    if (off >= fsz || n > fsz - off) {
        return false;
    }
    uint32_t want = n;
    if (sequential) {
        want = (uint32_t)(fsz - off < FH_RA_BYTES ? fsz - off : FH_RA_BYTES);
    }
    if (want > n && !h->ra) {
        h->ra = (uint8_t *)mem_alloc(FH_RA_BYTES);
    }
    if (want > n && h->ra && h->f->read(off, h->ra, want)) {
        h->ra_off = off;
        h->ra_len = want;
        memcpy(buf, h->ra, n);
        st->ra_reads++;
        return true;
    }
    h->ra_len = 0;
    return h->f->read(off, buf, n);
}

// ============================================================================================================================================
// what the Amiga wrote: the overlay (heap, metadata), the scratch runs (data), the folded-back data (xruns)
// ============================================================================================================================================
int Impl::ov_find(uint32_t b)
{
    int lo = 0, hi = (int)nov - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ov[mid].blk == b) return mid;
        if (ov[mid].blk < b) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

bool Impl::ov_put(uint32_t b, const uint8_t *data)
{
    int f = ov_find(b);
    if (f >= 0) {
        memcpy(ov[f].data, data, FFS_BLOCK);
        return true;
    }
    if (nov >= FH_OVERLAY_BLOCKS) {
        return false;
    }
    if (nov == ov_cap) {
        uint32_t nc = ov_cap + 64;
        OvBlk *q = (OvBlk *)mem_realloc(ov, ov_cap * sizeof(OvBlk), nc * sizeof(OvBlk));
        if (!q) return false;
        ov = q;
        ov_cap = nc;
    }
    uint8_t *d = (uint8_t *)mem_alloc(FFS_BLOCK);
    if (!d) return false;
    memcpy(d, data, FFS_BLOCK);
    int lo = 0, hi = (int)nov;
    while (lo < hi) { int mid = (lo + hi) / 2; if (ov[mid].blk < b) lo = mid + 1; else hi = mid; }
    memmove(&ov[lo + 1], &ov[lo], (nov - (uint32_t)lo) * sizeof(OvBlk));
    ov[lo].blk = b;
    ov[lo].data = d;
    nov++;
    st->overlay_blocks = nov;
    return true;
}

bool Impl::sr_find(uint32_t b, uint32_t *slot)
{
    int lo = 0, hi = (int)nsr - 1, r = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (sr[mid].start <= b) { r = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (r < 0 || b >= sr[r].start + sr[r].count) {
        return false;
    }
    *slot = sr[r].slot + (b - sr[r].start);
    return true;
}

bool Impl::scratch_open()
{
    if (scratch) {
        return true;
    }
    if (!host_rw) {
        return false;
    }
    fs->mkdir(tmpdir);
    if (!fs->save(scratch_path, "", 0)) {
        return false;
    }
    scratch = fs->open(scratch_path, true);
    if (!scratch || !scratch->writable()) {
        if (scratch) { fs->close(scratch); scratch = 0; }
        return false;
    }
    sslots = 0;
    return true;
}

void Impl::scratch_reset()
{
    if (scratch) {
        fs->close(scratch);
        scratch = 0;
    }
    fs->remove(scratch_path);
    sslots = 0;
    smeta = 0;
}

bool Impl::sr_put(uint32_t b, const uint8_t *data)
{
    uint32_t slot;
    if (sr_find(b, &slot)) {
        return scratch && scratch->write((uint64_t)slot * FFS_BLOCK, data, FFS_BLOCK);
    }
    if (nsr >= FH_SRUNS_MAX || !scratch_open()) {
        return false;
    }
    if ((uint64_t)sslots * FFS_BLOCK != scratch->size() && scratch->size() < (uint64_t)sslots * FFS_BLOCK) {
        return false;
    }
    if (!scratch->write((uint64_t)sslots * FFS_BLOCK, data, FFS_BLOCK)) {
        return false;
    }
    uint32_t s = sslots++;
    int lo = 0, hi = (int)nsr;
    while (lo < hi) { int mid = (lo + hi) / 2; if (sr[mid].start < b) lo = mid + 1; else hi = mid; }
    if (lo > 0 && sr[lo - 1].start + sr[lo - 1].count == b && sr[lo - 1].slot + sr[lo - 1].count == s) {
        sr[lo - 1].count++;
        return true;
    }
    if (nsr == sr_cap) {
        SRun *q = (SRun *)mem_realloc(sr, sr_cap * sizeof(SRun), (sr_cap + 256) * sizeof(SRun));
        if (!q) return false;
        sr = q;
        sr_cap += 256;
    }
    memmove(&sr[lo + 1], &sr[lo], (nsr - (uint32_t)lo) * sizeof(SRun));
    sr[lo].start = b;
    sr[lo].count = 1;
    sr[lo].slot = s;
    nsr++;
    st->scratch_runs = nsr;
    return true;
}

// Takes [start, start+count) out of the scratch runs (the data is on the host now). Slots stay until the scratch is reset.
void Impl::sr_remove_range(uint32_t start, uint32_t count)
{
    uint32_t end = start + count;
    for (uint32_t i = 0; i < nsr;) {
        SRun r = sr[i];
        uint32_t re = r.start + r.count;
        if (re <= start || r.start >= end) { i++; continue; }
        if (r.start >= start && re <= end) {          // whole run
            memmove(&sr[i], &sr[i + 1], (nsr - i - 1) * sizeof(SRun));
            nsr--;
            continue;
        }
        if (r.start < start && re > end) {            // split: needs one more entry
            if (nsr >= FH_SRUNS_MAX) { i++; continue; }
            if (nsr == sr_cap) {
                SRun *q = (SRun *)mem_realloc(sr, sr_cap * sizeof(SRun), (sr_cap + 256) * sizeof(SRun));
                if (!q) { i++; continue; }
                sr = q;
                sr_cap += 256;
            }
            memmove(&sr[i + 2], &sr[i + 1], (nsr - i - 1) * sizeof(SRun));
            sr[i].count = start - r.start;
            sr[i + 1].start = end;
            sr[i + 1].count = re - end;
            sr[i + 1].slot = r.slot + (end - r.start);
            nsr++;
            i += 2;
            continue;
        }
        if (r.start < start) {                         // keep the head
            sr[i].count = start - r.start;
        } else {                                       // keep the tail
            sr[i].slot = r.slot + (end - r.start);
            sr[i].start = end;
            sr[i].count = re - end;
        }
        i++;
    }
    st->scratch_runs = nsr;
}

bool Impl::delta_has(uint32_t b)
{
    uint32_t s;
    return ov_find(b) >= 0 || sr_find(b, &s);
}

bool Impl::delta_read(uint32_t b, uint8_t *out)
{
    int o = ov_find(b);
    if (o >= 0) {
        memcpy(out, ov[o].data, FFS_BLOCK);
        return true;
    }
    uint32_t slot;
    if (sr_find(b, &slot) && scratch) {
        if (!scratch->read((uint64_t)slot * FFS_BLOCK, out, FFS_BLOCK)) {
            printf("[folderhd] unit %d: scratch read of block %u failed\n", unit, (unsigned)b);
            memset(out, 0, FFS_BLOCK);
        }
        return true;
    }
    return false;
}

int Impl::xr_find(uint32_t b)
{
    int lo = 0, hi = (int)nxr - 1, r = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (xr[mid].start <= b) { r = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (r < 0 || b >= xr[r].start + xr[r].count) {
        return -1;
    }
    return r;
}

// Records that blocks start .. start+count-1 are the data blocks fblk .. of the host file of `node`. Older runs the new one overlaps are cut.
bool Impl::xr_insert(uint32_t start, uint32_t count, uint32_t fblk, uint32_t node)
{
    uint32_t end = start + count;
    for (uint32_t i = 0; i < nxr;) {
        XRun r = xr[i];
        uint32_t re = r.start + r.count;
        if (re <= start || r.start >= end) { i++; continue; }
        if (r.start >= start && re <= end) {
            memmove(&xr[i], &xr[i + 1], (nxr - i - 1) * sizeof(XRun));
            nxr--;
            continue;
        }
        if (r.start < start && re > end) {
            if (nxr >= FH_XRUNS_MAX) return false;
            if (nxr == xr_cap) {
                XRun *q = (XRun *)mem_realloc(xr, xr_cap * sizeof(XRun), (xr_cap + 256) * sizeof(XRun));
                if (!q) return false;
                xr = q;
                xr_cap += 256;
            }
            memmove(&xr[i + 2], &xr[i + 1], (nxr - i - 1) * sizeof(XRun));
            xr[i].count = start - r.start;
            xr[i + 1].start = end;
            xr[i + 1].count = re - end;
            xr[i + 1].fblk = r.fblk + (end - r.start);
            xr[i + 1].node = r.node;
            nxr++;
            i += 2;
            continue;
        }
        if (r.start < start) {
            xr[i].count = start - r.start;
        } else {
            xr[i].fblk = r.fblk + (end - r.start);
            xr[i].start = end;
            xr[i].count = re - end;
        }
        i++;
    }
    int lo = 0, hi = (int)nxr;
    while (lo < hi) { int mid = (lo + hi) / 2; if (xr[mid].start < start) lo = mid + 1; else hi = mid; }
    if (lo > 0 && xr[lo - 1].node == node && xr[lo - 1].start + xr[lo - 1].count == start && xr[lo - 1].fblk + xr[lo - 1].count == fblk) {
        xr[lo - 1].count += count;
        st->xruns = nxr;
        return true;
    }
    if (nxr >= FH_XRUNS_MAX) {
        return false;
    }
    if (nxr == xr_cap) {
        XRun *q = (XRun *)mem_realloc(xr, xr_cap * sizeof(XRun), (xr_cap + 256) * sizeof(XRun));
        if (!q) return false;
        xr = q;
        xr_cap += 256;
    }
    memmove(&xr[lo + 1], &xr[lo], (nxr - (uint32_t)lo) * sizeof(XRun));
    xr[lo].start = start;
    xr[lo].count = count;
    xr[lo].fblk = fblk;
    xr[lo].node = node;
    nxr++;
    st->xruns = nxr;
    return true;
}

int Impl::created_find(uint32_t hdr)
{
    int lo = 0, hi = (int)ncre - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t h = nodes[cre[mid]].hdr;
        if (h == hdr) return cre[mid];
        if (h < hdr) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

int Impl::node_by_hdr(uint32_t hdr)
{
    if (hdr == R) return 0;
    int lo = 1, hi = (int)nbase - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (nodes[mid].hdr == hdr) { if (!(nodes[mid].flags & F_DEAD)) return mid; break; }
        if (nodes[mid].hdr < hdr) lo = mid + 1; else hi = mid - 1;
    }
    return created_find(hdr);
}

// ============================================================================================================================================
// the write path
// ============================================================================================================================================
#if FH_STRAIGHT_WRITES
// T128 D5 as first proposed and withdrawn (spec section 5.5 no. 2): a data write into an unchanged, still linked file went straight into the host file. The ownership of a
// block is only known when the Amiga's metadata is, and FFS writes data first - a freed block that is reused wrote into the host file of the file that gave it up. Kept
// switched off for the negative control of the bench.
// Is the file i, in the Amiga's view, still the file it was at mount (header untouched, still linked into its directory)?
bool Impl::reachable(uint32_t i)
{
    uint32_t cur = i;
    uint8_t blk[FFS_BLOCK];
    for (int depth = 0; depth < 40; depth++) {
        const Node &nd = nodes[cur];
        uint32_t pb = nd.parent == 0 ? R : nodes[nd.parent].hdr;
        if (!view_read(pb, blk)) {
            return false;
        }
        const uint8_t *a = pstr(nd.name);
        uint32_t b = ffs_rd(blk, FL_TABLE + (int)ffs_hash(a + 1, a[0]));
        bool found = false;
        for (int guard = 0; b && guard < 20000; guard++) {
            if (b == nd.hdr) { found = true; break; }
            if (b >= nblk || !view_read(b, blk) || !ffs_sum_ok(blk, FFS_LONGS)) break;
            b = ffs_rd(blk, FL_HASHCHAIN);
        }
        if (!found) {
            return false;
        }
        if (nd.parent == 0) {
            return true;
        }
        cur = nd.parent;
    }
    return false;
}

bool Impl::stable(uint32_t i)
{
    Stab &s = stab[i % 16];
    if (s.node == (int32_t)i && s.gen == meta_gen) {
        return s.ok;
    }
    const Node &nd = nodes[i];
    bool ok = !(nd.flags & F_DEAD) && !delta_has(nd.hdr) && reachable(i);
    s.node = (int32_t)i;
    s.gen = meta_gen;
    s.ok = ok;
    return ok;
}

bool Impl::straight(uint32_t i, uint32_t fblk, const uint8_t *buf)
{
    const Node &nd = nodes[i];
    if ((uint64_t)(fblk + 1) * FFS_BLOCK > nd.hsize) {
        return false;                               // the last, partial block: what lies beyond the end of this file may belong to another
    }
    if (!stable(i)) {
        return false;
    }
    Handle *h = hopen(i, true);
    if (!h || !h->rw) {
        return false;
    }
    if (!h->f->write((uint64_t)fblk * FFS_BLOCK, buf, FFS_BLOCK)) {
        printf("[folderhd] unit %d: write of block %u of node %u failed\n", unit, (unsigned)fblk, (unsigned)i);
        return false;
    }
    h->wrote = true;
    h->ra_len = 0;
    st->straight_writes++;
    return true;
}

#endif

bool Impl::ro_put(uint32_t b, const uint8_t *buf)
{
    st->ro_writes++;
    if (ov_put(b, buf)) {
        gen++;
        meta_gen++;
        return true;
    }
    st->dropped_writes++;
    if (!ov_full_said) {
        ov_full_said = true;
        printf("[folderhd] unit %d: read only medium and the overlay (%u blocks) is full: further writes are lost\n", unit, (unsigned)FH_OVERLAY_BLOCKS);
    }
    return false;
}

bool Impl::write_block(uint32_t b, const uint8_t *buf)
{
    if (!host_rw) {
        return ro_put(b, buf);
    }
    int o = ov_find(b);
    uint32_t slot;
    bool meta = false;
    if (o >= 0) {
        memcpy(ov[o].data, buf, FFS_BLOCK);
        meta = true;
    } else if (sr_find(b, &slot) && ffs_sum_ok(buf, FFS_LONGS) && (ffs_rd(buf, FL_TYPE) == FFS_T_HEADER || ffs_rd(buf, FL_TYPE) == FFS_T_LIST) && nov < FH_OVERLAY_BLOCKS &&
               ov_put(b, buf)) {
        sr_remove_range(b, 1);                              // the block held data and is a header now (first fit): it moves to the overlay, which the fold-back does not take away
        meta = true;
    } else if (sr_find(b, &slot)) {
        if (!scratch || !scratch->write((uint64_t)slot * FFS_BLOCK, buf, FFS_BLOCK)) {
            st->dropped_writes++;
            return false;
        }
        meta = smeta > 0;
    } else {
        if (b < 2 || (b >= R && b < R + K)) {
            meta = true;
        } else {
            int i = span_find(b);
            if (i >= 0 && !(nodes[i].flags & F_DEAD)) {
                uint32_t off = b - nodes[i].hdr, nd_ = (nodes[i].flags & F_DIR) ? 0 : file_blocks(nodes[i]);
                if (off == 0 || off > nd_) {
                    meta = true;
                } else {
#if FH_STRAIGHT_WRITES
                    if (straight((uint32_t)i, off - 1, buf)) {
                        return true;
                    }
#endif
                    // a block in the data range of a file the Amiga may have given up (first fit hands out the block it has just freed): a header or an
                    // extension block it writes there belongs to the overlay, not to the scratch runs that the fold-back takes away for a file
                    meta = ffs_sum_ok(buf, FFS_LONGS) && (ffs_rd(buf, FL_TYPE) == FFS_T_HEADER || ffs_rd(buf, FL_TYPE) == FFS_T_LIST);
                }
            } else {
                meta = ffs_sum_ok(buf, FFS_LONGS) && (ffs_rd(buf, FL_TYPE) == FFS_T_HEADER || ffs_rd(buf, FL_TYPE) == FFS_T_LIST);
            }
        }
        bool stored = false;
        if (meta && ov_put(b, buf)) {
            stored = true;
        } else if (sr_put(b, buf)) {
            stored = true;
            if (meta) smeta++;
        }
        if (!stored) {
            st->dropped_writes++;
            if (!sr_full_said) {
                sr_full_said = true;
                printf("[folderhd] unit %d: the overlay and the scratch file cannot take block %u (medium full?); the write is lost\n", unit, (unsigned)b);
            }
            return false;
        }
    }
    gen++;
    if (meta) meta_gen++;
    if (phase >= PH_REN1) late = true;
    st->delta_writes++;
    return true;
}

// ============================================================================================================================================
// FolderDisk
// ============================================================================================================================================
FolderDisk::FolderDisk() : p(0), host_rw_(false)
{
    memset(&st_, 0, sizeof(st_));
    report_[0] = 0;
}

static void base_name(const char *path, char *out, int n)
{
    const char *e = path + strlen(path);
    while (e > path && e[-1] == '/') e--;
    const char *s = e;
    while (s > path && s[-1] != '/') s--;
    folderhd_cpy(out, n, s, (int)(e - s));
}

FolderDisk *FolderDisk::mount(const Cfg &cfg, char *msg, int msglen)
{
    FolderDisk *d = new FolderDisk();
    Impl *p = (Impl *)mem_alloc(sizeof(Impl));
    if (!p) {
        snprintf(msg, (size_t)msglen, "Out of memory.");
        delete d;
        return 0;
    }
    memset(p, 0, sizeof(Impl));
    d->p = p;
    p->self = d;
    p->fs = cfg.fs;
    p->io = cfg.io;
    p->unit = cfg.unit;
    p->st = &d->st_;
    p->phase = PH_IDLE;
    for (int i = 0; i < FH_HANDLES; i++) p->hd[i].node = -1;
    for (int i = 0; i < 16; i++) p->stab[i].node = -1;
    p->memo_blk = 0xFFFFFFFFu;
    snprintf(p->base, sizeof(p->base), "%s", cfg.folder);
    size_t bl = strlen(p->base);
    while (bl > 1 && p->base[bl - 1] == '/') p->base[--bl] = 0;
    // the medium's root: the first path component
    const char *sl = strchr(p->base + 1, '/');
    folderhd_cpy(p->root, sizeof(p->root), p->base, (int)(sl ? sl - p->base : (int)bl));
    if (!strcmp(p->base, "/") || !p->base[1]) {
        snprintf(msg, (size_t)msglen, "Choose a folder on a medium, not the list of the media.");
        delete d;
        return 0;
    }
    snprintf(p->tmpdir, sizeof(p->tmpdir), "%s/trx64", p->root);
    snprintf(p->scratch_path, sizeof(p->scratch_path), "%s/folderhd%d.scratch", p->tmpdir, cfg.unit);
    uint64_t sz;
    bool isdir;
    if (!cfg.fs->stat(p->base, &sz, &isdir) || !isdir) {
        snprintf(msg, (size_t)msglen, "%s is not a folder.", p->base);
        delete d;
        return 0;
    }
    char bn[64];
    base_name(p->base, bn, sizeof(bn));
    uint8_t an[128];
    int al = folderhd_host_to_aname(bn, an, sizeof(an));
    int vl = 0;
    for (int i = 0; i < al && vl < FFS_NAME_MAX; i++) {
        if (an[i] != ':' && an[i] != '/' && an[i] >= 32) p->volname[vl++] = (char)an[i];
    }
    p->volname[vl] = 0;
    if (!vl) snprintf(p->volname, sizeof(p->volname), "Folder");
    // is the medium writable at all? The probe is the scratch file's place; a stale file of a previous run is removed (it is stale by construction)
    p->host_rw = false;
    if (cfg.fs->mkdir(p->tmpdir) && cfg.fs->save(p->scratch_path, "", 0)) {
        p->host_rw = true;
        cfg.fs->remove(p->scratch_path);
        const int LE = 128;
        FsEntryShort *le = (FsEntryShort *)mem_alloc((size_t)LE * sizeof(FsEntryShort));
        int ln = le ? cfg.fs->list_short(p->tmpdir, le, LE) : 0;
        char pre[32];
        snprintf(pre, sizeof(pre), "folderhd%d.", cfg.unit);
        for (int i = 0; i < ln; i++) {
            if (le[i].cut || strncmp(le[i].name, pre, strlen(pre))) {
                continue;
            }
            if (strstr(le[i].name, ".bk")) {
                p->kept_backups++;                       // the old content of a file whose replace was cut: the one copy there is, left alone
                continue;
            }
            char sp[160];
            snprintf(sp, sizeof(sp), "%s/%s", p->tmpdir, le[i].name);
            cfg.fs->remove(sp);                          // a temp file a cut left behind
        }
        mem_free(le, (size_t)LE * sizeof(FsEntryShort));
    }
    d->host_rw_ = p->host_rw;
    char rep[400];
    if (!p->scan(msg, msglen, rep, sizeof(rep)) || !p->layout(cfg, msg, msglen)) {
        delete d;
        return 0;
    }
    snprintf(d->report_, sizeof(d->report_), "%s", rep);
    if (p->kept_backups) {
        size_t l = strlen(d->report_);
        snprintf(d->report_ + l, sizeof(d->report_) - l, "%u file(s) of an interrupted replace are kept in %s; ", (unsigned)p->kept_backups, p->tmpdir);
    }
    if (p->ncap > p->nn) {                                    // the index takes what it needs and no more
        Node *q = (Node *)mem_realloc(p->nodes, p->ncap * sizeof(Node), p->nn * sizeof(Node));
        if (q) { p->nodes = q; p->ncap = p->nn; }
    }
    if (p->pcap > p->plen) {
        uint8_t *q = (uint8_t *)mem_realloc(p->pool, p->pcap, p->plen);
        if (q) { p->pool = q; p->pcap = p->plen; }
    }
    p->pool_limit = p->plen + FH_POOL_EXTRA;
    FhStats &st = d->st_;
    st.nodes = p->nn - 1;
    st.pool_bytes = p->plen;
    st.index_bytes = p->index_bytes();
    st.cylinders = p->cyls;
    st.part_blocks = p->nblk;
    st.root_block = p->R;
    return d;
}

FolderDisk::~FolderDisk()
{
    if (!p) {
        return;
    }
    p->hclose_all();
    if (p->tmp) { p->fs->close(p->tmp); p->tmp = 0; }
    if (p->scratch) { p->fs->close(p->scratch); p->scratch = 0; p->fs->remove(p->scratch_path); }
    for (int i = 0; i < FH_HANDLES; i++) mem_free(p->hd[i].ra, FH_RA_BYTES);
    for (uint32_t i = 0; i < p->nov; i++) mem_free(p->ov[i].data, FFS_BLOCK);
    mem_free(p->ov, p->ov_cap * sizeof(OvBlk));
    mem_free(p->sr, p->sr_cap * sizeof(SRun));
    mem_free(p->xr, p->xr_cap * sizeof(XRun));
    mem_free(p->nodes, p->ncap * sizeof(Node));
    mem_free(p->pool, p->pcap);
    mem_free(p->cre, p->cre_cap * sizeof(uint16_t));
    mem_free(p->seen, p->seen ? FH_SEEN_BYTES : 0);
    mem_free(p->pend, p->pend_cap * sizeof(Pend));
    mem_free(p->buf, FH_CHUNK);
    mem_free(p, sizeof(Impl));
    p = 0;
}

uint64_t FolderDisk::size() { return p->size_bytes; }
bool FolderDisk::writable() { return true; }
bool FolderDisk::dirty() const { return p->dirty(); }

bool FolderDisk::read(uint64_t off, void *buf, uint32_t len)
{
    if ((off % FFS_BLOCK) || (len % FFS_BLOCK) || off + len > p->size_bytes) {
        return false;
    }
    uint8_t *o = (uint8_t *)buf;
    for (uint64_t lba = off / FFS_BLOCK; len; lba++, len -= FFS_BLOCK, o += FFS_BLOCK) {
        if (lba == 0) {
            ffs_build_rdb(o, p->cyls);
        } else if (lba == 1) {
            char dn[8];
            snprintf(dn, sizeof(dn), "DH%d", p->unit);
            ffs_build_part(o, p->cyls, dn);
        } else if (lba < FFS_CYL_BLOCKS) {
            memset(o, 0, FFS_BLOCK);
        } else {
            p->view_read((uint32_t)(lba - FFS_CYL_BLOCKS), o);
        }
    }
    return true;
}

bool FolderDisk::write(uint64_t off, const void *buf, uint32_t len)
{
    if ((off % FFS_BLOCK) || (len % FFS_BLOCK) || off + len > p->size_bytes) {
        return false;
    }
    const uint8_t *o = (const uint8_t *)buf;
    for (uint64_t lba = off / FFS_BLOCK; len; lba++, len -= FFS_BLOCK, o += FFS_BLOCK) {
        if (lba < FFS_CYL_BLOCKS) {
            continue;                                   // the RDB area is the device's own; nothing the Amiga writes there is kept
        }
        p->write_block((uint32_t)(lba - FFS_CYL_BLOCKS), o);
    }
    return true;
}

// ---- the menu's offer ----
bool folderhd_parse_gb(const char *text, uint64_t def, uint64_t *out)
{
    if (!text || !*text) {
        *out = def;
        return true;
    }
    uint64_t whole = 0, frac = 0, scale = 1;
    const char *s = text;
    bool any = false;
    for (; *s >= '0' && *s <= '9'; s++) {
        whole = whole * 10 + (uint64_t)(*s - '0');
        any = true;
        if (whole > 1000000) return false;
    }
    if (*s == '.' || *s == ',') {
        s++;
        for (int n = 0; *s >= '0' && *s <= '9'; s++, n++) {
            if (n < 2) { frac = frac * 10 + (uint64_t)(*s - '0'); scale *= 10; }
            any = true;
        }
    }
    if (*s || !any) {
        return false;
    }
    while (scale < 100) { frac *= 10; scale *= 10; }
    *out = whole * (1ull << 30) + frac * (1ull << 30) / 100;
    return true;
}

static uint64_t tree_bytes(CoreFs *fs, const char *dir, int depth, uint32_t *budget, bool at_root)
{
    const int cap = 2048;
    FsEntryShort *e = (FsEntryShort *)mem_alloc((size_t)cap * sizeof(FsEntryShort));
    if (!e || depth > 24) { mem_free(e, (size_t)cap * sizeof(FsEntryShort)); return 0; }
    uint64_t total = 0;
    int n = fs->list_short(dir, e, cap);
    for (int i = 0; i < n && *budget; i++) {
        if (e[i].name[0] == '.' || (e[i].attr & (FS_ATTR_HIDDEN | FS_ATTR_SYSTEM))) continue;
        if (at_root && e[i].dir && !strcasecmp(e[i].name, "trx64")) continue;
        (*budget)--;
        if (e[i].dir) {
            char sub[300];
            snprintf(sub, sizeof(sub), "%s/%s", dir, e[i].name);
            total += FFS_BLOCK + tree_bytes(fs, sub, depth + 1, budget, false);
        } else if (e[i].size < 0x100000000ull) {
            total += (e[i].size + FFS_BLOCK - 1) / FFS_BLOCK * FFS_BLOCK + FFS_BLOCK + e[i].size / (72 * FFS_BLOCK) * FFS_BLOCK;
        }
    }
    mem_free(e, (size_t)cap * sizeof(FsEntryShort));
    return total;
}

uint64_t folderhd_offer_bytes(CoreFs *fs, const char *folder, uint32_t max_cylinders, uint64_t at_least)
{
    if (max_cylinders > FH_MAX_CYL) max_cylinders = FH_MAX_CYL;
    if (max_cylinders < 2) return 0;
    uint64_t reach = (uint64_t)(max_cylinders - 1) * FFS_CYL_BLOCKS * FFS_BLOCK;
    char root[48];
    const char *sl = strchr(folder + 1, '/');
    folderhd_cpy(root, sizeof(root), folder, (int)(sl ? sl - folder : (int)strlen(folder)));
    uint64_t fb;
    if (!fs->free_bytes(root, &fb)) {
        return 0;
    }
    uint64_t room = fb > FH_RESERVE_BYTES ? fb - FH_RESERVE_BYTES : 0;
    if (at_least && at_least <= reach && at_least <= room) {
        return at_least;                                        // the free space alone backs it: the tree on top of it needs no look
    }
    uint32_t budget = FH_ENTRIES_MAX;
    uint64_t have = tree_bytes(fs, folder, 0, &budget, !strcasecmp(folder, root));
    uint64_t backed = have + room;
    return backed < reach ? backed : reach;
}
