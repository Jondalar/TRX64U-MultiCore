// T89 P8 / S9: the Disk II side of the Apple //e application - the 6-and-2 nibblizer and its inverse against vectors written from
// the DOS 3.3 RWTS definition (gen_apple2_golden.py) and against an independent POSTNIB-style decoder, the file orders, and the
// service of the core's track window against a model of a2_trk.vhd (request / acknowledge, the stale request after an acknowledge,
// write protect, dirty track flush at eject, undecodable tracks).
#include "check.h"
#include "fakes.h"
#include "apple2_regs.h"
#include "apple2_disk.h"
#include "hdd_rom.h"
#include <fstream>
#include <sstream>

// ---- the image: byte i of file sector s of track t
static uint8_t px(int t, int s, int i) { return (uint8_t)(t * 131 + s * 37 + i * 11 + 5 + (i >> 5) * t); }
static std::vector<uint8_t> make_image()
{
    std::vector<uint8_t> v(143360);
    for (int t = 0; t < 35; t++) for (int s = 0; s < 16; s++) for (int i = 0; i < 256; i++) v[t * 4096 + s * 256 + i] = px(t, s, i);
    return v;
}

// ---- a model of the core's window to the application: WIN_SEL picks the array, the track buffers are 6656 bytes of bits, the request
// registers follow a2_trk.vhd (a save has priority; an acknowledge closes the request, and the status the application reads lags: `lag`
// more reads still show it open), and the slot 7 card's registers follow a2_hdd.vhd
struct A2Model : FakeIo {
    std::vector<uint8_t> trk[2], hrom;
    bool req[2] = { false, false }, save[2] = { false, false }, dirty[2] = { false, false };
    uint8_t rtrk[2] = { 0, 0 }, cur[2] = { 0, 0 };
    int lag = 0, stale[2] = { 0, 0 };
    int acks[2] = { 0, 0 }, acks_on_idle = 0, trk_writes[2] = { 0, 0 };
    std::vector<uint8_t> cmds;
    // the card
    bool hreq = false, hwrite = false, hunit = false;
    uint16_t hblk = 0, hmem = 0;
    int hlag = 0, hstale = 0, hacks = 0, hrom_writes = 0;
    std::vector<int> hresults;
    A2Model()
    {
        trk[0].assign(A2_TRACK_BYTES, 0); trk[1].assign(A2_TRACK_BYTES, 0); hrom.assign(A2_HDDROM_BYTES, 0);
        regs[A2_CAPS2] = A2_CAPS2_BITS;
        rom_sel_reg = A2_WIN_SEL;
    }
    void open_req(int n, bool sv, int t) { req[n] = true; save[n] = sv; rtrk[n] = (uint8_t)t; }
    bool hbank1 = false;
    void open_hdd(int unit, bool wr, int blk, int mem) { hreq = true; hunit = unit != 0; hwrite = wr; hblk = (uint16_t)blk; hmem = (uint16_t)mem; }
    uint8_t rd(uint32_t reg)
    {
        for (int n = 0; n < 2; n++) {
            if (reg == (uint32_t)A2_DRV(n) + A2_DRV_STATUS) {
                bool shown = req[n] || stale[n] > 0;
                if (!req[n] && stale[n] > 0) stale[n]--;
                return (uint8_t)((shown ? 1 : 0) | (shown && save[n] ? 2 : 0) | (dirty[n] ? 4 : 0) | 8);
            }
            if (reg == (uint32_t)A2_DRV(n) + A2_DRV_REQ) return rtrk[n];
        }
        if (reg == A2_HDD_STATUS) {
            bool shown = hreq || hstale > 0;
            if (!hreq && hstale > 0) hstale--;
            return (uint8_t)((shown ? 1 : 0) | (hwrite ? 2 : 0) | (hunit ? 4 : 0) | (hbank1 ? 8 : 0));
        }
        if (reg == A2_HDD_BLOCK) return (uint8_t)(hblk & 255);
        if (reg == A2_HDD_BLOCK + 1) return (uint8_t)(hblk >> 8);
        if (reg == A2_HDD_MEM) return (uint8_t)(hmem & 255);
        if (reg == A2_HDD_MEM + 1) return (uint8_t)(hmem >> 8);
        return FakeIo::rd(reg);
    }
    void on_wr(uint32_t reg, uint8_t v)
    {
        for (int n = 0; n < 2; n++) {
            if (reg == (uint32_t)A2_DRV(n) + A2_DRV_ACK) {
                if (!req[n]) { acks_on_idle++; continue; }
                acks[n]++;
                if (save[n]) dirty[n] = false; else cur[n] = rtrk[n];
                req[n] = false; stale[n] = lag;
            }
        }
        if (reg == A2_HDD_ACK) {
            if (!hreq) { acks_on_idle++; return; }
            hacks++;
            hresults.push_back(regs[A2_HDD_RESULT]);
            hreq = false; hstale = hlag;
        }
        if (reg == A2_DISK_CMD) {
            cmds.push_back(v);
            for (int n = 0; n < 2; n++) if ((v & (4 << n)) && dirty[n] && !req[n]) open_req(n, true, cur[n]);
        }
    }
    int sel() { return regs[A2_WIN_SEL] & 15; }
    void rom_write(uint32_t off, const void *p, uint32_t n)
    {
        if (sel() == A2_SEL_TRACK1 || sel() == A2_SEL_TRACK2) { CHECK(off + n <= A2_TRACK_BYTES); memcpy(&trk[sel() - A2_SEL_TRACK1].at(off), p, n); trk_writes[sel() - A2_SEL_TRACK1]++; }
        else if (sel() == A2_SEL_HDDROM) { CHECK(off + n <= A2_HDDROM_BYTES); memcpy(&hrom.at(off), p, n); hrom_writes++; }
        else overrun_sel = true;
    }
    void rom_read(uint32_t off, void *p, uint32_t n)
    {
        if (sel() == A2_SEL_TRACK1 || sel() == A2_SEL_TRACK2) memcpy(p, &trk[sel() - A2_SEL_TRACK1].at(off), n);
        else overrun_sel = true;
    }
    bool overrun_sel = false;
    // the bits of the buffer, as the drive reads them
    int bit(int n, uint32_t i) const { return (trk[n][i >> 3] >> (7 - (i & 7))) & 1; }
    uint32_t bits_reg(int n) { return regs[A2_DRV(n) + A2_DRV_BITS] | (uint32_t)regs[A2_DRV(n) + A2_DRV_BITS + 1] << 8; }
    bool noise_reg(int n) { return (regs[A2_DRV(n) + A2_DRV_FLAGS] & A2_DRVF_NOISE) != 0; }
};

// ---- an independent decoder: what DOS 3.3's RWTS does with a data field (the 86 byte buffer filled from the top, POSTNIB shifting
// two bits out of each use), and the software interleave as the inverse of the logical-to-physical tables
static const int DOS_L2P[16] = { 0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15 };
static const int PRO_L2P[16] = { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15 };
static int rwts_decode(const uint8_t *s, uint8_t out[256]);
static int rwts_read(const uint8_t *ring, int phys, uint8_t out[256]) { return rwts_decode(ring + phys * 416 + 23, out); }
static int rwts_decode(const uint8_t *s, uint8_t out[256])
{
    static int dec[256];
    static const uint8_t enc[64] = { 0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,
        0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF2,0xF3,
        0xF4,0xF5,0xF6,0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF };
    for (int i = 0; i < 256; i++) dec[i] = -1;
    for (int i = 0; i < 64; i++) dec[enc[i]] = i;
    uint8_t aux[86], acc = 0;
    for (int x = 85; x >= 0; x--) { acc ^= (uint8_t)dec[*s++]; aux[x] = acc; }
    for (int y = 0; y < 256; y++) { acc ^= (uint8_t)dec[*s++]; out[y] = acc; }
    if (dec[*s] != acc) return -1;
    int x = 86;
    for (int y = 0; y < 256; y++) {
        if (--x < 0) x = 85;
        uint8_t a = out[y];
        for (int k = 0; k < 2; k++) { int c = aux[x] & 1; aux[x] >>= 1; a = (uint8_t)((a << 1) | c); }
        out[y] = a;
    }
    return 0;
}

static std::vector<std::string> golden_lines()
{
    std::vector<std::string> v;
    std::ifstream f(std::string(GOLDEN_DIR) + "/apple2_nib.txt");
    std::string l;
    while (std::getline(f, l)) v.push_back(l);
    return v;
}

static std::vector<uint8_t> track_sectors(const std::vector<uint8_t> &img, int t) { return std::vector<uint8_t>(img.begin() + t * 4096, img.begin() + t * 4096 + 4096); }

