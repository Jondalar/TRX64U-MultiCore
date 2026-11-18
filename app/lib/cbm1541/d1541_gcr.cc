#include "d1541_gcr.h"
#include <string.h>
#include <stdio.h>

// ---- the code -------------------------------------------------------------------------------------------------------------------
// Single nibble GCR table (disk_image.cc:27)
static const uint8_t gcr_table[16] = { 0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
                                       0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15 };

static const int track_lengths[4]       = { 0x1E0C, 0x1BE6, 0x1A0A, 0x186A };
static const int sectors_per_track_[4]  = { 21, 19, 18, 17 };
static const int region_end[4]          = { 17, 24, 30, 35 };
static const int sector_gap_lengths[4]  = { 9, 19, 13, 10 };
static const int region_speed_codes[4]  = { 3, 2, 1, 0 };

void gcr_encode4(const uint8_t *bin, uint8_t *gcr)
{
    uint64_t acc = 0;
    for (int i = 0; i < 4; i++) {
        acc = (acc << 5) | gcr_table[bin[i] >> 4];
        acc = (acc << 5) | gcr_table[bin[i] & 15];
    }
    for (int i = 0; i < 5; i++) {
        gcr[i] = (uint8_t)(acc >> (8 * (4 - i)));
    }
}

int gcr_decode5(const uint8_t *gcr, uint8_t *bin)
{
    static int8_t inv[32];
    static bool ready;
    if (!ready) {
        for (int i = 0; i < 32; i++) {
            inv[i] = -1;
        }
        for (int n = 0; n < 16; n++) {
            inv[gcr_table[n]] = (int8_t)n;
        }
        ready = true;
    }
    uint64_t acc = 0;
    for (int i = 0; i < 5; i++) {
        acc = (acc << 8) | gcr[i];
    }
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        int hi = inv[(acc >> (35 - 10 * i)) & 31];
        int lo = inv[(acc >> (30 - 10 * i)) & 31];
        bad += (hi < 0) + (lo < 0);
        bin[i] = (uint8_t)(((hi < 0 ? 0 : hi) << 4) | (lo < 0 ? 0 : lo));
    }
    return bad;
}

// ---- geometry (track numbers are ZERO based) ----------------------------------------------------------------------------------
int d64_region(int track)
{
    for (int i = 0; i < 4; i++) {
        if (track < region_end[i]) {
            return i;
        }
    }
    return 3;
}

int d64_sectors(int track) { return sectors_per_track_[d64_region(track)]; }

int d64_sectors_before(int track)
{
    int count = 0;
    for (int i = 0; i < track; i++) {
        count += d64_sectors(i);
    }
    return count;
}

int d64_zone_speed(int track) { return region_speed_codes[d64_region(track)]; }

// ---- the GCR image ------------------------------------------------------------------------------------------------------------
GcrImage::GcrImage(uint8_t *mem, uint32_t phys) : mem_(mem), phys_(phys), used_(0), id1_(0), id2_(0)
{
    memset(header_, 0, sizeof(header_));
    memset(sector_buffer_, 0, sizeof(sector_buffer_));
    invalidate();
}

void GcrImage::invalidate()
{
    memset(tracks, 0, sizeof(tracks));
    used_ = 0;
}

void GcrImage::blank()
{
    invalidate();
    memset(mem_, 0, GCR_MAXSIZE);
    add_blank_tracks(mem_);
}

// 40 valid tracks, whole tracks only, as long as they fit (disk_image.cc:167)
void GcrImage::add_blank_tracks(uint8_t *gcr)
{
    uint8_t *blank_start = gcr;
    uint8_t *address_limit = mem_ + GCR_MAXSIZE;
    for (int i = 0; i < GCR_USED_TRACKS; i += 2) {
        int region = d64_region(i / 2);
        int length = track_lengths[region];
        if (!tracks[i].track_address) {
            if (gcr + length <= address_limit) {
                tracks[i].track_address = gcr;
                tracks[i].track_length = length;
                tracks[i].speed_zone = region_speed_codes[region];
                gcr += length;
            }
        }
    }
    memset(blank_start, 0, (size_t)(gcr - blank_start));
    if ((uint32_t)(gcr - mem_) > used_) {
        used_ = (uint32_t)(gcr - mem_);
    }
}

