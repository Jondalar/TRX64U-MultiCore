// T146: the menu picture's codec, its key, the settle timer and the key queue (host-app/software/hostapp/menusnap.cc).
#include "menusnap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::vector<uint16_t> menu_like()   // flat background, a header band, rows with text-like noise: what the menu looks like to a codec
{
    std::vector<uint16_t> p(MS_PIXELS, 0x0042);
    for (int y = 0; y < 40; y++) for (int x = 0; x < MS_W; x++) p[y * MS_W + x] = 0x0884;
    unsigned s = 12345;
    for (int row = 0; row < 12; row++) {
        for (int y = 0; y < 30; y++) {
            for (int x = 20; x < 700; x++) {
                s = s * 1103515245u + 12345u;
                if ((s >> 16) % 5 == 0) p[(60 + row * 36 + y) * MS_W + x] = 0xFFFF - ((s >> 20) & 7);
            }
        }
    }
    return p;
}

static std::vector<uint8_t> buf(MS_RAW_BYTES + MS_RAW_BYTES / 64 + 64);

static bool roundtrip(const std::vector<uint16_t> &px, uint32_t key, int *len_out)
{
    int n = ms_encode(px.data(), MS_W, MS_H, key, buf.data(), buf.size());
    if (n < 0) return false;
    if (len_out) *len_out = n;
    std::vector<uint16_t> back(MS_PIXELS, 0x5555);
    if (ms_load(buf.data(), n, MS_W, MS_H, key, back.data()) != MS_OK) return false;
    return back == px;
}