static void test_nibblizer()
{
    std::vector<uint8_t> img = make_image();
    // known answers: image sectors by the golden's own content rule (s*37 + i*11 + 5) in track 3
    std::vector<uint8_t> g(4096);
    for (int s = 0; s < 16; s++) for (int i = 0; i < 256; i++) g[s * 256 + i] = (uint8_t)(s * 37 + i * 11 + 5);
    static uint8_t ring[A2N_TRACK];
    int nlines = 0;
    for (auto &l : golden_lines()) {
        std::istringstream is(l);
        std::string order, hex; int t, p;
        is >> order >> t >> p >> hex;
        a2n_nibblize_track(g.data(), order == "dos" ? A2_ORDER_DOS : A2_ORDER_PRODOS, t, ring);
        std::string got;
        char b[3];
        for (int i = 0; i < 416; i++) { snprintf(b, sizeof(b), "%02x", ring[p * 416 + i]); got += b; }
        CHECK_STR(got.c_str(), hex.c_str());
        nlines++;
    }
    CHECK_EQ(nlines, 4);
    // the file order tables are the inverse of the software interleaves
    for (int p = 0; p < 16; p++) {
        CHECK_EQ(DOS_L2P[a2n_file_sector(A2_ORDER_DOS, p)], p);
        CHECK_EQ(PRO_L2P[a2n_file_sector(A2_ORDER_PRODOS, p)], p);
    }
    // all 35 tracks, both orders: the independent decoder reads the same sectors, and de-nibblizing restores them byte for byte
    for (int o = 0; o < 2; o++) {
        A2Order order = o ? A2_ORDER_PRODOS : A2_ORDER_DOS;
        const int *l2p = o ? PRO_L2P : DOS_L2P;
        for (int t = 0; t < 35; t++) {
            std::vector<uint8_t> sec = track_sectors(img, t);
            a2n_nibblize_track(sec.data(), order, t, ring);
            uint8_t d[256];
            bool all = true;
            for (int s = 0; s < 16; s++) {
                if (rwts_read(ring, l2p[s], d) || memcmp(d, sec.data() + s * 256, 256)) all = false;
            }
            CHECK(all);
            uint8_t back[4096];
            CHECK_EQ(a2n_denibblize_track(ring, order, t, back), 16);
            CHECK(!memcmp(back, sec.data(), 4096));
        }
    }
    // a damaged ring is refused whole: a data nibble, an address checksum, the wrong track
    std::vector<uint8_t> sec = track_sectors(img, 7);
    uint8_t back[4096];
    a2n_nibblize_track(sec.data(), A2_ORDER_DOS, 7, ring);
    CHECK_EQ(a2n_denibblize_track(ring, A2_ORDER_DOS, 8, back), -1);
    ring[5 * 416 + 100] = 0x00;
    CHECK_EQ(a2n_denibblize_track(ring, A2_ORDER_DOS, 7, back), -1);
    a2n_nibblize_track(sec.data(), A2_ORDER_DOS, 7, ring);
    ring[9 * 416 + 9] ^= 0x01;
    CHECK_EQ(a2n_denibblize_track(ring, A2_ORDER_DOS, 7, back), -1);
    // a ring rotated by an odd amount (the guest rewrote bytes across the wrap) still decodes
    a2n_nibblize_track(sec.data(), A2_ORDER_DOS, 7, ring);
    static uint8_t rot[A2N_TRACK];
    for (int i = 0; i < (int)A2N_TRACK; i++) rot[i] = ring[(i + 1234) % A2N_TRACK];
    CHECK_EQ(a2n_denibblize_track(rot, A2_ORDER_DOS, 7, back), 16);
    CHECK(!memcmp(back, sec.data(), 4096));
}

static CoreCtx *mkctx(A2Model &io, FakeFs &fs)
{
    static CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.io = &io; c.fs = &fs;
    return &c;
}

// ---- bit tracks: the sector image as bits (10-bit sync), read back by a slicer written here the way the Disk II's shift register works
static std::vector<uint8_t> slice(const uint8_t *bits, uint32_t nbits)
{
    std::vector<uint8_t> out;
    bool inbyte = false;
    unsigned v = 0;
    int cnt = 0;
    for (uint32_t i = 0; i < nbits; i++) {
        int b = (bits[i >> 3] >> (7 - (i & 7))) & 1;
        if (!inbyte) {
            if (b) { inbyte = true; v = 1; cnt = 1; }
        } else {
            v = (v << 1) | (unsigned)b;
            if (++cnt == 8) { out.push_back((uint8_t)v); inbyte = false; }
        }
    }
    return out;
}

static void rotate_bits(const uint8_t *in, uint32_t nbits, uint32_t by, uint8_t *out)
{
    memset(out, 0, (nbits + 7) / 8);
    for (uint32_t i = 0; i < nbits; i++) {
        uint32_t src = (i + by) % nbits;
        if ((in[src >> 3] >> (7 - (src & 7))) & 1) out[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    }
}

// the bits of a .nib ring as the application is meant to play them (T89.1 write fix), built independently of a2n_nib_bits: nibble i is a self sync byte when it is FF and
// lies in a window of three FF (the ring is circular); returns the bit count, the bits go to `out` (A2N_BUF_BITS / 8 bytes)
static uint8_t want_bits[A2N_BUF_BITS / 8];
static uint32_t nib_pos_tab[A2N_TRACK];                                // bit position of nibble i
static uint32_t nib_bits_expected(const uint8_t *ring, uint32_t n, uint8_t *out)
{
    memset(out, 0, A2N_BUF_BITS / 8);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < n; i++) {
        bool in_run = false;
        for (int w = -2; w <= 0 && ring[i] == 0xFF; w++) {
            bool all = true;
            for (int k = 0; k < 3; k++) all = all && ring[(i + n + (uint32_t)(w + k) + n) % n] == 0xFF;
            in_run = in_run || all;
        }
        nib_pos_tab[i] = pos;
        for (int b = 7; b >= 0; b--) {
            if ((ring[i] >> b) & 1) out[pos >> 3] |= (uint8_t)(0x80 >> (pos & 7));
            pos++;
        }
        if (in_run) pos += 2;
    }
    return pos;
}