// aaaabbbb ccccdddd eeeeffff gggghhhh  ->  AAAAABBB BBCCCCCD DDDDEEEE EFFFFFGG GGGHHHHH
uint8_t *GcrImage::convert_block(uint8_t *bin, uint8_t *gcr, int len)
{
    for (int i = 0; i < len; i += 4) {
        gcr_encode4(bin + i, gcr);
        gcr += 5;
    }
    return gcr;
}

uint8_t *GcrImage::convert_track(uint8_t logical_track_1b, int region, uint8_t *bin, uint8_t *gcr, uint8_t *errors, int errors_size,
                                 int geosgaps, int twist)
{
    uint8_t *orig_dest = gcr;
    gcr = track_buffer_;
    uint8_t *end = gcr + track_lengths[region];

    for (uint8_t s = 0; s < sectors_per_track_[region]; s++) {
        sector_buffer_[0] = 7;
        sector_buffer_[258] = 0;
        sector_buffer_[259] = 0;
        header_[0] = 8;
        header_[3] = logical_track_1b;
        header_[4] = id2_;
        header_[5] = id1_;
        header_[6] = 0x0f;
        header_[7] = 0x0f;
        uint8_t errorcode = 0;
        if (errors != NULL && s < errors_size) {
            errorcode = errors[s];
        }
        header_[2] = s;
        header_[1] = header_[2] ^ header_[3] ^ header_[4] ^ header_[5];
        switch (errorcode) {
        case 2:  header_[0] = 0; break;
        case 4:  sector_buffer_[0] = 0; break;
        case 9:  header_[1] = (uint8_t)~header_[1]; break;
        case 11:
            header_[4] = (uint8_t)(id2_ ^ ~(s + 0x55));
            header_[5] = (uint8_t)(id1_ ^ ~(s + 0xAA));
            break;
        }
        for (int i = 0; i < 5; i++) {
            *(gcr++) = errorcode != 3 ? 0xFF : 0x00;
        }
        gcr = convert_block(header_, gcr, 8);
        for (int i = 0; i < 9; i++) {
            *(gcr++) = 0x55;
        }
        if (geosgaps & 1) {                                // disk_image.cc:299-302
            *(gcr - 1) = 0x67;
            *(gcr - 4) = 0x67;
        }
        for (int i = 0; i < 5; i++) {
            *(gcr++) = errorcode != 3 ? 0xFF : 0x00;
        }
        uint8_t *bp = &sector_buffer_[1];
        uint8_t chk = 0;
        for (int i = 0; i < 256; i++) {
            uint8_t b = *(bin++);
            chk ^= b;
            *(bp++) = b;
        }
        *bp = errorcode != 5 ? chk : (uint8_t)~chk;
        gcr = convert_block(sector_buffer_, gcr, 260);
        for (int i = 0; i < sector_gap_lengths[region]; i++) {
            *(gcr++) = 0x55;
        }
        if (geosgaps & 1) {                                // disk_image.cc:334-337
            *(gcr - 1) = 0x67;
            *(gcr - 4) = 0x67;
        }
    }
    while (gcr < end) {
        *(gcr++) = 0x55;
    }
    if (geosgaps & 1) {                                    // disk_image.cc:344-345
        *(gcr - 1) = 0x67;
    }
    // now copy with some offset, depending on the track
    int len = track_lengths[region];
    int tw = twist * (16 - region) / 16;
    int offset = (logical_track_1b * tw) % len;
    memcpy(orig_dest + offset, track_buffer_, (size_t)(len - offset));
    memcpy(orig_dest, track_buffer_ + len - offset, (size_t)offset);
    return orig_dest + len;
}

