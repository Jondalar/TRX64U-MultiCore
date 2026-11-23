// The HTTP client of the online sub-app (online_http.h).
#include "online_http.h"
#include "otext.h"

namespace {

#define HEAD_MAX 4096            // the longest header block read
#define REQ_MAX 1024

struct Buf {
    const HttpEnv *e;
    uint8_t *b;
    uint32_t len, cap;
};

// room for `need` bytes: exactly that many when the length of the answer is known (one block, no copy on the way), else a half more than there is (a few copies at most)
bool reserve(Buf &x, uint32_t need, bool exact)
{
    if (need <= x.cap) {
        return true;
    }
    uint32_t nc = exact ? need : (x.cap ? x.cap + x.cap / 2 : 8192);
    if (nc < need) {
        nc = need;
    }
    uint8_t *nb = (uint8_t *)x.e->mem->realloc(x.b, nc);
    if (!nb) {
        return false;
    }
    x.b = nb;
    x.cap = nc;
    return true;
}

int find_crlf2(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i + 4 <= n; i++) {
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
            return (int)i;
        }
    }
    return -1;
}

bool read_number(const char *s, const char *end, uint32_t *v)
{
    uint32_t x = 0;
    bool any = false;
    while (s < end && *s == ' ') {
        s++;
    }
    while (s < end && *s >= '0' && *s <= '9') {
        x = x * 10 + (uint32_t)(*s - '0');
        any = true;
        s++;
    }
    *v = x;
    return any;
}

// The decoded body of a chunked answer, in place over data[0..n): true with the length in *out when the last chunk was there.
bool unchunk(uint8_t *d, uint32_t n, uint32_t *out)
{
    uint32_t r = 0, w = 0;
    for (;;) {
        uint32_t size = 0;
        bool any = false;
        while (r < n) {
            char c = (char)d[r];
            int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (h < 0) {
                break;
            }
            size = size * 16 + (uint32_t)h;
            any = true;
            r++;
        }
        while (r < n && d[r] != '\n') {                       // a chunk extension, then the end of the line
            r++;
        }
        if (!any || r >= n) {
            return false;
        }
        r++;
        if (size == 0) {
            *out = w;
            return true;
        }
        if (n - r < size + 2) {
            return false;
        }
        memmove(d + w, d + r, size);
        w += size;
        r += size + 2;                                        // the CRLF after the data
    }
}

// the end of the stream of a chunked answer: "0" CRLF and the empty line (trailers are not expected)
bool chunk_end(const uint8_t *d, uint32_t n)
{
    return n >= 5 && d[n - 5] == '0' && d[n - 4] == '\r' && d[n - 3] == '\n' && d[n - 2] == '\r' && d[n - 1] == '\n';
}

}   // namespace