int main()
{
    // known CRC-32 value
    CHECK(ms_crc32(0, "123456789", 9) == 0xCBF43926u);
    CHECK(ms_crc32(ms_crc32(0, "1234", 4), "56789", 5) == 0xCBF43926u);

    // round trips
    int n;
    CHECK(roundtrip(std::vector<uint16_t>(MS_PIXELS, 0x1234), 1, &n));                   // one flat colour: a handful of long runs
    printf("flat frame: %d bytes\n", n);
    CHECK(n < 100);
    auto m = menu_like();
    CHECK(roundtrip(m, 2, &n));
    printf("menu-like frame: %d bytes (raw %d)\n", n, MS_RAW_BYTES);
    CHECK(n < MS_RAW_BYTES / 4);
    // worst case: no two neighbours equal, so no run at all
    std::vector<uint16_t> w(MS_PIXELS);
    for (int i = 0; i < MS_PIXELS; i++) w[i] = (uint16_t)(i * 40503u + (i >> 5));
    for (int i = 1; i < MS_PIXELS; i++) if (w[i] == w[i - 1]) w[i] ^= 1;
    CHECK(roundtrip(w, 3, &n));
    printf("worst case: %d bytes, bound %u\n", n, ms_max_encoded(MS_PIXELS));
    CHECK((uint32_t)n <= ms_max_encoded(MS_PIXELS));
    CHECK(n <= MS_LIMIT);
    // runs of 2 (too short to be a run) next to runs of 3 and 129/130/65665/65666 pixels
    {
        std::vector<uint16_t> p(MS_PIXELS);
        int i = 0, lens[] = { 2, 3, 4, 129, 130, 131, 65665, 65666, 1, 2, 1, 3, 1 };
        uint16_t v = 1;
        for (int l : lens) { for (int k = 0; k < l && i < MS_PIXELS; k++) p[i++] = v; v += 7; }
        while (i < MS_PIXELS) { p[i] = v; i++; if (i % 3 == 0) v += 3; }
        CHECK(roundtrip(p, 4, &n));
    }
    // random noise with short runs
    {
        std::vector<uint16_t> p(MS_PIXELS);
        unsigned s = 99;
        for (int i = 0; i < MS_PIXELS; i++) { s = s * 1664525u + 1013904223u; p[i] = (s >> 28) < 6 ? (i ? p[i - 1] : 0) : (uint16_t)(s >> 8); }
        CHECK(roundtrip(p, 5, &n));
    }
    // the size limit: an output buffer too small, and the 1 MB cap (D5)
    {
        std::vector<uint8_t> small(1000);
        CHECK(ms_encode(w.data(), MS_W, MS_H, 1, small.data(), small.size()) == -1);
        CHECK(ms_encode(m.data(), MS_W, MS_H, 1, small.data(), small.size()) == -1);
        CHECK(ms_encode(std::vector<uint16_t>(MS_PIXELS, 7).data(), MS_W, MS_H, 1, small.data(), small.size()) > 0);
        std::vector<uint8_t> big(3 * 1024 * 1024);
        std::vector<uint16_t> wide(1024 * 1024);                         // 2 MB raw, no runs: above the limit even though the buffer is big
        for (size_t i = 0; i < wide.size(); i++) wide[i] = (uint16_t)(i * 40503u + (i >> 5));
        for (size_t i = 1; i < wide.size(); i++) if (wide[i] == wide[i - 1]) wide[i] ^= 1;
        CHECK(ms_encode(wide.data(), 1024, 1024, 1, big.data(), big.size()) == -1);
    }

    // the key: boot.cfg, the mode and the build each change it
    const char *cfg = "countdown=5\ndefault=0\ncore=/SD/a.cor|A|00\n";
    const char *cfg_moved = "countdown=5\ndefault=0\ncore=/SD/b.cor|B|00\n";
    uint32_t k0 = ms_key(cfg, strlen(cfg), 0, 2, "85acf2b");
    CHECK(k0 == ms_key(cfg, strlen(cfg), 0, 2, "85acf2b"));
    uint32_t k_cfg = ms_key(cfg_moved, strlen(cfg_moved), 0, 2, "85acf2b");
    uint32_t k_sys = ms_key(cfg, strlen(cfg), 1, 2, "85acf2b");
    uint32_t k_hdmi = ms_key(cfg, strlen(cfg), 0, 1, "85acf2b");
    uint32_t k_git = ms_key(cfg, strlen(cfg), 0, 2, "85acf2c");
    CHECK(k_cfg != k0 && k_sys != k0 && k_hdmi != k0 && k_git != k0);

    // S2: a picture is only loaded under the key it was taken with; each refusal has its own cause
    auto img = menu_like();
    n = ms_encode(img.data(), MS_W, MS_H, k0, buf.data(), buf.size());
    CHECK(n > 0);
    std::vector<uint16_t> out(MS_PIXELS, 0);
    CHECK(ms_load(buf.data(), n, MS_W, MS_H, k0, out.data()) == MS_OK && out == img);
    out.assign(MS_PIXELS, 0);
    CHECK(ms_load(buf.data(), n, MS_W, MS_H, k_cfg, out.data()) == MS_KEY);     // an entry moved, renamed, added or removed
    CHECK(ms_load(buf.data(), n, MS_W, MS_H, k_sys, out.data()) == MS_KEY);     // another picture mode
    CHECK(ms_load(buf.data(), n, MS_W, MS_H, k_hdmi, out.data()) == MS_KEY);
    CHECK(ms_load(buf.data(), n, MS_W, MS_H, k_git, out.data()) == MS_KEY);     // another build
    CHECK(out == std::vector<uint16_t>(MS_PIXELS, 0));                          // a refusal wrote nothing
    // damage: every kind has its refusal
    std::vector<uint8_t> f(buf.begin(), buf.begin() + n);
    CHECK(ms_load(f.data(), 10, MS_W, MS_H, k0, out.data()) == MS_TOO_SHORT);
    CHECK(ms_load(f.data(), n - 1, MS_W, MS_H, k0, out.data()) == MS_LEN);      // cut off
    { auto g = f; g[0] ^= 1; CHECK(ms_load(g.data(), n, MS_W, MS_H, k0, out.data()) == MS_MAGIC); }
    { auto g = f; g[4] = 2; CHECK(ms_load(g.data(), n, MS_W, MS_H, k0, out.data()) == MS_FORMAT); }
    { auto g = f; g[6] ^= 1; CHECK(ms_load(g.data(), n, MS_W, MS_H, k0, out.data()) == MS_SIZE); }
    { auto g = f; g[MS_HDR_BYTES + 5] ^= 0x10; CHECK(ms_load(g.data(), n, MS_W, MS_H, k0, out.data()) == MS_CRC); }
    {   // data that passes its CRC but does not make w*h pixels (a file written by something else): refused
        std::vector<uint8_t> g(MS_HDR_BYTES + 3);
        memcpy(g.data(), f.data(), MS_HDR_BYTES);
        g[MS_HDR_BYTES] = 0x80; g[MS_HDR_BYTES + 1] = 0; g[MS_HDR_BYTES + 2] = 0;   // one run of 3 pixels
        uint32_t crc = ms_crc32(0, g.data() + MS_HDR_BYTES, 3);
        g[16] = 3; g[17] = g[18] = g[19] = 0;
        g[20] = crc; g[21] = crc >> 8; g[22] = crc >> 16; g[23] = crc >> 24;
        CHECK(ms_load(g.data(), g.size(), MS_W, MS_H, k0, out.data()) == MS_DATA);
    }
    {   // a run that would write past the end
        std::vector<uint8_t> g(MS_HDR_BYTES + 5);
        memcpy(g.data(), f.data(), MS_HDR_BYTES);
        g[MS_HDR_BYTES] = 0xFF; g[MS_HDR_BYTES + 1] = 0xFF; g[MS_HDR_BYTES + 2] = 0xFF; g[MS_HDR_BYTES + 3] = 0; g[MS_HDR_BYTES + 4] = 0;
        uint32_t crc = ms_crc32(0, g.data() + MS_HDR_BYTES, 5);
        g[16] = 5; g[17] = g[18] = g[19] = 0;
        g[20] = crc; g[21] = crc >> 8; g[22] = crc >> 16; g[23] = crc >> 24;
        std::vector<uint16_t> tiny(MS_PIXELS, 0x7777);
        CHECK(ms_load(g.data(), g.size(), MS_W, MS_H, k0, tiny.data()) == MS_DATA);
    }
    // the same picture again is recognised by key and data CRC
    uint32_t hk, hc, hk2, hc2;
    CHECK(ms_header_info(f.data(), n, &hk, &hc) && hk == k0);
    auto img2 = img; img2[1000] ^= 1;
    std::vector<uint8_t> f2(buf.size());
    int n2 = ms_encode(img2.data(), MS_W, MS_H, k0, f2.data(), f2.size());
    CHECK(ms_header_info(f2.data(), n2, &hk2, &hc2) && hk2 == k0 && hc2 != hc);

    // S3: the settle timer. The picture is saved once, 2 s after the last change, and not again while nothing changes.
    MsSettle s;
    ms_settle_init(&s);
    CHECK(!ms_settle(&s, 1000, 11, true, MS_QUIET_MS));        // first sight
    CHECK(!ms_settle(&s, 2500, 11, true, MS_QUIET_MS));        // 1.5 s
    CHECK(ms_settle(&s, 3000, 11, true, MS_QUIET_MS));         // 2 s: save
    ms_settle_saved(&s);
    CHECK(!ms_settle(&s, 9000, 11, true, MS_QUIET_MS));        // nothing changed: no second save
    CHECK(!ms_settle(&s, 10000, 12, true, MS_QUIET_MS));       // a change restarts the quiet time
    CHECK(!ms_settle(&s, 11500, 12, true, MS_QUIET_MS));
    CHECK(!ms_settle(&s, 11900, 13, true, MS_QUIET_MS));       // a change in between
    CHECK(!ms_settle(&s, 13000, 13, true, MS_QUIET_MS));
    CHECK(!ms_settle(&s, 14000, 13, false, MS_QUIET_MS));      // not eligible (countdown running, other view): no save
    CHECK(ms_settle(&s, 14000, 13, true, MS_QUIET_MS));
    ms_settle_saved(&s);
    CHECK(!ms_settle(&s, 20000, 14, true, MS_QUIET_MS));
    ms_settle_touch(&s, 21500);                                // a key
    CHECK(!ms_settle(&s, 22000, 14, true, MS_QUIET_MS));
    CHECK(ms_settle(&s, 23500, 14, true, MS_QUIET_MS));
    ms_settle_saved(&s);
    CHECK(!ms_settle(&s, 30000, 11, true, MS_QUIET_MS));       // back to an old signature: a change, saved again after the quiet time
    CHECK(ms_settle(&s, 32000, 11, true, MS_QUIET_MS));

    // S4: keys queued in order
    MsKeyQ q;
    ms_keyq_init(&q);
    CHECK(ms_keyq_pop(&q) == -1);
    CHECK(ms_keyq_push(&q, 'a') && ms_keyq_push(&q, 'b') && ms_keyq_push(&q, 'c'));
    CHECK(ms_keyq_pop(&q) == 'a' && ms_keyq_pop(&q) == 'b' && ms_keyq_pop(&q) == 'c' && ms_keyq_pop(&q) == -1);
    for (int i = 0; i < MS_KEYQ; i++) CHECK(ms_keyq_push(&q, i + 1));
    CHECK(!ms_keyq_push(&q, 99) && q.dropped == 1);
    CHECK(ms_keyq_pop(&q) == 1);

    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("test_menusnap: all passed\n");
    return 0;
}