void GcrImage::from_bin(BinImage *bin, int twist, int geos)
{
    id1_ = bin->bin_data[91554];
    id2_ = bin->bin_data[91555];
    uint8_t *gcr = mem_;
    invalidate();
    memset(mem_, 0, GCR_MAXSIZE);
    for (int lt = 0; lt < bin->num_tracks; lt++) {
        int pt = 2 * lt;
        int sec = d64_sectors_before(lt);
        int region = d64_region(lt);
        uint8_t *error_bytes = NULL;
        int error_size = 0;
        if (bin->errors) {
            error_bytes = bin->errors + sec;
            error_size = bin->error_size - sec;
        }
        tracks[pt].track_address = gcr;
        uint8_t *newgcr = convert_track((uint8_t)(lt + 1), region, bin->track_start[lt], gcr, error_bytes, error_size, geos & 1, twist);
        tracks[pt].track_length = (int)(newgcr - gcr);
        tracks[pt].speed_zone = region_speed_codes[region];
        tracks[pt].track_used = true;
        gcr = newgcr;
    }
    if ((geos & 2) && bin->num_tracks == 35 && gcr + 7692 <= mem_ + GCR_MAXSIZE) {     // "Track 36": disk_image.cc:640-668, the copy protection track
        int pt = 70;
        tracks[pt].track_length = 7692;
        tracks[pt].track_address = gcr;
        tracks[pt].speed_zone = 3;
        tracks[pt].track_used = true;
        memset(gcr, 0x55, 7692);
        uint8_t data[12] = { 0x2f, 0x53, 0x77, 0x7d, 0x67, 0x45, 0xb5, 0xdd, 0x77, 0x62, 0x73, 0x77 };
        uint8_t *w = gcr;
        for (int s = 0; s < 0x18; s++) {
            for (int l = 0; l < 4; l++) {
                for (int i = 0; i < 0x15; i++) {
                    *(w++) = data[3 * l + 0];
                    *(w++) = data[3 * l + 1];
                    *(w++) = data[3 * l + 2];
                }
            }
            for (int l = 0; l < 4; l++) {
                uint8_t ovl0 = data[3 * l] > 127 ? 1 : 0;
                uint8_t ovl1 = data[3 * l + 1] > 127 ? 1 : 0;
                uint8_t ovl2 = data[3 * l + 2] > 127 ? 1 : 0;
                data[3 * l + 2] = (uint8_t)((data[3 * l + 2] << 1) + ovl0);
                data[3 * l + 1] = (uint8_t)((data[3 * l + 1] << 1) + ovl2);
                data[3 * l + 0] = (uint8_t)((data[3 * l + 0] << 1) + ovl1);
            }
        }
        gcr += 7692;
    }
    used_ = (uint32_t)(gcr - mem_);
    add_blank_tracks(gcr);
}

// ---- decoding ---------------------------------------------------------------------------------------------------------------
static uint8_t *find_sync(uint8_t *gcr_data, uint8_t *begin, uint8_t *end)
{
    bool wrap = false;
    int sync_count = 0;
    do {
        if (gcr_data >= end) {
            gcr_data = begin;
            if (wrap) {
                return NULL;
            }
            wrap = true;
        }
        if (*gcr_data == 0xFF) {
            sync_count++;
        } else {
            if (sync_count >= 2) {
                return gcr_data;                          // byte after sync
            }
            sync_count = 0;
        }
        gcr_data++;
    } while (1);
}

// `count` bytes from *current, wrapped round the end of the track and shifted by `shift` bits (a sync that does not end on a byte)
static uint8_t *wrap_bytes(uint8_t **current, uint8_t *begin, uint8_t *end, int count, uint8_t *buffer, uint8_t shift)
{
    uint8_t *gcr = *current;
    if (shift == 0) {
        if (gcr > (end - count)) {
            uint8_t *d = buffer;
            uint8_t *s = gcr;
            *current = (gcr + count) - (end - begin);
            while (count--) {
                if (s == end) {
                    s = begin;
                }
                *(d++) = *(s++);
            }
            return buffer;
        }
        *current = gcr + count;
        return gcr;
    }
    uint8_t *d = buffer;
    uint8_t *s = gcr;
    uint8_t curByte = (uint8_t)(*(s++) << shift);
    if (gcr > (end - count)) {
        *current = (gcr + count) - (end - begin);
    } else {
        *current = gcr + count;
    }
    while (count--) {
        if (s == end) {
            s = begin;
        }
        curByte |= *s >> (8 - shift);
        *(d++) = curByte;
        curByte = (uint8_t)(*s << shift);
        s++;
    }
    return buffer;
}