static void test_bits()
{
    std::vector<uint8_t> img = make_image();
    static uint8_t bits[A2N_BUF_BITS / 8], rot[A2N_BUF_BITS / 8], ring[A2N_TRACK];
    for (int o = 0; o < 2; o++) {
        A2Order order = o ? A2_ORDER_PRODOS : A2_ORDER_DOS;
        const int *l2p = o ? PRO_L2P : DOS_L2P;
        (void)l2p;
        for (int t = 0; t < 35; t++) {
            std::vector<uint8_t> sec = track_sectors(img, t);
            uint32_t nbits = a2n_sector_bits(sec.data(), order, t, bits);
            CHECK_EQ(nbits, 16u * (24 * 10 + 14 * 8 + 6 * 10 + 349 * 8));
            std::vector<uint8_t> nib = slice(bits, nbits);
            // the library's own reader agrees with the slicer
            static uint8_t lib[A2N_TRACK];
            uint32_t n = a2n_bits_nibbles(bits, nbits, lib, A2N_TRACK);
            CHECK_EQ(n, nib.size());
            CHECK(!memcmp(lib, nib.data(), nib.size()));
            // every address field names this track and a sector once; the data field after it decodes to that sector of the image
            int seen = 0;
            for (size_t i = 0; i + 360 < nib.size(); i++) {
                if (nib[i] != 0xD5 || nib[i + 1] != 0xAA || nib[i + 2] != 0x96) continue;
                auto d44 = [&](size_t k) { return ((nib[k] << 1) | 1) & nib[k + 1]; };
                int vol = d44(i + 3), trk = d44(i + 5), sc = d44(i + 7), chk = d44(i + 9);
                CHECK_EQ(vol, 254); CHECK_EQ(trk, t); CHECK_EQ(chk, vol ^ trk ^ sc);
                size_t j = i + 14;
                int fills = 0;
                while (nib[j] == 0xFF) { j++; fills++; }
                CHECK_EQ(fills, 6);
                CHECK(nib[j] == 0xD5 && nib[j + 1] == 0xAA && nib[j + 2] == 0xAD);
                uint8_t d[256];
                CHECK_EQ(rwts_decode(&nib[j + 3], d), 0);
                CHECK(!memcmp(d, sec.data() + a2n_file_sector(order, sc) * 256, 256));
                seen |= 1 << sc;
            }
            CHECK_EQ(seen, 0xFFFF);
            // bit tracks wrap: rotated by an odd number of bits the track still decodes to the same sectors
            rotate_bits(bits, nbits, 12345 + (uint32_t)t * 77, rot);
            uint8_t back[4096];
            static uint8_t rn[A2N_TRACK];
            uint32_t m = a2n_bits_nibbles(rot, nbits, rn, A2N_TRACK);
            CHECK_EQ(a2n_denibblize_ring(rn, m, order, t, back), 16);
            CHECK(!memcmp(back, sec.data(), 4096));
        }
    }
    // a .nib track as bits and back: the nibbles are the ring's, an FF that is part of a run of three or more (a gap, circular) is a self sync byte (FF and two zero
    // bits), the other FF stay 8 bits. The expected bits are built here by a different walk (every window of three FF marks its members).
    std::vector<uint8_t> sec = track_sectors(img, 3);
    a2n_nibblize_track(sec.data(), A2_ORDER_DOS, 3, ring);
    uint32_t nb = a2n_nib_bits(ring, bits);
    uint32_t want = nib_bits_expected(ring, A2N_TRACK, want_bits);
    CHECK_EQ(nb, want);
    CHECK(nb > A2N_BITS_MAX);                                         // the gaps of the ring (6 + 47 FF a sector) carry zeros
    CHECK(nb <= A2N_BUF_BITS);
    CHECK(!memcmp(bits, want_bits, (nb + 7) / 8));
    static uint8_t back[A2N_TRACK];
    CHECK_EQ(a2n_bits_nibbles(bits, nb, back, A2N_TRACK), A2N_TRACK);
    CHECK(!memcmp(back, ring, A2N_TRACK));
    {   // isolated FF and a pair stay 8 bits; a run of three and a run over the end of the ring are self sync
        static uint8_t r2[A2N_TRACK];
        memset(r2, 0xD5, sizeof(r2));
        r2[10] = 0xFF;                                                 // alone
        r2[20] = 0xFF; r2[21] = 0xFF;                                  // a pair
        r2[30] = r2[31] = r2[32] = 0xFF;                               // three
        r2[A2N_TRACK - 1] = 0xFF; r2[0] = 0xFF; r2[1] = 0xFF;          // three over the end
        uint32_t n2 = a2n_nib_bits(r2, bits);
        CHECK_EQ(n2, A2N_BITS_MAX + 2u * 6u);
        CHECK_EQ(nib_bits_expected(r2, A2N_TRACK, want_bits), n2);
        CHECK(!memcmp(bits, want_bits, (n2 + 7) / 8));
        CHECK_EQ(a2n_bits_nibbles(bits, n2, back, A2N_TRACK), A2N_TRACK);
        CHECK(!memcmp(back, r2, A2N_TRACK));
    }
    {   // a ring of FF only: the buffer is full before every FF has its zeros; the bits still hold all nibbles
        static uint8_t r3[A2N_TRACK];
        memset(r3, 0xFF, sizeof(r3));
        uint32_t n3 = a2n_nib_bits(r3, bits);
        CHECK(n3 <= A2N_BUF_BITS);
        CHECK(n3 > A2N_BITS_MAX);
        CHECK_EQ(a2n_bits_nibbles(bits, n3, back, A2N_TRACK), A2N_TRACK);
        CHECK(!memcmp(back, r3, A2N_TRACK));
    }
    // a track the guest wrote with 8-bit gaps (no sync zeros) is still read
    CHECK_EQ(a2n_bits_nibbles(bits, 0, back, A2N_TRACK), 0u);
}

// What a DOS 3.3 RWTS does to a .nib track (the failure of the Wasteland map write, T89.1): it overwrites the bits from the start of the gap behind an address field
// with 5 self sync bytes (FF and two zero bits), D5 AA AD, the 343 nibbles, DE AA EB and a part of an FF, and leaves the bits behind that. The next address field
// must still be read afterwards: with 8-bit FF in the gaps the written bits end at a bit offset that is not a multiple of eight from the old byte grid, the ones of the
// gap carry no zero to take the reader back to the grid and the D5 of the next sector is read as another nibble.
static void test_nib_splice()
{
    std::vector<uint8_t> img = make_image();
    static uint8_t ring[A2N_TRACK], bits[A2N_BUF_BITS / 8], back[4096], rn[A2N_TRACK];
    for (int track = 0; track < 3; track++) {
        std::vector<uint8_t> sec = track_sectors(img, track);
        a2n_nibblize_track(sec.data(), A2_ORDER_DOS, track, ring);
        uint32_t nb = a2n_nib_bits(ring, bits);
        nib_bits_expected(ring, A2N_TRACK, want_bits);                  // fills nib_pos_tab for the expanded form (the mutant's bits differ: the positions are looked up in the bits below)
        // the position of every nibble in the bits the application made: a walk with the slicer's rule
        static uint32_t at[A2N_TRACK];
        {
            uint32_t n = 0; bool inbyte = false; int cnt = 0;
            for (uint32_t i = 0; i < nb && n < A2N_TRACK; i++) {
                int b = (bits[i >> 3] >> (7 - (i & 7))) & 1;
                if (!inbyte) { if (b) { inbyte = true; cnt = 1; at[n] = i; } } else if (++cnt == 8) { inbyte = false; n++; }
            }
            CHECK_EQ(n, A2N_TRACK);
        }
        for (int p = 3; p < 13; p += 4) {
            uint8_t save[A2N_BUF_BITS / 8];
            memcpy(save, bits, sizeof(save));
            std::vector<uint8_t> w;                                     // the written bits
            auto putbyte = [&](uint8_t v) { for (int b = 7; b >= 0; b--) w.push_back((v >> b) & 1); };
            for (int i = 0; i < 5; i++) { putbyte(0xFF); w.push_back(0); w.push_back(0); }
            putbyte(0xD5); putbyte(0xAA); putbyte(0xAD);
            for (int i = 0; i < 343; i++) putbyte(ring[p * 416 + 23 + i]);
            putbyte(0xDE); putbyte(0xAA); putbyte(0xEB); putbyte(0xFF);
            uint32_t start = at[p * 416 + 14];                          // the first FF behind the address field
            for (size_t k = 0; k < w.size(); k++) {
                uint32_t b = start + (uint32_t)k;
                uint8_t m = (uint8_t)(0x80 >> (b & 7));
                if (w[k]) bits[b >> 3] |= m; else bits[b >> 3] &= (uint8_t)~m;
            }
            uint32_t m = a2n_bits_nibbles(bits, nb, rn, A2N_TRACK);
            CHECK(m > 0);
            int got = a2n_denibblize_ring(rn, m, A2_ORDER_DOS, track, back);
            CHECK_EQ(got, 16);                                          // the sector written and its neighbours: every address field is still found
            if (got == 16) CHECK(!memcmp(back, sec.data(), 4096));
            memcpy(bits, save, sizeof(save));
        }
    }
}

// ---- WOZ files built here (the layouts of the reference, written from the format, not from our reader)
static void put16(std::vector<uint8_t> &v, size_t at, uint32_t x) { v[at] = (uint8_t)x; v[at + 1] = (uint8_t)(x >> 8); }
static void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) { put16(v, at, x & 0xFFFF); put16(v, at + 2, x >> 16); }
struct WozTrack { std::vector<uint8_t> bits; uint32_t nbits; };
static std::vector<uint8_t> make_woz(int ver, const std::vector<WozTrack> &tr, const uint8_t tmap[160], int disk_type = 1, int wp = 0, bool flux = false)
{
    std::vector<uint8_t> v(12, 0);
    memcpy(&v[0], ver == 2 ? "WOZ2" : "WOZ1", 4);
    v[4] = 0xFF; v[5] = 0x0A; v[6] = 0x0D; v[7] = 0x0A;
    put32(v, 8, 0xDEADBEEF);
    auto chunk = [&](const char *id, uint32_t size) { size_t at = v.size(); v.resize(at + 8 + size, 0); memcpy(&v[at], id, 4); put32(v, at + 4, size); return at + 8; };
    size_t info = chunk("INFO", 60);
    v[info] = (uint8_t)ver; v[info + 1] = (uint8_t)disk_type; v[info + 2] = (uint8_t)wp;
    size_t tm = chunk("TMAP", 160);
    memcpy(&v[tm], tmap, 160);
    if (flux) chunk("FLUX", 160);
    if (ver == 2) {
        size_t trks = chunk("TRKS", 1280);
        for (size_t i = 0; i < tr.size(); i++) {
            size_t blocks = (tr[i].bits.size() + 511) / 512;
            size_t start = (v.size() + 511) / 512 * 512;
            v.resize(start + blocks * 512, 0);
            memcpy(&v[start], tr[i].bits.data(), tr[i].bits.size());
            put16(v, trks + i * 8, (uint32_t)(start / 512));
            put16(v, trks + i * 8 + 2, (uint32_t)blocks);
            put32(v, trks + i * 8 + 4, tr[i].nbits);
        }
        put32(v, 8, 0xDEADBEEF);
        // the TRKS chunk size covers the descriptors only here; the bits follow it, as in a real file the chunk size covers everything - fix up
        put32(v, trks - 4, (uint32_t)(v.size() - trks));
    } else {
        size_t trks = chunk("TRKS", (uint32_t)(tr.size() * 6656));
        for (size_t i = 0; i < tr.size(); i++) {
            memcpy(&v[trks + i * 6656], tr[i].bits.data(), tr[i].bits.size());
            put16(v, trks + i * 6656 + 6646, (uint32_t)tr[i].bits.size());
            put16(v, trks + i * 6656 + 6648, tr[i].nbits);
        }
    }
    return v;
}