int http_get(const HttpEnv &env, const char *host, const char *path, const char *client_id, uint32_t max_body, HttpOut *out)
{
    out->body = 0;
    out->len = 0;
    out->status = 0;
    if (!env.net || !env.mem || !env.net->connect || !env.net->recv || !env.net->send || !env.net->close || !env.net->wait) {
        return HTTP_NO_NET;
    }
    char req[REQ_MAX];
    req[0] = 0;
    ot_cat(req, sizeof(req), "GET ");
    ot_cat(req, sizeof(req), path);
    ot_cat(req, sizeof(req), " HTTP/1.1\r\nAccept-encoding: identity\r\nHost: ");
    ot_cat(req, sizeof(req), host);
    ot_cat(req, sizeof(req), "\r\nUser-Agent: Assembly Query\r\nClient-Id: ");
    ot_cat(req, sizeof(req), client_id);
    ot_cat(req, sizeof(req), "\r\nConnection: close\r\n\r\n");
    int rl = ot_len(req);

    if (env.cancelled && env.cancelled(env.user)) {
        return HTTP_BROKEN;
    }
    int h = env.net->connect(host, 80, HTTP_CONNECT_MS);
    if (h < 0) {
        return HTTP_NO_NET;
    }
    int rc = HTTP_BROKEN;
    Buf x = { &env, 0, 0, 0 };
    bool ok = env.net->send(h, req, rl) == rl;
    int head = -1;                                            // the offset of the blank line, once read
    uint32_t body_at = 0, clen = 0;
    bool chunked = false, have_len = false, closed = false;
    uint32_t last = env.sys && env.sys->ms ? env.sys->ms() : 0;
    while (ok) {
        if (head < 0 ? closed : (closed || (have_len && x.len - body_at >= clen) || (chunked && chunk_end(x.b + body_at, x.len - body_at)))) {
            break;
        }
        uint32_t want = x.len + 2048;
        bool exact = head >= 0 && have_len;
        if (exact) {
            want = body_at + clen;
        }
        if (want > x.cap && !reserve(x, want, exact)) {
            rc = HTTP_NO_ROOM;
            ok = false;
            break;
        }
        int n = env.net->recv(h, x.b + x.len, (int)(x.cap - x.len));
        if (n == 0) {
            closed = true;                                    // the server closed: what is here is the answer
            continue;
        }
        if (n == -5) {                                        // nothing waiting
            int hh = h;
            env.net->wait(&hh, 1, 1000);
            if (env.cancelled && env.cancelled(env.user)) {
                ok = false;                                   // the job was asked to end
                continue;
            }
            if (env.sys && env.sys->ms && env.sys->ms() - last > HTTP_IDLE_MS) {
                ok = false;
            }
            continue;
        }
        if (n < 0) {
            ok = false;                                       // -3: the connection broke
            break;
        }
        x.len += (uint32_t)n;
        last = env.sys && env.sys->ms ? env.sys->ms() : 0;
        if (head < 0) {
            head = find_crlf2(x.b, x.len);
            if (head < 0) {
                ok = x.len <= HEAD_MAX;                       // a header block that does not end is no HTTP
                continue;
            }
            body_at = (uint32_t)head + 4;
            const char *hb = (const char *)x.b;
            const char *sp = hb;
            while (sp < hb + head && *sp != ' ') {
                sp++;
            }
            uint32_t st = 0;                                  // "HTTP/1.1 200 OK"
            if (x.len < 5 || memcmp(x.b, "HTTP/", 5) != 0 || !read_number(sp, hb + head, &st)) {
                ok = false;
                break;
            }
            out->status = (int)st;
            if (st < 200 || st > 299) {
                rc = HTTP_STATUS;
                ok = false;
                break;
            }
            const char *te = ot_ifind(hb, head, "\r\ntransfer-encoding:");
            if (te) {
                const char *eol = te + 2;
                while (eol < hb + head && *eol != '\r') {
                    eol++;
                }
                chunked = ot_ifind(te, (int)(eol - te), "chunked") != 0;
            }
            const char *cl = ot_ifind(hb, head, "\r\ncontent-length:");
            if (cl && !chunked) {
                have_len = read_number(cl + 17, hb + head, &clen);
            }
            if (have_len && clen > max_body) {
                rc = HTTP_TOO_BIG;
                ok = false;
                break;
            }
        }
        if (head >= 0 && !have_len && x.len - body_at > max_body) {
            rc = HTTP_TOO_BIG;                                // an answer of unknown length that has grown past what the caller takes
            ok = false;
        }
    }
    if (ok && head >= 0) {
        uint32_t got = x.len - body_at, blen = got;
        if (chunked) {
            ok = unchunk(x.b + body_at, got, &blen);          // a chunked answer that did not end properly is no answer
        } else if (have_len) {
            ok = got >= clen;                                 // the server closed before the whole body came
            blen = clen;
        }
        if (ok && blen > max_body) {
            rc = HTTP_TOO_BIG;
            ok = false;
        }
        if (ok) {
            memmove(x.b, x.b + body_at, blen);
            out->body = x.b;
            out->len = blen;
            x.b = 0;
            rc = HTTP_OK;
        }
    }
    env.net->close(h);
    if (x.b) {
        env.mem->free(x.b);
    }
    return rc;
}