static void conv5(uint8_t **gcr, uint8_t *bin)
{
    gcr_decode5(*gcr, bin);
    *gcr += 5;
}

int GcrImage::decode_track(uint8_t *gcr, int trackNumber, int trackLen, int maxSector, uint8_t *bin, uint8_t *status, int statlen)
{
    uint8_t header[8];
    memset(header, 0, sizeof(header));
    uint8_t *begin = gcr;
    uint8_t *end = begin + trackLen;
    uint8_t *current = gcr;
    uint8_t *dest = bin;
    uint8_t *st = status;
    bool expect_data = false;
    bool wrapped = false;
    int secs = 0;
    uint8_t sector_buffer[352];

    while (secs < maxSector) {
        uint8_t *new_gcr = find_sync(current, begin, end);
        if (!new_gcr) {
            break;                                        // no sync found
        }
        if (new_gcr < current) {
            if (wrapped) {
                break;
            }
            wrapped = true;
        }
        uint8_t *pntr = current = new_gcr;
        uint8_t shift = 0;
        uint8_t firstByte = *new_gcr;
        while (firstByte & 0x80) {
            shift++;
            firstByte <<= 1;
            if (shift == 8) {
                break;
            }
        }
        uint8_t *gcr_data = wrap_bytes(&pntr, begin, end, 5, sector_buffer, shift);
        conv5(&gcr_data, &header[0]);
        if (header[0] == 8) {
            gcr_data = wrap_bytes(&pntr, begin, end, 5, sector_buffer, shift);
            conv5(&gcr_data, &header[4]);
            int t = header[3];
            int s = header[2];
            dest = bin + (256 * s);
            if (expect_data) {                            // a header, but a data block was expected
                if (st && statlen > 0) {
                    *(st++) = 0xE4;
                    statlen--;
                }
            }
            if (st && statlen > 0) {                      // new sector, store its number
                *(st++) = (uint8_t)s;
                statlen--;
            }
            expect_data = true;
            if (t != trackNumber) {
                if (st && statlen > 0) {
                    *(st++) = 0xE1;
                    statlen--;
                    expect_data = false;
                }
            } else if (s >= maxSector) {
                if (st && statlen > 0) {
                    *(st++) = 0xE2;
                    statlen--;
                    expect_data = false;
                }
            }
            if (expect_data) {
                current = pntr;
            }
            continue;
        }
        if (header[0] == 7 && expect_data) {
            expect_data = false;
            secs++;
            uint8_t *binarySector = dest;
            memcpy(dest, &header[1], 3);
            dest += 3;
            gcr_data = wrap_bytes(&pntr, begin, end, 320, sector_buffer, shift);
            for (int i = 0; i < 63; i++) {
                conv5(&gcr_data, dest);
                dest += 4;
            }
            conv5(&gcr_data, &header[4]);
            *(dest++) = header[4];
            if (st && statlen > 0) {
                uint8_t chk = 0;
                for (int i = 0; i < 256; i++) {
                    chk ^= *(binarySector++);
                }
                if (chk != header[5]) {
                    *(st++) = 0xE3;
                } else {
                    *(st++) = 0x00;
                    current = pntr;
                }
                statlen--;
            }
        }
    }
    return secs;
}

int GcrImage::to_bin(BinImage *bin)
{
    int errors = 0;
    bin->init(D64_40_WITH_ERRORS);
    memset(bin->bin_data, 0, D64_BUF_SIZE);
    int valid_tracks = 0;
    for (int pt = 0; pt < GCR_TRACKS; pt += 2) {          // whole tracks: a half track would overwrite the track it sits next to
        if (!tracks[pt].track_address) {
            continue;
        }
        int secs = 0;
        bin->write_track(pt, this, errors, secs);
        if (secs > 0) {
            valid_tracks++;
        }
    }
    bin->set_tracks(valid_tracks);
    return errors;
}