static void test_woz()
{
    std::vector<uint8_t> img = make_image();
    static uint8_t abits[A2N_BITS_MAX / 8];
    std::vector<uint8_t> sec0 = track_sectors(img, 0);
    uint32_t an = a2n_sector_bits(sec0.data(), A2_ORDER_DOS, 0, abits);
    WozTrack A, B;
    A.bits.assign(abits, abits + (an + 7) / 8); A.nbits = an;
    B.nbits = 40003; B.bits.resize((B.nbits + 7) / 8);
    for (size_t i = 0; i < B.bits.size(); i++) B.bits[i] = (uint8_t)(i * 13 + 7);
    uint8_t tmap[160];
    memset(tmap, 0xFF, sizeof(tmap));
    tmap[0] = 0; tmap[1] = 0; tmap[3] = 1; tmap[4] = 1;                    // the 1.25 position (5) has no track
    char msg[200];
    for (int ver = 1; ver <= 2; ver++) {
        A2Model io; FakeFs fs;
        std::vector<uint8_t> w = make_woz(ver, { A, B }, tmap);
        fs.put("/usb1/g.woz", w);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/g.woz", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_DISK_CTL], 0x01);
        io.open_req(0, false, 2);                       // whole track 0 is quarter track 2 of the stepper
        CHECK(a2_service(c));
        CHECK_EQ(io.bits_reg(0), an);
        CHECK(!io.noise_reg(0));
        for (uint32_t i = 0; i < an; i++) { if (io.bit(0, i) != ((abits[i >> 3] >> (7 - (i & 7))) & 1)) { CHECK(false); break; } }
        CHECK_EQ(io.trk_writes[0], 1);
        // the same track at the neighbouring quarter position: not copied again
        io.open_req(0, false, 3);
        a2_service(c);
        CHECK_EQ(io.trk_writes[0], 1);
        CHECK_EQ(io.bits_reg(0), an);
        // a quarter track with no entry plays random flux
        io.open_req(0, false, 4);
        a2_service(c);
        CHECK(io.noise_reg(0));
        CHECK_EQ(io.trk_writes[0], 1);
        // track 1, from its slot, at q = 5, 6, 7
        io.open_req(0, false, 6);
        a2_service(c);
        CHECK(!io.noise_reg(0));
        CHECK_EQ(io.bits_reg(0), B.nbits);
        CHECK_EQ(io.trk_writes[0], 2);
        CHECK(!memcmp(io.trk[0].data(), B.bits.data(), B.bits.size()));
        io.open_req(0, false, 5);
        a2_service(c);
        CHECK_EQ(io.trk_writes[0], 2);
        io.open_req(0, false, 7);                    // index 5: no TMAP entry, random flux (and the slot of track 1 is not used for it)
        a2_service(c);
        CHECK(io.noise_reg(0));
        CHECK_EQ(io.trk_writes[0], 2);
        io.open_req(0, false, 6);                    // back to track 1: the noise request left no track in the buffer as far as the application knows, so it is written again
        a2_service(c);
        CHECK(!io.noise_reg(0));
        CHECK_EQ(io.bits_reg(0), B.nbits);
        CHECK_EQ(io.trk_writes[0], 3);
        // the guest wrote track 1: it goes back in place; the header CRC field is cleared on the first write, INFO and the other track stay
        for (size_t i = 100; i < 200; i++) io.trk[0][i] ^= 0xFF;
        io.dirty[0] = true; io.cur[0] = 6;
        io.open_req(0, true, 6);
        CHECK(a2_service(c));
        const std::vector<uint8_t> &f = fs.files["/usb1/g.woz"];
        std::vector<uint8_t> want = B.bits;
        for (size_t i = 100; i < 200; i++) want[i] ^= 0xFF;
        // find the track in the file again from its descriptor
        uint32_t at = 0;
        if (ver == 2) { at = (uint32_t)(f[256 + 8] | f[256 + 9] << 8) * 512u; } else { at = 256 + 6656; }
        // (the TRKS data offset is the same in both layouts: INFO, TMAP, TRKS follow each other from byte 12)
        CHECK(!memcmp(&f[at], want.data(), want.size()));
        CHECK_EQ(f[8] | f[9] | f[10] | f[11], 0);
        CHECK(!memcmp(&f[12], &w[12], 8 + 60 + 8 + 160 - 0));                 // INFO and TMAP untouched
        CHECK(!memcmp(&f[ver == 2 ? 3 * 512 : 256], &w[ver == 2 ? 3 * 512 : 256], ver == 2 ? 40 : 40));   // track 0 untouched
        // a quarter track that has no slot cannot be saved: dropped, acknowledged
        io.dirty[0] = true; io.cur[0] = 4;
        io.open_req(0, true, 4);
        a2_service(c);
        CHECK_EQ(io.acks[0], 9);
    }
    {   // write protect from INFO, a 3.5" disk, a file that is not WOZ, a FLUX chunk, a truncated file
        A2Model io; FakeFs fs;
        fs.put("/usb1/wp.woz", make_woz(2, { A }, tmap, 1, 1));
        fs.put("/usb1/35.woz", make_woz(2, { A }, tmap, 2));
        fs.put("/usb1/fx.woz", make_woz(2, { A }, tmap, 1, 0, true));
        std::vector<uint8_t> bad = make_woz(2, { A }, tmap);
        bad[0] = 'X';
        fs.put("/usb1/bad.woz", bad);
        fs.put_size("/usb1/short.woz", 100);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/wp.woz", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_DISK_CTL], 0x05);
        CHECK_EQ(a2_mount(c, "/usb1/35.woz", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "3.5") != 0);
        CHECK_EQ(a2_mount(c, "/usb1/bad.woz", 1, msg, sizeof(msg)), 1);
        CHECK_EQ(a2_mount(c, "/usb1/short.woz", 1, msg, sizeof(msg)), 1);
        CHECK(a2_drive_name(c, 1) == 0);
        // Q1: WOZ 3 / FLUX is refused with a message, in the file wherever the chunk is (here before TRKS, in fx3 at the end), and nothing is mounted
        CHECK_EQ(a2_mount(c, "/usb1/fx.woz", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "FLUX") != 0);
        std::vector<uint8_t> fx3 = make_woz(2, { A }, tmap);
        size_t at = fx3.size(); fx3.resize(at + 8 + 16, 0); memcpy(&fx3[at], "FLUX", 4); put32(fx3, at + 4, 16);
        fs.put("/usb1/fx3.woz", fx3);
        CHECK_EQ(a2_mount(c, "/usb1/fx3.woz", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "FLUX") != 0);
        std::vector<uint8_t> v3 = make_woz(2, { A }, tmap);
        v3[8 + 12 + 0] = 3;                          // INFO version 3 (WOZ 2.1 flux capture), no FLUX chunk read first
        fs.put("/usb1/v3.woz", v3);
        CHECK_EQ(a2_mount(c, "/usb1/v3.woz", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "WOZ 3") != 0);
        CHECK(a2_drive_name(c, 1) == 0);
    }
}

