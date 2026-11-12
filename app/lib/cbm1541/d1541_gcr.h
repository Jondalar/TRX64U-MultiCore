// The 1541's disk in memory (T104.1 D15): GCR tracks the drive hardware reads, the binary .d64 image behind them, and the codec between
// the two. The geometry, the GCR code, the track layout (sync, header, gap, data, zone lengths, the track twist) and the decoder's walk
// over a track are the ones of Gideon's disk_image.cc, which the firmware has used on real drives for years; what changed is where the
// bytes come from: the 4-to-5 byte codec is software (a 16 entry table, no GCR_CODER page needed), and the files are CoreFile.
//
// Single sided and 1541 only: the drive gateware has no 1571 (T103, a316cffd), so no side 1 and no .d71.
// Pure: no board code. The track memory is a buffer the caller hands in together with the address the drive hardware reads it at.
#ifndef CBM1541_GCR_H
#define CBM1541_GCR_H

#include <stdint.h>
#include "fsif.h"

#define GCR_TRACKS            84          // half tracks, index 2 * (track - 1) is the whole track
#define GCR_USED_TRACKS       80          // the half tracks that get a blank track (40 whole tracks)
#define GCR_MAXTRACKLEN       0x1EF8
#define GCR_DUMMYTRACKLEN     0x1E0C      // the track every empty half track points at
#define GCR_MAXMETADATA       (12 + (2 * GCR_TRACKS * 10))
#define GCR_MAXSIZE           ((GCR_MAXTRACKLEN * GCR_USED_TRACKS) + GCR_MAXMETADATA)

#define D64_35_NO_ERRORS      174848
#define D64_35_WITH_ERRORS    175531
#define D64_40_NO_ERRORS      196608
#define D64_40_WITH_ERRORS    197376
#define D64_MAX_TRACKS        42
#define D64_MAX_SECTORS       (683 + 7 * 17)
#define D64_BUF_SIZE          (D64_MAX_SECTORS * 256 + D64_MAX_SECTORS)

// 4 bytes <-> 5 bytes of GCR, in the order of the track (high nibble first).
void gcr_encode4(const uint8_t *bin, uint8_t *gcr);
// Returns the number of 5 bit groups that are no GCR code (0: clean); the nibbles of those come out as 0.
int gcr_decode5(const uint8_t *gcr, uint8_t *bin);

// Zones of the 1541: whole track 1 .. 35 (36 .. 40 like the last zone). Track numbers here are ZERO based.
int d64_region(int track);
int d64_sectors(int track);
int d64_sectors_before(int track);
int d64_zone_speed(int track);        // the speed code (3 .. 0) the drive's bit time follows

struct GcrTrack {
    uint8_t *track_address;
    int track_length;
    int speed_zone;
    bool track_used;
    bool in_image_file;
    bool track_is_mfm;
};

class BinImage;

class GcrImage {
public:
    // `mem` is GCR_MAXSIZE bytes the drive can reach at `phys`; the image does not own it.
    GcrImage(uint8_t *mem, uint32_t phys);

    void invalidate();
    void blank();                                     // 40 unformatted whole tracks
    // Reads a .g64 whole. false and a sentence when it is none or a track does not fit.
    bool load(CoreFile *f, char *why, int whylen);
    // The .g64 file as it would be saved, into out (cap bytes); the length, 0 when it does not fit. align: start every track at sector 0.
    uint32_t build_g64(uint8_t *out, uint32_t cap, bool align);
    // The bytes of one track as they go to the file (aligned or not) and the 2 byte length field in front. false: no such track.
    bool track_file_bytes(int track, bool align, uint8_t *out, uint32_t cap, uint32_t *len);
    // Writes one track of a mounted .g64 back into its file at its place (length field included). false when it cannot.
    bool write_track(int track, CoreFile *f, bool align);
    // The disk of a .d64: all tracks of the binary image, track 18's ID at the places Gideon reads it from.
    void from_bin(BinImage *bin, int twist);
    // Decodes every track into the binary image; returns the sectors that did not decode clean.
    int to_bin(BinImage *bin);
    void track_got_written_with_gcr(int track);

    // The drive's view: where track i starts for the hardware.
    uint32_t phys_of(const uint8_t *p) const { return phys_ + (uint32_t)(p - mem_); }
    GcrTrack tracks[GCR_TRACKS];
    uint32_t used_bytes() const { return used_; }

    static int decode_track(uint8_t *gcr, int trackNumber, int trackLen, int maxSector, uint8_t *bin, uint8_t *status, int statlen);

private:
    uint8_t *mem_;
    uint32_t phys_;
    uint32_t used_;                                   // the part of mem_ the tracks lie in
    uint8_t id1_, id2_;
    uint8_t header_[8];
    uint8_t sector_buffer_[352];
    uint8_t track_buffer_[GCR_MAXTRACKLEN];

    uint8_t *convert_block(uint8_t *bin, uint8_t *gcr, int len);
    uint8_t *convert_track(uint8_t logical_track_1b, int region, uint8_t *bin, uint8_t *gcr, uint8_t *errors, int errors_size,
                           int geosgaps, int twist);
    int find_track_start(int track);
    void add_blank_tracks(uint8_t *gcr);
};

// A .d64 in memory: 35 or 40 (up to 42) tracks of 256 byte sectors, then the optional error bytes, one per sector.
class BinImage {
public:
    BinImage();
    ~BinImage();

    // Sets the layout from a file size; 0, or a negative number when the size is no .d64.
    int init(uint32_t size);
    int load(CoreFile *f);                            // the whole file; init() of its size
    // A blank formatted disk of `tracks` (35 or 40) tracks. name is "NAME" or "NAME,ID".
    void format(const char *name, int tracks);
    // The decoded disk has n tracks (35 .. 40): the data ends there and the error bytes move up behind it.
    void set_tracks(int n);
    uint32_t file_bytes() const;                      // what save() writes: data, and the error bytes when any is set
    // The file image into out (file_bytes() long).
    void build(uint8_t *out) const;
    // Decodes one half track of `gcr` into this image; returns 0, or the Gideon's negative codes (-10, -11, -12 decode failed). errorcount counts sectors with an error.
    int write_track(int phys_track, GcrImage *gcr, int &errorcount, int &secs);
    uint8_t *track_data(int t) { return t >= 0 && t < num_tracks ? track_start[t] : 0; }
    int track_sectors(int t) const { return t >= 0 && t < num_tracks ? sectors_[t] : 0; }
    uint32_t track_offset(int t) const { return (uint32_t)(track_start[t] - bin_data); }
    uint8_t *sector(int track_1b, int sector);

    int num_tracks;
    uint8_t *bin_data;
    uint8_t *errors;
    int error_size;

    friend class GcrImage;
private:
    uint8_t *track_start[D64_MAX_TRACKS];
    int sectors_[D64_MAX_TRACKS];
    int data_size_;
};

// Blank images, for `create`: the formatted file as bytes. d64: 35 tracks, 174,848 bytes. d81: 80 tracks, 819,200 bytes.
// Both fill the buffer (cap bytes); the length, 0 when it does not fit.
uint32_t d64_blank(uint8_t *out, uint32_t cap, const char *name);
uint32_t d81_blank(uint8_t *out, uint32_t cap, const char *name);

#endif