int GcrImage::find_track_start(int track)
{
    uint8_t *begin = tracks[track].track_address;
    uint8_t *end = begin + tracks[track].track_length;
    uint8_t *gcr = begin;
    for (int j = 0; j < 42; j++) {                        // at most twice for each sector (two syncs per sector)
        gcr = find_sync(gcr, begin, end);
        if (!gcr) {
            return 0;
        }
        uint8_t *g = wrap_bytes(&gcr, begin, end, 5, sector_buffer_, 0);
        conv5(&g, &header_[0]);
        if (header_[0] == 8 && header_[2] == 0) {          // sector 0 found
            int offset = (int)(gcr - begin) - 10;
            if (offset < 0) {
                offset += tracks[track].track_length;
            }
            return offset;
        }
    }
    return 0;
}

// ---- the .g64 file ----------------------------------------------------------------------------------------------------------------
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put_le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

bool GcrImage::load(CoreFile *f, char *why, int whylen)
{
    invalidate();
    uint64_t n64 = f->size();
    if (n64 > GCR_MAXSIZE) {
        snprintf(why, whylen, "the image is %u bytes, the drive's track memory takes %u", (unsigned)fs_size32(n64), (unsigned)GCR_MAXSIZE);
        return false;
    }
    uint32_t n = (uint32_t)n64;
    if (n < 12 + 8 || !f->read(0, mem_, n)) {
        snprintf(why, whylen, n < 20 ? "not a .g64 (too short)" : "cannot read the image (the medium is gone?)");
        return false;
    }
    // A .g71 (GCR-1571) is the same table layout with 168 entries: side 0 is the first 84, which is all a 1541 head can reach. The tables lie behind
    // the count the header reports (disk_image.cc:698-728 reads the speed zones at 3 + reported_tracks), so the position is the file's, the loop is ours.
    if (strncmp((const char *)mem_, "GCR-1541", 8) != 0 && strncmp((const char *)mem_, "GCR-1571", 8) != 0) {
        snprintf(why, whylen, "not a .g64 (no GCR-1541 signature)");
        return false;
    }
    uint32_t count = mem_[9];
    uint32_t reported = count > GCR_TRACKS ? GCR_TRACKS : count;
    if (12 + count * 8 > n) {
        snprintf(why, whylen, "not a .g64 (the track tables are cut off)");
        return false;
    }
    for (uint32_t i = 0; i < reported; i++) {
        uint32_t offset = le32(mem_ + 12 + 4 * i);
        if (!offset) {
            continue;
        }
        if (offset < 12 + count * 8 || offset + 2 > n) {
            snprintf(why, whylen, "track %u points outside the file", (unsigned)i);
            return false;
        }
        uint8_t *tr = mem_ + offset;
        uint16_t w = (uint16_t)(tr[0] | (tr[1] << 8));
        uint32_t len = w & 0x3FFF;
        if (offset + 2 + len > n) {
            snprintf(why, whylen, "track %u is cut off by the end of the file", (unsigned)i);
            return false;
        }
        tracks[i].track_address = tr + 2;
        tracks[i].track_length = (int)len;
        tracks[i].track_used = true;
        tracks[i].in_image_file = true;
        tracks[i].track_is_mfm = (w & 0x8000) != 0;
        tracks[i].speed_zone = (int)le32(mem_ + 12 + 4 * count + 4 * i);
    }
    used_ = n;
    add_blank_tracks(mem_ + n);
    return true;
}