static void test_service()
{
    std::vector<uint8_t> img = make_image();
    char msg[200];
    static uint8_t want[A2N_BITS_MAX / 8];
    {   // mount, put a track (quarter track 68 = track 17), the neighbours of it, a half track, a save, order DOS
        A2Model io; FakeFs fs; io.lag = 3;
        fs.put("/usb1/game.dsk", img);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/game.dsk", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_DISK_CTL], 0x01);
        CHECK(!io.cmds.empty() && io.cmds.back() == 0x01);
        CHECK_STR(a2_drive_name(c, 0), "game.dsk");
        CHECK(a2_drive_name(c, 1) == 0);
        std::vector<uint8_t> sec0 = track_sectors(img, 0);
        static uint8_t want0[A2N_BITS_MAX / 8];
        a2n_sector_bits(sec0.data(), A2_ORDER_DOS, 0, want0);
        io.open_req(0, false, 70);                   // whole track 17
        CHECK(a2_service(c));
        std::vector<uint8_t> sec = track_sectors(img, 17);
        uint32_t nb = a2n_sector_bits(sec.data(), A2_ORDER_DOS, 17, want);
        CHECK_EQ(io.bits_reg(0), nb);
        CHECK(!io.noise_reg(0));
        CHECK(!memcmp(io.trk[0].data(), want, (nb + 7) / 8));
        CHECK_EQ(io.acks[0], 1);
        CHECK(!a2_service(c));                       // the lagging status of the closed request is not served again
        CHECK_EQ(io.acks[0], 1);
        CHECK_EQ(io.acks_on_idle, 0);
        io.open_req(0, false, 71);                   // a quarter beyond the track: the same data, not written again
        a2_service(c);
        io.open_req(0, false, 69);
        a2_service(c);
        CHECK_EQ(io.trk_writes[0], 1);
        CHECK_EQ(io.acks[0], 3);
        io.open_req(0, false, 72);                   // half track: random flux
        a2_service(c);
        CHECK(io.noise_reg(0));
        CHECK_EQ(io.trk_writes[0], 1);
        io.open_req(0, false, 74);                   // track 18
        a2_service(c);
        std::vector<uint8_t> sec18 = track_sectors(img, 18);
        nb = a2n_sector_bits(sec18.data(), A2_ORDER_DOS, 18, want);
        CHECK(!io.noise_reg(0));
        CHECK(!memcmp(io.trk[0].data(), want, (nb + 7) / 8));
        // the guest rewrote file sector 4 of track 18: the buffer comes back as a save and the file has the new sector
        std::vector<uint8_t> mod = sec18;
        for (int i = 0; i < 256; i++) mod[4 * 256 + i] = (uint8_t)(0xA0 ^ i);
        nb = a2n_sector_bits(mod.data(), A2_ORDER_DOS, 18, want);
        memcpy(io.trk[0].data(), want, (nb + 7) / 8);
        io.dirty[0] = true; io.cur[0] = 74;
        io.open_req(0, true, 74);
        CHECK(a2_service(c));
        CHECK(!io.dirty[0]);
        const std::vector<uint8_t> &f = fs.files["/usb1/game.dsk"];
        CHECK(!memcmp(&f[18 * 4096], mod.data(), 4096));
        CHECK(!memcmp(&f[0], &img[0], 18 * 4096));                       // nothing else changed
        CHECK(!memcmp(&f[19 * 4096], &img[19 * 4096], 16 * 4096));
        CHECK(!io.overrun_sel);
        // the stop below track 0: quarter tracks 0 and 1 show track 0
        io.open_req(0, false, 1);
        a2_service(c);
        CHECK(!io.noise_reg(0));
        CHECK(!memcmp(io.trk[0].data(), want0, 8));
        // garbage that does not decode is dropped with a notice and the file stays
        io.cur[0] = 74;
        io.open_req(0, false, 74);
        a2_service(c);
        memset(io.trk[0].data(), 0xAA, A2_TRACK_BYTES);
        io.dirty[0] = true;
        io.open_req(0, true, 74);
        uint32_t acks = (uint32_t)io.acks[0];
        a2_service(c);
        CHECK(!memcmp(&fs.files["/usb1/game.dsk"][18 * 4096], mod.data(), 4096));
        CHECK(c->notice_pending && strstr(c->notice, "decoded") != 0);
        CHECK_EQ(io.acks[0], (int)acks + 1);                              // and still acknowledged: the core must not hang
    }
    {   // a written track of which one sector does not decode: the 15 that do are kept, the one that does not keeps its old contents, with a notice
        A2Model io; FakeFs fs;
        fs.put("/usb1/part.dsk", img);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/part.dsk", 0, msg, sizeof(msg)), 0);
        io.open_req(0, false, 74);
        a2_service(c);
        std::vector<uint8_t> sec18 = track_sectors(img, 18), mod = sec18;
        for (int i = 0; i < 4096; i++) mod[i] = (uint8_t)(mod[i] ^ 0x5A);                    // the guest rewrote the whole track
        uint32_t nb = a2n_sector_bits(mod.data(), A2_ORDER_DOS, 18, want);
        uint32_t base = 9 * 3204 + 240 + 112 + 60 + 24 + 200 * 8 + 2;                       // inside the data field of physical sector 9
        want[base >> 3] ^= (uint8_t)(0x80 >> (base & 7));
        want[(base + 40) >> 3] ^= (uint8_t)(0x80 >> ((base + 40) & 7));
        memcpy(io.trk[0].data(), want, (nb + 7) / 8);
        io.dirty[0] = true; io.cur[0] = 74;
        io.open_req(0, true, 74);
        a2_service(c);
        const std::vector<uint8_t> &f = fs.files["/usb1/part.dsk"];
        int same_new = 0, same_old = 0;
        for (int s = 0; s < 16; s++) {
            if (!memcmp(&f[18 * 4096 + s * 256], &mod[s * 256], 256)) same_new++;
            if (!memcmp(&f[18 * 4096 + s * 256], &sec18[s * 256], 256)) same_old++;
        }
        CHECK_EQ(same_new, 15);
        CHECK_EQ(same_old, 1);
        CHECK(c->notice_pending && strstr(c->notice, "15 of 16") != 0);
    }
    {   // ProDOS order by extension, drive 2, eject flushes a dirty track while no request is open yet
        A2Model io; FakeFs fs;
        fs.put("/usb1/p.po", img);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/p.PO", 1, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_DISK_CTL], 0x02);
        io.open_req(1, false, 14);
        a2_service(c);
        std::vector<uint8_t> sec = track_sectors(img, 3);
        uint32_t nb = a2n_sector_bits(sec.data(), A2_ORDER_PRODOS, 3, want);
        CHECK(!memcmp(io.trk[1].data(), want, (nb + 7) / 8));
        std::vector<uint8_t> mod = sec;
        mod[100] ^= 0xFF;
        nb = a2n_sector_bits(mod.data(), A2_ORDER_PRODOS, 3, want);
        memcpy(io.trk[1].data(), want, (nb + 7) / 8);
        io.dirty[1] = true; io.cur[1] = 14;
        CHECK_EQ(a2_eject(c, 1, msg, sizeof(msg)), 0);
        CHECK(!memcmp(&fs.files["/usb1/p.po"][3 * 4096], mod.data(), 4096));
        CHECK(fs.syncs >= 1);
        CHECK(a2_drive_name(c, 1) == 0);
        CHECK_EQ(io.regs[A2_DISK_CTL], 0x00);
        CHECK(io.cmds.back() == 0x02);                                   // image changed 2
    }
    {   // .nib: the nibbles as 8-bit cells both ways
        A2Model io; FakeFs fs;
        std::vector<uint8_t> nib(232960);
        for (size_t i = 0; i < nib.size(); i++) nib[i] = (uint8_t)(0x80 | ((i * 7 + (i >> 8)) & 0x7F));
        fs.put("/usb1/x.nib", nib);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/x.nib", 0, msg, sizeof(msg)), 0);
        io.open_req(0, false, 38);
        a2_service(c);
        CHECK_EQ(io.bits_reg(0), 53248u);
        CHECK(!memcmp(io.trk[0].data(), &nib[9 * 6656], 6656));
        for (int i = 0; i < 6656; i++) io.trk[0][i] = (uint8_t)(0x80 | ((io.trk[0][i] + 0x11) & 0x7F));
        io.dirty[0] = true; io.cur[0] = 38;
        io.open_req(0, true, 38);
        a2_service(c);
        CHECK(!memcmp(&fs.files["/usb1/x.nib"][9 * 6656], io.trk[0].data(), 6656));
        CHECK(!memcmp(&fs.files["/usb1/x.nib"][8 * 6656], &nib[8 * 6656], 6656));
    }
    {   // T89.1 round trip of a .nib / .dsk track: the core writes a sector (bits), the head leaves (save_track), comes back (put_track): all 16 sectors decode
        for (int kind = 0; kind < 2; kind++) {
            A2Model io; FakeFs fs;
            const bool nibf = kind == 0;
            std::vector<uint8_t> file;
            if (nibf) {
                file.assign(232960, 0xFF);
                for (int t = 0; t < 35; t++) {
                    std::vector<uint8_t> s = track_sectors(img, t);
                    a2n_nibblize_track(s.data(), A2_ORDER_DOS, t, &file[t * 6656]);
                }
            } else {
                file = img;
            }
            const char *path = nibf ? "/usb1/rt.nib" : "/usb1/rt.dsk";
            fs.put(path, file);
            CoreCtx *c = mkctx(io, fs);
            CHECK_EQ(a2_mount(c, path, 0, msg, sizeof(msg)), 0);
            for (int t = 3; t < 33; t += 6) {
                io.open_req(0, false, 4 * t + 2);
                a2_service(c);
                uint32_t nb = io.bits_reg(0);
                std::vector<uint8_t> mod = track_sectors(img, t);
                for (int i = 0; i < 256; i++) mod[5 * 256 + i] = (uint8_t)(i * 11 + 90);
                // the written sector: the data field of file sector 5 replaced in the bit stream by the bits of the modified track
                static uint8_t nw[A2N_BUF_BITS / 8], ring[A2N_TRACK];
                uint32_t nn;
                if (nibf) {
                    a2n_nibblize_track(mod.data(), A2_ORDER_DOS, t, ring);
                    nn = a2n_nib_bits(ring, nw);
                } else {
                    nn = a2n_sector_bits(mod.data(), A2_ORDER_DOS, t, nw);
                }
                CHECK_EQ(nn, nb);
                memcpy(io.trk[0].data(), nw, (nn + 7) / 8);
                io.dirty[0] = true; io.cur[0] = (uint8_t)(4 * t + 2);
                io.open_req(0, true, 4 * t + 2);
                CHECK(a2_service(c));
                io.open_req(0, false, 4 * (t + 1) + 2);      // the head steps away ...
                a2_service(c);
                io.open_req(0, false, 4 * t + 2);            // ... and back
                a2_service(c);
                CHECK_EQ(io.bits_reg(0), nn);
                static uint8_t back[A2N_TRACK + 8];
                uint32_t m = a2n_bits_nibbles(io.trk[0].data(), io.bits_reg(0), back, A2N_TRACK);
                static uint8_t got[4096];
                int r = a2n_denibblize_ring(back, m, A2_ORDER_DOS, t, got);
                snprintf(msg, sizeof(msg), "roundtrip %s track %d: %u nibbles, denibblize %d", nibf ? "nib" : "dsk", t, m, r);
                if (r != 16) printf("%s\n", msg); CHECK(r == 16);
                CHECK(r == 16 && !memcmp(got, mod.data(), 4096));
            }
        }
    }
    {   // a .2mg of a floppy: the 64 byte header is skipped, the format byte picks the order
        A2Model io; FakeFs fs;
        std::vector<uint8_t> mg(64, 0);
        memcpy(&mg[0], "2IMG", 4);
        put32(mg, 0x0C, 1); put32(mg, 0x18, 64); put32(mg, 0x1C, 143360);
        mg.insert(mg.end(), img.begin(), img.end());
        fs.put("/usb1/f.2mg", mg);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/f.2mg", 0, msg, sizeof(msg)), 0);
        io.open_req(0, false, 10);
        a2_service(c);
        std::vector<uint8_t> sec = track_sectors(img, 2);
        uint32_t nb = a2n_sector_bits(sec.data(), A2_ORDER_PRODOS, 2, want);
        CHECK(!memcmp(io.trk[0].data(), want, (nb + 7) / 8));
        // a hard disk volume is not a floppy
        std::vector<uint8_t> hd(64, 0);
        memcpy(&hd[0], "2IMG", 4);
        put32(hd, 0x0C, 1); put32(hd, 0x18, 64); put32(hd, 0x1C, 512 * 100);
        hd.resize(64 + 512 * 100);
        fs.put("/usb1/h.2mg", hd);
        CHECK_EQ(a2_mount(c, "/usb1/h.2mg", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "slot 7") != 0);
    }
    {   // write protect: a read only medium sets the bit, a saved track is dropped with a notice, the file is intact
        A2Model io; FakeFs fs;
        fs.put("/usb1/ro.dsk", img);
        fs.read_only = true;
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/ro.dsk", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_DISK_CTL], 0x05);
        io.open_req(0, false, 10);
        a2_service(c);
        std::vector<uint8_t> mod = track_sectors(img, 2);
        mod[0] ^= 1;
        uint32_t nb = a2n_sector_bits(mod.data(), A2_ORDER_DOS, 2, want);
        memcpy(io.trk[0].data(), want, (nb + 7) / 8);
        io.open_req(0, true, 10);
        a2_service(c);
        CHECK(fs.files["/usb1/ro.dsk"] == img);
        CHECK(c->notice_pending && strstr(c->notice, "read only") != 0);
        CHECK_EQ(io.acks[0], 2);
    }
    {   // sizes other than the two are refused, nothing is mounted, and a mounted disk stays; an older core is refused
        A2Model io; FakeFs fs;
        fs.put("/usb1/ok.dsk", img);
        fs.put_size("/usb1/bad.dsk", 143361);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_mount(c, "/usb1/ok.dsk", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(a2_mount(c, "/usb1/bad.dsk", 0, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "143361") != 0);
        CHECK_STR(a2_drive_name(c, 0), "ok.dsk");
        // Q1: a 3.5" image is not a Disk II image: refused with the way out named, the mounted disk stays
        fs.put_size("/usb1/m800.dsk", 819200);
        CHECK_EQ(a2_mount(c, "/usb1/m800.dsk", 0, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "3.5") != 0 && strstr(msg, "slot 7") != 0);
        CHECK_STR(a2_drive_name(c, 0), "ok.dsk");
        CHECK_EQ(a2_mount(c, "/usb1/ok.dsk", 2, msg, sizeof(msg)), 1);
        CHECK_EQ(a2_mount(c, "/usb1/none.dsk", 0, msg, sizeof(msg)), 1);
        io.regs[A2_CAPS2] = 0;
        CHECK_EQ(a2_mount(c, "/usb1/ok.dsk", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "older") != 0);
    }
}

// ---- slot 7: the ProDOS hard disk
static void test_hdd()
{
    char msg[200];
    std::vector<uint8_t> vol(512 * 2000);
    for (size_t i = 0; i < vol.size(); i++) vol[i] = (uint8_t)(i * 31 + (i >> 9) * 7 + 3);
    {   // mount: sizes, protect bits, the card's ROM; two units; read, write, errors
        A2Model io; FakeFs fs;
        fs.put("/usb1/a.hdv", vol);
        fs.put("/usb1/b.po", std::vector<uint8_t>(512 * 50, 0x5A));
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/a.hdv", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x01);
        CHECK_EQ(io.regs[A2_HDD_SIZE(0)] | io.regs[A2_HDD_SIZE(0) + 1] << 8, 2000);
        CHECK(!memcmp(io.hrom.data(), a2_hdd_rom, 256));
        CHECK_STR(a2_unit_name(c, 0), "a.hdv");
        CHECK_EQ(a2_hdd_mount(c, "/usb1/b.po", 1, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x03);
        CHECK_EQ(io.regs[A2_HDD_SIZE(1)], 50);
        // READ block 0x0123 of unit 1 to $2000
        io.open_hdd(0, false, 0x123, 0x2000);
        io.hlag = 2;
        CHECK(a2_service(c));
        CHECK(!memcmp(&io.ram[0x2000], &vol[0x123 * 512], 512));
        CHECK_EQ(io.hresults.size(), 1u);
        CHECK_EQ((int)io.hresults.back(), 0);
        CHECK(!a2_service(c));                       // the status lags: not served again
        CHECK_EQ(io.hacks, 1);
        // WRITE block 5 of unit 1 from $3000
        for (int i = 0; i < 512; i++) io.ram[0x3000 + i] = (uint8_t)(0xC0 ^ i);
        io.open_hdd(0, true, 5, 0x3000);
        CHECK(a2_service(c));
        CHECK(!memcmp(&fs.files["/usb1/a.hdv"][5 * 512], &io.ram[0x3000], 512));
        CHECK(!memcmp(&fs.files["/usb1/a.hdv"][4 * 512], &vol[4 * 512], 512));
        CHECK(!memcmp(&fs.files["/usb1/a.hdv"][6 * 512], &vol[6 * 512], 512));
        // language card bank 1 selected (S13): the CPU's $D000-$DFFF is the RAM $C000-$CFFF, per byte; a buffer straddling $D000 and $E000 splits
        io.hbank1 = true;
        io.open_hdd(0, false, 9, 0xCF00);              // $CF00-$CFFF stays, $D000-$D0FF -> $C000-$C0FF
        a2_service(c);
        CHECK(!memcmp(&io.ram[0xCF00], &vol[9 * 512], 256));
        CHECK(!memcmp(&io.ram[0xC000], &vol[9 * 512 + 256], 256));
        io.open_hdd(0, false, 10, 0xDC00);             // the ProDOS volume directory buffer: lands at $CC00
        a2_service(c);
        CHECK(!memcmp(&io.ram[0xCC00], &vol[10 * 512], 512));
        io.open_hdd(0, false, 11, 0xDF00);             // $DF00-$DFFF -> $CF00, $E000-$E0FF stays
        a2_service(c);
        CHECK(!memcmp(&io.ram[0xCF00], &vol[11 * 512], 256));
        CHECK(!memcmp(&io.ram[0xE000], &vol[11 * 512 + 256], 256));
        for (int i = 0; i < 512; i++) io.ram[0xCC00 + i] = (uint8_t)(0x3C + i);   // write to disk reads the same way
        io.open_hdd(0, true, 12, 0xDC00);
        a2_service(c);
        CHECK(!memcmp(&fs.files["/usb1/a.hdv"][12 * 512], &io.ram[0xCC00], 512));
        io.hbank1 = false;
        io.open_hdd(0, false, 13, 0xDC00);             // bank 2: the physical address
        a2_service(c);
        CHECK(!memcmp(&io.ram[0xDC00], &vol[13 * 512], 512));
        // unit 2 (bit 7 of the unit byte): its own image
        io.open_hdd(1, false, 49, 0x4000);
        a2_service(c);
        CHECK_EQ(io.ram[0x4000], 0x5A);
        // a block past the end of the volume: ProDOS error $27, nothing in RAM changes
        io.ram[0x5000] = 0x77;
        io.open_hdd(1, false, 50, 0x5000);
        a2_service(c);
        CHECK_EQ((int)io.hresults.back(), 0x27);
        CHECK_EQ(io.ram[0x5000], 0x77);
        // eject unit 1: the card still has unit 2, then none: the ROM goes
        CHECK_EQ(a2_hdd_eject(c, 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x02);
        CHECK(!memcmp(io.hrom.data(), a2_hdd_rom, 256));
        CHECK_EQ(a2_hdd_eject(c, 1, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x00);
        bool zero = true;
        for (int i = 0; i < 256; i++) if (io.hrom[i]) zero = false;
        CHECK(zero);
        CHECK(a2_unit_name(c, 0) == 0);
        CHECK(fs.syncs >= 1);
        // a request that was open while the last unit was ejected is still answered (error $28: no device), or the CPU would wait for ever
        io.open_hdd(0, false, 3, 0x6000);
        CHECK(a2_service(c));
        CHECK_EQ((int)io.hresults.back(), 0x28);
        CHECK(!io.hreq);
    }
    {   // a read only volume: a write is refused with $2B and the file stays; 2MG header skipped; a buffer that wraps the 64 KB
        A2Model io; FakeFs fs;
        std::vector<uint8_t> mg(64, 0);
        memcpy(&mg[0], "2IMG", 4);
        put32(mg, 0x0C, 1); put32(mg, 0x18, 64); put32(mg, 0x1C, (uint32_t)vol.size());
        mg.insert(mg.end(), vol.begin(), vol.end());
        fs.put("/usb1/v.2mg", mg);
        fs.read_only = true;
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/v.2mg", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x05);
        CHECK_EQ(io.regs[A2_HDD_SIZE(0)] | io.regs[A2_HDD_SIZE(0) + 1] << 8, 2000);
        io.open_hdd(0, false, 7, 0xFF00);
        a2_service(c);
        CHECK(!memcmp(&io.ram[0xFF00], &vol[7 * 512], 256));
        CHECK(!memcmp(&io.ram[0], &vol[7 * 512 + 256], 256));
        CHECK(!io.overrun);
        io.open_hdd(0, true, 7, 0x2000);
        a2_service(c);
        CHECK_EQ((int)io.hresults.back(), 0x2B);
        CHECK(fs.files["/usb1/v.2mg"] == mg);
    }
    {   // refusals: not whole blocks, a DOS order 2MG, an older core; a volume over 32 MB is cut to 65,535 blocks with a notice
        A2Model io; FakeFs fs;
        fs.put_size("/usb1/odd.hdv", 512 * 10 + 1);
        std::vector<uint8_t> mg(64, 0);
        memcpy(&mg[0], "2IMG", 4);
        put32(mg, 0x0C, 0); put32(mg, 0x18, 64); put32(mg, 0x1C, 512 * 10);
        mg.resize(64 + 512 * 10);
        fs.put("/usb1/dos.2mg", mg);
        fs.put_size("/usb1/big.hdv", 40u << 20);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/odd.hdv", 0, msg, sizeof(msg)), 1);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/dos.2mg", 0, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "ProDOS order") != 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x00);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/big.hdv", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_SIZE(0)] | io.regs[A2_HDD_SIZE(0) + 1] << 8, 65535);
        CHECK(c->notice_pending && strstr(c->notice, "32 MB") != 0);
        // the file has more blocks than the card can address: block 65535 does not exist for the card, 65534 does
        io.open_hdd(0, false, 65534, 0x2000);
        a2_service(c);
        CHECK_EQ((int)io.hresults.back(), 0);
        io.open_hdd(0, false, 65535, 0x2000);
        a2_service(c);
        CHECK_EQ((int)io.hresults.back(), 0x27);
        io.regs[A2_CAPS2] = 0;
        CHECK_EQ(a2_hdd_mount(c, "/usb1/odd.hdv", 1, msg, sizeof(msg)), 1);
        CHECK(strstr(msg, "no slot 7") != 0);
    }
    {   // Q1: 3.5" ProDOS block images (800 KB .po / .2mg) are a slot 7 unit: read, write, the boot block; other 3.5" images are refused
        A2Model io; FakeFs fs;
        std::vector<uint8_t> m(512 * 1600);
        for (size_t i = 0; i < m.size(); i++) m[i] = (uint8_t)(i * 13 + (i >> 9) * 5 + 1);
        fs.put("/usb1/m800.po", m);
        std::vector<uint8_t> mg(64, 0);
        memcpy(&mg[0], "2IMG", 4);
        put32(mg, 0x0C, 1); put32(mg, 0x18, 64); put32(mg, 0x1C, (uint32_t)m.size());
        mg.insert(mg.end(), m.begin(), m.end());
        fs.put("/usb1/m800.2mg", mg);
        std::vector<uint8_t> dos = mg;
        put32(dos, 0x0C, 0);
        fs.put("/usb1/m800dos.2mg", dos);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_kind_of(819200, ".po", 0), A2K_HDD);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/m800.po", 0, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_SIZE(0)] | io.regs[A2_HDD_SIZE(0) + 1] << 8, 1600);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x01);
        io.open_hdd(0, false, 0, 0x0800);                                  // the boot block
        CHECK(a2_service(c));
        CHECK_EQ((int)io.hresults.back(), 0);
        CHECK(!memcmp(&io.ram[0x0800], &m[0], 512));
        io.open_hdd(0, false, 1599, 0x2000);
        a2_service(c);
        CHECK(!memcmp(&io.ram[0x2000], &m[1599 * 512], 512));
        memset(&io.ram[0x3000], 0x6B, 512);
        io.open_hdd(0, true, 1599, 0x3000);
        a2_service(c);
        CHECK_EQ((int)io.hresults.back(), 0);
        CHECK(!memcmp(&fs.files["/usb1/m800.po"][1599 * 512], &io.ram[0x3000], 512));
        io.open_hdd(0, false, 1600, 0x2000);                               // past the end of the 3.5" disk
        a2_service(c);
        CHECK_EQ((int)io.hresults.back(), 0x27);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/m800.2mg", 1, msg, sizeof(msg)), 0);
        CHECK_EQ(io.regs[A2_HDD_CTL], 0x03);
        CHECK_EQ(a2_kind_of(64 + 819200, ".2mg", mg.data()), A2K_HDD);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/m800dos.2mg", 1, msg, sizeof(msg)), 1);      // not a block image: refused, unit 2 stays
        CHECK(strstr(msg, "3.5") != 0 && strstr(msg, "block image") != 0);
        CHECK_STR(a2_unit_name(c, 1), "m800.2mg");
    }
    {   // what the file browser offers
        uint8_t h2[64] = { 0 };
        memcpy(h2, "2IMG", 4);
        h2[0x18] = 64; h2[0x1C] = 0x00; h2[0x1D] = 0x30; h2[0x1E] = 0x02;       // data offset 64, 143360 = 0x23000 bytes
        CHECK_EQ(a2_kind_of(143360, ".dsk", 0), A2K_FLOPPY);
        CHECK_EQ(a2_kind_of(143360, ".po", 0), A2K_FLOPPY);
        CHECK_EQ(a2_kind_of(143360 * 2, ".po", 0), A2K_HDD);
        CHECK_EQ(a2_kind_of(512 * 2000, ".hdv", 0), A2K_HDD);
        CHECK_EQ(a2_kind_of(1, ".txt", 0), A2K_NONE);
        CHECK_EQ(a2_kind_of(143360 + 64, ".2mg", h2), A2K_FLOPPY);
    }
}

// ---- T180 D4 / D1 (S7): everything the drives and the slot 7 units hold goes back to its file before the medium is let go; a medium that refuses it keeps
// the disk mounted, stops the eject and the replacement, and says why; a track the file did not take is kept and written when the medium is back
static int g_sv_on, g_sv_off;
static void sv_hook(CoreCtx *, bool on) { (on ? g_sv_on : g_sv_off)++; }

static void test_writeback()
{
    std::vector<uint8_t> img = make_image();
    std::vector<uint8_t> other = img;
    other[5] ^= 0x3C;
    char msg[200];
    // a mounted disk whose track 18 the guest rewrote: the new bits are in the core's buffer, nothing of it is in the file
    auto dirty_floppy = [&](A2Model &io, FakeFs &fs, CoreCtx *c, std::vector<uint8_t> &mod) {
        fs.put("/usb1/w.dsk", img);
        fs.put("/usb1/o.dsk", other);
        CHECK_EQ(a2_mount(c, "/usb1/w.dsk", 0, msg, sizeof(msg)), 0);
        io.open_req(0, false, 74);
        a2_service(c);
        mod = track_sectors(img, 18);
        for (int i = 0; i < 256; i++) mod[4 * 256 + i] = (uint8_t)(0xA0 ^ i);
        uint32_t nb = a2n_sector_bits(mod.data(), A2_ORDER_DOS, 18, want_bits);
        memcpy(io.trk[0].data(), want_bits, (nb + 7) / 8);
        io.dirty[0] = true;
        io.cur[0] = 74;
    };
    auto has_mod = [&](FakeFs &fs, const std::vector<uint8_t> &mod) { return !memcmp(&fs.files["/usb1/w.dsk"][18 * 4096], mod.data(), 4096); };
    {   // the common path: the dirty track goes out, the disk stays mounted
        A2Model io; FakeFs fs;
        CoreCtx *c = mkctx(io, fs);
        std::vector<uint8_t> mod;
        dirty_floppy(io, fs, c, mod);
        CHECK(a2_media_pending(c));
        CHECK(!has_mod(fs, mod));
        c->saving = sv_hook;
        g_sv_on = g_sv_off = 0;
        CHECK_EQ(a2_media_flush(c, msg, sizeof(msg)), 0);
        CHECK(g_sv_on >= 1 && g_sv_on == g_sv_off);                                  // "Saving media..." while the track went out
        g_sv_on = 0;
        CHECK_EQ(a2_media_flush(c, msg, sizeof(msg)), 0);
        CHECK_EQ(g_sv_on, 0);                                                         // nothing to write: nothing said
        CHECK(has_mod(fs, mod));
        CHECK(!a2_media_pending(c));
        CHECK(a2_drive_name(c, 0) != 0);
        CHECK(fs.syncs >= 1);
    }
    {   // the medium refuses the sync: the disk stays in the drive for the eject, the replacement and the common path; it goes through once the medium is back
        A2Model io; FakeFs fs;
        CoreCtx *c = mkctx(io, fs);
        std::vector<uint8_t> mod;
        dirty_floppy(io, fs, c, mod);
        fs.sync_fails = true;
        CHECK(a2_media_flush(c, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "D1") != 0 && strstr(msg, "stays mounted") != 0);
        CHECK(a2_media_pending(c));
        CHECK(a2_eject(c, 0, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "D1") != 0);
        CHECK_STR(a2_drive_name(c, 0), "w.dsk");
        int open = fs.open_files;
        CHECK(a2_mount(c, "/usb1/o.dsk", 0, msg, sizeof(msg)) != 0);                 // D1: the replacement does not let the old disk go either
        CHECK_STR(a2_drive_name(c, 0), "w.dsk");
        CHECK_EQ(fs.open_files, open);                                               // and the new file is not left open
        CHECK_EQ(io.regs[A2_DISK_CTL] & 1, 1);                                       // the core still has its disk
        fs.sync_fails = false;
        CHECK_EQ(a2_eject(c, 0, msg, sizeof(msg)), 0);
        CHECK(has_mod(fs, mod));
        CHECK(a2_drive_name(c, 0) == 0);
        CHECK_EQ(fs.open_files, 0);
    }
    {   // the medium refuses the track itself (full, write protected after the mount): the bits are kept and written when it is back
        A2Model io; FakeFs fs;
        CoreCtx *c = mkctx(io, fs);
        std::vector<uint8_t> mod;
        dirty_floppy(io, fs, c, mod);
        fs.read_only = true;
        CHECK(a2_media_flush(c, msg, sizeof(msg)) != 0);                             // the core handed the track over, the file did not take it
        CHECK(!io.dirty[0]);                                                          // the core has nothing left: only the application holds it now
        CHECK(!has_mod(fs, mod));
        CHECK(a2_media_pending(c));
        CHECK(a2_eject(c, 0, msg, sizeof(msg)) != 0);
        CHECK(a2_mount(c, "/usb1/o.dsk", 0, msg, sizeof(msg)) != 0);
        CHECK_STR(a2_drive_name(c, 0), "w.dsk");
        fs.read_only = false;
        CHECK_EQ(a2_eject(c, 0, msg, sizeof(msg)), 0);                                // the same eject, the medium is back
        CHECK(has_mod(fs, mod));                                                      // nothing was lost on the way
        CHECK(a2_drive_name(c, 0) == 0);
    }
    {   // a slot 7 unit: a block written, the sync refused
        A2Model io; FakeFs fs;
        std::vector<uint8_t> vol(512 * 100, 0x11);
        fs.put("/usb1/a.hdv", vol);
        fs.put("/usb1/b.hdv", vol);
        CoreCtx *c = mkctx(io, fs);
        CHECK_EQ(a2_hdd_mount(c, "/usb1/a.hdv", 0, msg, sizeof(msg)), 0);
        for (int i = 0; i < 512; i++) io.ram[0x3000 + i] = (uint8_t)(0xC0 ^ i);
        io.open_hdd(0, true, 5, 0x3000);
        a2_service(c);
        CHECK(a2_media_pending(c));
        fs.sync_fails = true;
        CHECK(a2_media_flush(c, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "S7.1") != 0 && strstr(msg, "stays mounted") != 0);
        CHECK(a2_hdd_eject(c, 0, msg, sizeof(msg)) != 0);
        CHECK(strstr(msg, "S7.1") != 0);
        CHECK_STR(a2_unit_name(c, 0), "a.hdv");
        int open = fs.open_files;
        CHECK(a2_hdd_mount(c, "/usb1/b.hdv", 0, msg, sizeof(msg)) != 0);
        CHECK_STR(a2_unit_name(c, 0), "a.hdv");
        CHECK_EQ(fs.open_files, open);
        CHECK_EQ(io.regs[A2_HDD_CTL] & 1, 1);
        fs.sync_fails = false;
        CHECK_EQ(a2_media_flush(c, msg, sizeof(msg)), 0);
        CHECK(!a2_media_pending(c));
        CHECK_EQ(a2_hdd_eject(c, 0, msg, sizeof(msg)), 0);
        CHECK(!memcmp(&fs.files["/usb1/a.hdv"][5 * 512], &io.ram[0x3000], 512));
    }
    {   // nothing pending: nothing is written
        A2Model io; FakeFs fs;
        CoreCtx *c = mkctx(io, fs);
        std::vector<uint8_t> mod;
        dirty_floppy(io, fs, c, mod);
        CHECK_EQ(a2_media_flush(c, msg, sizeof(msg)), 0);
        int s = fs.syncs, w = fs.writes;
        CHECK(!a2_media_pending(c));
        CHECK_EQ(a2_media_flush(c, msg, sizeof(msg)), 0);
        CHECK_EQ(fs.syncs, s);
        CHECK_EQ(fs.writes, w);
    }
}

int main()
{
    test_nibblizer();
    test_bits();
    test_nib_splice();
    test_woz();
    test_service();
    test_hdd();
    test_writeback();
    DONE("apple2_disk");
}
