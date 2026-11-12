// T104.1 D15 (cbm1541): the disk in memory - the GCR code, the track layout, the decoder, the .g64 file, the .d64 and .d81 blank images.
// The vectors of the code are computed by a second implementation (a script with the 5 bit table, not the library's), the 1541's own
// header and data block markers ($52 and $55) are the first bytes of known blocks.
#include "check.h"
#include "fakes.h"
#include "d1541_gcr.h"
#include <vector>

typedef std::vector<uint8_t> Bytes;

// A disk whose sectors outside the directory track carry a pattern of their own.
static void fill(BinImage &b)
{
    for (int t = 1; t <= b.num_tracks; t++) {
        if (t == 18) continue;
        for (int s = 0; s < d64_sectors(t - 1); s++) {
            uint8_t *p = b.sector(t, s);
            for (int i = 0; i < 256; i++) p[i] = (uint8_t)(t * 31 + s * 7 + i * 3);
        }
    }
}

int main()
{
    // ---- the code: known blocks ----
    {
        struct V { uint8_t bin[4]; uint8_t gcr[5]; };
        static const V v[] = {
            { { 0x08, 0x00, 0x00, 0x00 }, { 0x52, 0x54, 0xa5, 0x29, 0x4a } },       // a header block starts with $52
            { { 0x07, 0x00, 0x00, 0x00 }, { 0x55, 0xd4, 0xa5, 0x29, 0x4a } },       // a data block starts with $55
            { { 0xff, 0xff, 0xff, 0xff }, { 0xad, 0x6b, 0x5a, 0xd6, 0xb5 } },
            { { 0x01, 0x23, 0x45, 0x67 }, { 0x52, 0xe5, 0x37, 0x3e, 0xd7 } },
            { { 0x08, 0x10, 0x01, 0x01 }, { 0x52, 0x56, 0xa5, 0x2d, 0x4b } },
        };
        for (const V &x : v) {
            uint8_t g[5], b[4];
            gcr_encode4(x.bin, g);
            CHECK(!memcmp(g, x.gcr, 5));
            CHECK_EQ(gcr_decode5(x.gcr, b), 0);
            CHECK(!memcmp(b, x.bin, 4));
        }
        // every pattern of four bytes comes back (a sample over all 65536 of the first two bytes)
        int bad = 0;
        for (int a = 0; a < 256; a++) {
            for (int c = 0; c < 256; c++) {
                uint8_t in[4] = { (uint8_t)a, (uint8_t)c, (uint8_t)(a ^ c), (uint8_t)(a + 3 * c) }, g[5], out[4];
                gcr_encode4(in, g);
                if (gcr_decode5(g, out) || memcmp(in, out, 4)) bad++;
            }
        }
        CHECK_EQ(bad, 0);
        // a group of five bits that is no code is counted: 00000 in the first group
        uint8_t zero[5] = { 0, 0, 0, 0, 0 }, o[4];
        CHECK(gcr_decode5(zero, o) > 0);
    }

    // ---- geometry ----
    CHECK_EQ(d64_sectors(0), 21);
    CHECK_EQ(d64_sectors(17), 19);
    CHECK_EQ(d64_sectors(24), 18);
    CHECK_EQ(d64_sectors(30), 17);
    CHECK_EQ(d64_sectors(39), 17);
    CHECK_EQ(d64_sectors_before(17), 357);            // track 18 (index 17) starts at block 357
    CHECK_EQ(d64_sectors_before(35), 683);
    CHECK_EQ(d64_zone_speed(0), 3);
    CHECK_EQ(d64_zone_speed(34), 0);

    // ---- a formatted disk to GCR and back ----
    BinImage bin;
    bin.format("TEST DISK,AB", 35);
    fill(bin);
    std::vector<uint8_t> gm(GCR_MAXSIZE);
    GcrImage g(gm.data(), 0x00100000);
    g.from_bin(&bin, 2700);
    static const int lens[4] = { 0x1E0C, 0x1BE6, 0x1A0A, 0x186A };
    for (int t = 0; t < 35; t++) {
        const GcrTrack &tr = g.tracks[2 * t];
        CHECK(tr.track_address != 0);
        CHECK_EQ(tr.track_length, lens[t < 17 ? 0 : t < 24 ? 1 : t < 30 ? 2 : 3]);
        CHECK_EQ(tr.speed_zone, t < 17 ? 3 : t < 24 ? 2 : t < 30 ? 1 : 0);
        CHECK(tr.track_used);
        CHECK(!g.tracks[2 * t + 1].track_address);                   // no half tracks
    }
    // tracks 36 .. 40 are blank (zero, not used), as long as they fit
    CHECK(g.tracks[70].track_address != 0);
    CHECK(!g.tracks[70].track_used);
    CHECK_EQ(g.tracks[70].track_address[100], 0);
    CHECK(g.phys_of(g.tracks[2].track_address) == 0x00100000u + (uint32_t)(g.tracks[2].track_address - gm.data()));
    // the first header in track 1 decodes to the ID the disk carries: 08, checksum, sector, track, id2, id1
    {
        const uint8_t *t = g.tracks[0].track_address;
        int len = g.tracks[0].track_length, p = 0;
        for (; p < len - 10; p++) {
            if (t[p] == 0xFF && t[p + 1] == 0xFF && t[p + 2] == 0xFF && t[p + 3] == 0xFF && t[p + 4] == 0xFF && t[p + 5] == 0x52) break;
        }
        CHECK(p < len - 10);
        uint8_t h[8];
        gcr_decode5(t + p + 5, h);
        gcr_decode5(t + p + 10, h + 4);
        CHECK_EQ(h[0], 8);
        CHECK_EQ(h[3], 1);                                            // track 1
        CHECK_EQ(h[4], 'B');                                          // id2
        CHECK_EQ(h[5], 'A');                                          // id1
        CHECK_EQ(h[1], (uint8_t)(h[2] ^ h[3] ^ h[4] ^ h[5]));         // the header checksum
        CHECK_EQ(h[6], 0x0F);
    }
    // the decoder walks a track and finds every sector with a clean checksum
    {
        uint8_t out[21 * 256], st[64];
        memset(st, 0xEE, sizeof(st));
        int secs = GcrImage::decode_track(g.tracks[0].track_address, 1, g.tracks[0].track_length, 21, out, st, 64);
        CHECK_EQ(secs, 21);
        for (int i = 0; i < 21; i++) CHECK_EQ(st[2 * i + 1], 0);
    }
    BinImage back;
    int errs = g.to_bin(&back);
    CHECK_EQ(errs, 0);
    CHECK_EQ(back.num_tracks, 35);
    CHECK(!memcmp(back.bin_data, bin.bin_data, D64_35_NO_ERRORS));
    CHECK_EQ(back.file_bytes(), (uint32_t)D64_35_NO_ERRORS);

    // a flipped bit in the data of one sector: its checksum fails, the others stay clean, and the error byte is kept
    {
        std::vector<uint8_t> m2(GCR_MAXSIZE);
        GcrImage g3(m2.data(), 0x200000);
        g3.from_bin(&bin, 2700);
        uint8_t *t = g3.tracks[2 * 4].track_address;                  // track 5
        int len = g3.tracks[2 * 4].track_length, p = 0, n = 0;
        for (; p < len - 20; p++) {
            if (t[p] == 0xFF && t[p + 1] == 0xFF && t[p + 2] == 0xFF && t[p + 3] == 0xFF && t[p + 4] == 0xFF && t[p + 5] == 0x55) {
                if (++n == 3) break;                                  // the third data block
            }
        }
        CHECK(p < len - 20);
        t[p + 5 + 100] ^= 0x04;                                       // flips a data bit (or makes an invalid code)
        BinImage b2;
        int e = g3.to_bin(&b2);
        CHECK(e >= 1);
        CHECK(b2.errors != 0);
        int bad = 0;
        for (int s = 0; s < d64_sectors(4); s++) bad += b2.errors[d64_sectors_before(4) + s] != 0;
        CHECK_EQ(bad, 1);
        CHECK(b2.file_bytes() > (uint32_t)D64_35_NO_ERRORS);          // the error bytes go to the file
    }

    // error bytes of a .d64 become faults of the track: code 5 is a data checksum error, 3 has no sync, 2 no header
    {
        BinImage e;
        e.format("ERR", 35);
        fill(e);
        // an image with error bytes: 175531 bytes
        std::vector<uint8_t> file(D64_35_WITH_ERRORS, 0);
        memcpy(file.data(), e.bin_data, D64_35_NO_ERRORS);
        file[D64_35_NO_ERRORS + d64_sectors_before(0) + 3] = 5;
        FakeFs fs;
        fs.put("/e.d64", file);
        CoreFile *f = fs.open("/e.d64", false);
        BinImage ld;
        CHECK_EQ(ld.load(f), 0);
        fs.close(f);
        CHECK_EQ(ld.num_tracks, 35);
        CHECK(ld.errors != 0);
        CHECK_EQ(ld.error_size, 683);
        std::vector<uint8_t> m(GCR_MAXSIZE);
        GcrImage ge(m.data(), 0x300000);
        ge.from_bin(&ld, 2700);
        uint8_t out[21 * 256], st[64];
        memset(st, 0xEE, sizeof(st));
        int secs = GcrImage::decode_track(ge.tracks[0].track_address, 1, ge.tracks[0].track_length, 21, out, st, 64);
        CHECK_EQ(secs, 21);
        int flagged = 0;
        for (int i = 0; i < 21; i++) flagged += st[2 * i + 1] != 0;
        CHECK_EQ(flagged, 1);                                         // sector 3 only
    }

    // sizes
    {
        BinImage s;
        CHECK_EQ(s.init(D64_35_NO_ERRORS), 0);
        CHECK_EQ(s.num_tracks, 35);
        CHECK(s.errors == 0);
        CHECK_EQ(s.init(D64_35_WITH_ERRORS), 0);
        CHECK_EQ(s.num_tracks, 35);
        CHECK(s.errors != 0);
        CHECK_EQ(s.init(D64_40_NO_ERRORS), 0);
        CHECK_EQ(s.num_tracks, 40);
        CHECK_EQ(s.init(D64_40_WITH_ERRORS), 0);
        CHECK_EQ(s.num_tracks, 40);
        CHECK_EQ(s.error_size, 768);
        CHECK(s.init(349696) < 0);                                    // a .d71 is for the 1571
        CHECK(s.init(100000) < 0);
        CHECK(s.sector(41, 0) == 0);
    }

    // ---- the .g64 file ----
    {
        std::vector<uint8_t> out(GCR_MAXSIZE + 8192);
        uint32_t n = g.build_g64(out.data(), (uint32_t)out.size(), false);
        CHECK(n > 0);
        const Bytes keep(out.begin(), out.begin() + n);
        CHECK(!memcmp(out.data(), "GCR-1541", 8));
        CHECK_EQ(out[8], 0);
        CHECK_EQ(out[9], 84);
        CHECK_EQ(out[10] | (out[11] << 8), 0x1EF8);
        auto le = [&](uint32_t o) { return (uint32_t)out[o] | ((uint32_t)out[o + 1] << 8) | ((uint32_t)out[o + 2] << 16) | ((uint32_t)out[o + 3] << 24); };
        CHECK_EQ(le(12), 12 + 84 * 8);                                // track 1 follows the tables
        CHECK_EQ(le(12 + 4), 0u);                                     // no half track
        CHECK_EQ(le(12 + 4 * 70), 0u);                                // track 36: blank, not saved
        CHECK_EQ(le(12 + 4 * 84), 3u);                                // the speed zone of track 1
        CHECK_EQ(le(12 + 4 * 84 + 4 * 68), 0u);                       // ... of track 35
        CHECK_EQ(out[le(12)] | (out[le(12) + 1] << 8), 0x1E0C);
        FakeFs fs;
        fs.put("/a.g64", std::vector<uint8_t>(out.begin(), out.begin() + n));
        CoreFile *f = fs.open("/a.g64", true);
        std::vector<uint8_t> m(GCR_MAXSIZE);
        GcrImage l(m.data(), 0x400000);
        char why[120];
        CHECK(l.load(f, why, sizeof(why)));
        for (int t = 0; t < 35; t++) {
            CHECK(l.tracks[2 * t].in_image_file);
            CHECK_EQ(l.tracks[2 * t].track_length, g.tracks[2 * t].track_length);
            CHECK_EQ(l.tracks[2 * t].speed_zone, g.tracks[2 * t].speed_zone);
            CHECK(!memcmp(l.tracks[2 * t].track_address, g.tracks[2 * t].track_address, (size_t)g.tracks[2 * t].track_length));
        }
        CHECK(l.tracks[70].track_address != 0 && !l.tracks[70].in_image_file);   // blank track added behind the file
        BinImage lb;
        CHECK_EQ(l.to_bin(&lb), 0);
        CHECK(!memcmp(lb.bin_data, bin.bin_data, D64_35_NO_ERRORS));
        // a track written back lands in its place in the file, length field included
        l.tracks[8].track_address[10] ^= 0xFF;
        CHECK(l.write_track(8, f, false));
        const Bytes &file = fs.files["/a.g64"];
        uint32_t off = le(12 + 4 * 8);
        CHECK_EQ(file[off + 2 + 10], l.tracks[8].track_address[10]);
        CHECK_EQ(file[off] | (file[off + 1] << 8), l.tracks[8].track_length);
        CHECK(fs.syncs > 0);
        CHECK(!l.write_track(70, f, false));                          // a track that is not in the file has no place in it
        CHECK(!l.write_track(1, f, false));                           // no such track
        fs.close(f);
        // aligned: every saved track starts with the sync of sector 0
        std::vector<uint8_t> al(GCR_MAXSIZE + 8192);
        uint32_t an = g.build_g64(al.data(), (uint32_t)al.size(), true);
        CHECK(an == n);
        auto lea = [&](uint32_t o) { return (uint32_t)al[o] | ((uint32_t)al[o + 1] << 8) | ((uint32_t)al[o + 2] << 16) | ((uint32_t)al[o + 3] << 24); };
        for (int t = 0; t < 35; t++) {
            uint32_t o = lea(12 + 4 * 2 * t);
            CHECK(al[o + 2] == 0xFF && al[o + 6] == 0xFF && al[o + 7] == 0x52);
        }
        fs.put("/al.g64", std::vector<uint8_t>(al.begin(), al.begin() + an));
        CoreFile *f2 = fs.open("/al.g64", false);
        GcrImage l2(m.data(), 0x400000);
        CHECK(l2.load(f2, why, sizeof(why)));
        BinImage lb2;
        CHECK_EQ(l2.to_bin(&lb2), 0);
        CHECK(!memcmp(lb2.bin_data, bin.bin_data, D64_35_NO_ERRORS));  // a rotated track decodes to the same disk
        fs.close(f2);
        // cap too small
        CHECK_EQ(g.build_g64(out.data(), 1000, false), 0u);
        CHECK_EQ(g.build_g64(out.data(), 12 + 84 * 8 + 5000, false), 0u);

        // files that are no .g64
        auto bad = [&](const char *name, Bytes d, const char *want) {
            fs.put(name, d);
            CoreFile *bf = fs.open(name, false);
            GcrImage bi(m.data(), 0);
            bool ok = bi.load(bf, why, sizeof(why));
            fs.close(bf);
            if (ok || !strstr(why, want)) printf("  (%s: ok=%d why=\"%s\")\n", name, ok, why);
            CHECK(!ok);
            CHECK(strstr(why, want) != 0);
        };
        Bytes good = keep;
        Bytes sig = good; sig[0] = 'X';
        bad("/sig.g64", sig, "signature");
        Bytes ds = good; memcpy(ds.data(), "GCR-1571", 8);
        bad("/ds.g64", ds, "double sided");
        Bytes cut(good.begin(), good.begin() + 12 + 84 * 8 + 1000);
        bad("/cut.g64", cut, "cut off");
        Bytes ptr = good; ptr[12 + 3 * 4] = 0xFF; ptr[12 + 3 * 4 + 1] = 0xFF; ptr[12 + 3 * 4 + 2] = 0xFF;
        bad("/ptr.g64", ptr, "outside");
        bad("/tiny.g64", Bytes(10, 0), "too short");
        bad("/huge.g64", Bytes(GCR_MAXSIZE + 1, 0), "track memory takes");
    }

    // ---- blank images ----
    {
        std::vector<uint8_t> d(D64_BUF_SIZE);
        CHECK_EQ(d64_blank(d.data(), (uint32_t)d.size(), "NEW DISK,01"), (uint32_t)D64_35_NO_ERRORS);
        CHECK_EQ(d64_blank(d.data(), 1000, "X"), 0u);
        uint8_t *bam = d.data() + 357 * 256;
        CHECK_EQ(bam[0], 18);
        CHECK_EQ(bam[1], 1);
        CHECK_EQ(bam[2], 0x41);
        int free_blocks = 0;
        for (int t = 1; t <= 35; t++) {
            if (t != 18) free_blocks += bam[4 * t];
        }
        CHECK_EQ(free_blocks, 664);                                   // 683 minus the directory track's 19
        CHECK_EQ(bam[4 * 18], 17);
        CHECK(bam[4 * 18 + 1] == 0xFC && bam[4 * 18 + 2] == 0xFF && bam[4 * 18 + 3] == 0x07);
        CHECK_EQ(bam[4 * 1], 21);
        CHECK(bam[4 * 1 + 1] == 0xFF && bam[4 * 1 + 2] == 0xFF && bam[4 * 1 + 3] == 0x1F);
        CHECK_EQ(bam[4 * 31], 17);
        CHECK(!memcmp(bam + 144, "NEW DISK", 8));
        CHECK_EQ(bam[144 + 8], 0xA0);
        CHECK_EQ(bam[144 + 16], 0xA0);
        CHECK(bam[162] == '0' && bam[163] == '1');                    // the ID behind the comma
        CHECK(bam[165] == '2' && bam[166] == 'A');
        uint8_t *dir = d.data() + (357 + 1) * 256;
        CHECK(dir[0] == 0 && dir[1] == 0xFF);
        // the same disk, case folded
        CHECK_EQ(d64_blank(d.data(), (uint32_t)d.size(), "mixed"), (uint32_t)D64_35_NO_ERRORS);
        CHECK(!memcmp(d.data() + 357 * 256 + 144, "MIXED", 5));

        std::vector<uint8_t> e(820000);
        CHECK_EQ(d81_blank(e.data(), (uint32_t)e.size(), "BIG,3D"), 819200u);
        CHECK_EQ(d81_blank(e.data(), 800000, "x"), 0u);
        uint8_t *h = e.data() + 399360;                               // track 40 sector 0
        CHECK(h[0] == 40 && h[1] == 3 && h[2] == 0x44);
        CHECK(!memcmp(h + 4, "BIG", 3));
        CHECK(h[22] == '3' && h[23] == 'D');
        CHECK(h[25] == '3' && h[26] == 'D');
        uint8_t *b1 = h + 256, *b2 = h + 512, *dr = h + 768;
        CHECK(b1[0] == 40 && b1[1] == 2 && b1[2] == 0x44 && b1[3] == 0xBB && b1[4] == '3' && b1[5] == 'D' && b1[6] == 0xC0);
        CHECK(b2[0] == 0 && b2[1] == 0xFF && b2[2] == 0x44 && b2[3] == 0xBB);
        CHECK(dr[0] == 0 && dr[1] == 0xFF);
        int fb = 0;
        for (int t = 1; t <= 80; t++) {
            uint8_t *en = (t <= 40 ? b1 : b2) + 0x10 + 6 * ((t - 1) % 40);
            if (t != 40) fb += en[0];
            if (t != 40) CHECK(en[1] == 0xFF && en[5] == 0xFF);
        }
        CHECK_EQ(fb, 3160);                                           // what a blank 1581 disk shows as blocks free
        uint8_t *t40 = b1 + 0x10 + 6 * 39;
        CHECK(t40[0] == 36 && t40[1] == 0xF0);
    }

    DONE("d1541_image");
}