bool GcrImage::track_file_bytes(int track, bool align, uint8_t *out, uint32_t cap, uint32_t *len)
{
    if (track < 0 || track >= GCR_TRACKS || !tracks[track].track_address || !tracks[track].track_used) {
        return false;
    }
    uint32_t l = (uint32_t)tracks[track].track_length;
    if (cap < l + 2) {
        return false;
    }
    out[0] = (uint8_t)l;
    out[1] = (uint8_t)(l >> 8);
    if (tracks[track].track_is_mfm) {
        out[1] |= 0x80;
    }
    int start = align ? find_track_start(track) : 0;
    const uint8_t *src = tracks[track].track_address;
    if (start > 0 && (uint32_t)start < l) {
        memcpy(out + 2, src + start, l - (uint32_t)start);
        memcpy(out + 2 + l - (uint32_t)start, src, (size_t)start);
    } else {
        memcpy(out + 2, src, l);
    }
    *len = l + 2;
    return true;
}

uint32_t GcrImage::build_g64(uint8_t *out, uint32_t cap, bool align)
{
    uint32_t hdr = 12 + GCR_TRACKS * 8;
    if (cap < hdr) {
        return 0;
    }
    memset(out, 0, hdr);
    memcpy(out, "GCR-1541", 8);
    out[8] = 0;
    out[9] = GCR_TRACKS;
    out[10] = (uint8_t)GCR_MAXTRACKLEN;
    out[11] = (uint8_t)(GCR_MAXTRACKLEN >> 8);
    uint32_t pos = hdr;
    for (int i = 0; i < GCR_TRACKS; i++) {
        uint32_t l = 0;
        if (!track_file_bytes(i, align, out + pos, cap - pos, &l)) {
            if (tracks[i].track_address && tracks[i].track_used) {
                return 0;                                  // it exists and does not fit
            }
            continue;
        }
        put_le32(out + 12 + 4 * i, pos);
        put_le32(out + 12 + 4 * GCR_TRACKS + 4 * i, (uint32_t)tracks[i].speed_zone);
        pos += l;
    }
    return pos;
}

bool GcrImage::write_track(int track, CoreFile *f, bool align)
{
    if (track < 0 || track >= GCR_TRACKS || !tracks[track].track_address || !tracks[track].in_image_file || !f) {
        return false;
    }
    uint32_t offset = (uint32_t)(tracks[track].track_address - mem_);
    if (offset < 2) {
        return false;
    }
    static uint8_t buf[0x4002];
    uint32_t len = 0;
    if (!track_file_bytes(track, align, buf, sizeof(buf), &len)) {
        return false;
    }
    return f->write(offset - 2, buf, len) && f->sync();
}

void GcrImage::track_got_written_with_gcr(int track)
{
    if (track < 0 || track >= GCR_TRACKS || !tracks[track].track_address) {
        return;
    }
    // The hardware does not record at what speed a track got written: the standard zone is recorded, which also says "this is GCR now".
    tracks[track].speed_zone = region_speed_codes[d64_region(track / 2)];
    tracks[track].track_used = true;
    tracks[track].track_is_mfm = false;
}

// ---- the binary image ---------------------------------------------------------------------------------------------------------
BinImage::BinImage() : num_tracks(0), errors(0), error_size(0), data_size_(0)
{
    bin_data = new uint8_t[D64_BUF_SIZE];
    memset(bin_data, 0, D64_BUF_SIZE);
    memset(track_start, 0, sizeof(track_start));
    memset(sectors_, 0, sizeof(sectors_));
}

BinImage::~BinImage()
{
    delete[] bin_data;
}

uint8_t *BinImage::sector(int track_1b, int s)
{
    int t = track_1b - 1;
    if (t < 0 || t >= num_tracks || s < 0 || s >= sectors_[t]) {
        return 0;
    }
    return track_start[t] + (s << 8);
}

int BinImage::init(uint32_t size)
{
    num_tracks = 0;
    errors = 0;
    error_size = 0;
    if (size < D64_35_NO_ERRORS) {
        return -3;
    }
    if (size >= 2 * D64_35_NO_ERRORS) {
        return -4;                                          // a .d71
    }
    size -= D64_35_NO_ERRORS;
    num_tracks = 35;
    data_size_ = D64_35_NO_ERRORS;
    errors = &bin_data[D64_35_NO_ERRORS];
    while (size >= 17 * 256 && num_tracks < D64_MAX_TRACKS) {
        num_tracks++;
        size -= 17 * 256;
        errors += 17 * 256;
        data_size_ += 17 * 256;
    }
    error_size = (int)size;
    if (size == 0) {
        errors = 0;
    }
    uint8_t *track = bin_data;
    for (int i = 0; i < D64_MAX_TRACKS; i++) {
        if (i < num_tracks) {
            sectors_[i] = d64_sectors(i);
            track_start[i] = track;
            track += 256 * sectors_[i];
        } else {
            sectors_[i] = 0;
            track_start[i] = 0;
        }
    }
    return 0;
}

int BinImage::load(CoreFile *f)
{
    uint64_t n = f->size();
    if (n < D64_35_NO_ERRORS) {
        return -3;
    }
    if (n >= 2 * D64_35_NO_ERRORS) {
        return -4;                                          // a .d71
    }
    if (n > D64_BUF_SIZE) {
        return -5;
    }
    memset(bin_data, 0, D64_BUF_SIZE);
    if (!f->read(0, bin_data, (uint32_t)n)) {
        return -2;
    }
    return init((uint32_t)n);
}

uint32_t BinImage::file_bytes() const
{
    uint32_t n = (uint32_t)data_size_;
    int secs = d64_sectors_before(num_tracks);
    if (errors && error_size >= secs) {
        uint8_t orred = 0;
        for (int i = 0; i < secs; i++) {
            orred |= errors[i];
        }
        if (orred) {
            n += (uint32_t)secs;
        }
    }
    return n;
}

void BinImage::build(uint8_t *out) const
{
    uint32_t n = file_bytes();
    memcpy(out, bin_data, (size_t)data_size_);
    if (n > (uint32_t)data_size_) {
        memcpy(out + data_size_, errors, n - (uint32_t)data_size_);
    }
}

int BinImage::write_track(int phys_track, GcrImage *gcr_image, int &errorcount, int &secs)
{
    int logical_track = phys_track / 2;
    secs = 0;
    if (logical_track >= D64_MAX_TRACKS || !track_start[logical_track]) {
        return -10;                                         // the track does not exist in the binary image
    }
    uint8_t *bin = track_start[logical_track];
    GcrTrack *gt = &gcr_image->tracks[phys_track];
    if (!gt->track_address) {
        return -11;                                         // ... or in the GCR image
    }
    uint8_t status[64];
    memset(status, 0, sizeof(status));
    int expected = sectors_[logical_track];
    secs = GcrImage::decode_track(gt->track_address, logical_track + 1, gt->track_length, expected, bin, status, 64);
    int error_offset = errors ? d64_sectors_before(logical_track) : 0;
    for (int i = 0; i < 2 * secs; i += 2) {
        if (errors && error_offset + status[i] < error_size) {
            errors[error_offset + status[i]] = status[i + 1];
        }
        if (status[i + 1]) {
            errorcount++;
        }
    }
    if (secs != expected) {
        return -12;                                         // decode failed
    }
    return 0;
}

// ---- BAM and directory: blank disks -------------------------------------------------------------------------------------------
// "NAME" or "NAME,ID": 16 characters of upper case name padded with $A0, the ID behind the comma at +18/+19, the DOS type at +21/+22.
static void set_volume_name(const char *name, uint8_t *dst, const char *dos)
{
    for (int t = 0; t < 27; t++) {
        dst[t] = 0xA0;
    }
    dst[21] = (uint8_t)dos[0];
    dst[22] = (uint8_t)dos[1];
    int b = 0;
    for (int t = 0; t < 27; t++) {
        char c = name[b++];
        if (!c) {
            break;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if (c == ',') {
            t = 17;
            continue;
        }
        dst[t] = (uint8_t)c;
    }
}

static void bam_free(uint8_t *entry, int sectors, bool all_free)
{
    // entry[0] free count, entry[1..] bitmap, sector n at bit n of byte n / 8
    memset(entry + 1, 0, 3);
    for (int s = 0; s < sectors; s++) {
        entry[1 + s / 8] |= (uint8_t)(1 << (s & 7));
    }
    entry[0] = all_free ? (uint8_t)sectors : 0;
}

static void d64_allocate(uint8_t *bam, int track_1b, int sector)
{
    uint8_t *e = bam + 4 * track_1b;
    if (e[1 + sector / 8] & (1 << (sector & 7))) {
        e[1 + sector / 8] &= (uint8_t)~(1 << (sector & 7));
        e[0]--;
    }
}

void BinImage::set_tracks(int n)
{
    if (n < 35) {
        n = 35;
    }
    if (n > 40) {
        n = 40;
    }
    int secs = d64_sectors_before(n);
    if (errors && error_size >= secs) {
        memmove(bin_data + secs * 256, errors, (size_t)secs);
        errors = bin_data + secs * 256;
        error_size = secs;
    } else {
        errors = 0;
        error_size = 0;
    }
    num_tracks = n;
    data_size_ = secs * 256;
}

void BinImage::format(const char *name, int tracks)
{
    if (tracks != 40) {
        tracks = 35;
    }
    memset(bin_data, 0, D64_BUF_SIZE);
    init((uint32_t)(tracks == 35 ? D64_35_NO_ERRORS : D64_40_NO_ERRORS));
    uint8_t *root = sector(18, 0);
    root[0] = 18;
    root[1] = 1;
    root[2] = 0x41;
    root[3] = 0x20;
    for (int t = 1; t <= 35; t++) {
        bam_free(root + 4 * t, d64_sectors(t - 1), true);
    }
    set_volume_name(name, root + 144, "2A");
    d64_allocate(root, 18, 0);
    d64_allocate(root, 18, 1);
    uint8_t *dir = sector(18, 1);
    dir[0] = 0;
    dir[1] = 0xFF;
}

uint32_t d64_blank(uint8_t *out, uint32_t cap, const char *name)
{
    if (cap < D64_35_NO_ERRORS) {
        return 0;
    }
    BinImage *b = new BinImage();
    b->format(name, 35);
    b->build(out);
    uint32_t n = b->file_bytes();
    delete b;
    return n;
}

// The 1581: 80 tracks of 40 sectors of 256 bytes. Header 40/0, BAM 40/1 (tracks 1 .. 40) and 40/2 (41 .. 80), first directory sector 40/3.
uint32_t d81_blank(uint8_t *out, uint32_t cap, const char *name)
{
    const uint32_t total = 3200u * 256;
    if (cap < total) {
        return 0;
    }
    memset(out, 0, total);
    uint8_t *hdr = out + (39 * 40 + 0) * 256;
    uint8_t *bam1 = out + (39 * 40 + 1) * 256;
    uint8_t *bam2 = out + (39 * 40 + 2) * 256;
    uint8_t *dir = out + (39 * 40 + 3) * 256;
    hdr[0] = 40;
    hdr[1] = 3;
    hdr[2] = 0x44;
    set_volume_name(name, hdr + 4, "3D");
    bam1[0] = 40; bam1[1] = 2; bam1[2] = 0x44; bam1[3] = 0xBB; bam1[4] = hdr[22]; bam1[5] = hdr[23]; bam1[6] = 0xC0;
    bam2[0] = 0;  bam2[1] = 0xFF; bam2[2] = 0x44; bam2[3] = 0xBB; bam2[4] = hdr[22]; bam2[5] = hdr[23]; bam2[6] = 0xC0;
    for (int t = 0; t < 80; t++) {
        uint8_t *e = (t < 40 ? bam1 : bam2) + 0x10 + 6 * (t % 40);
        e[0] = 40;
        memset(e + 1, 0xFF, 5);
    }
    uint8_t *t40 = bam1 + 0x10 + 6 * 39;                      // header, both BAM sectors and the first directory sector are in use
    t40[0] = 36;
    t40[1] = 0xF0;
    dir[0] = 0;
    dir[1] = 0xFF;
    return total;
}
