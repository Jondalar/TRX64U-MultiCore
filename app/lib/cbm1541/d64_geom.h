// The 1541 disk's zone geometry (track numbers are ZERO based): header only, so the generic sector reader (d1541_fs.cc) and the library's GCR codec (d1541_gcr.cc) share it
// without the reader linking any code of the cbm1541 library.
#ifndef D64_GEOM_H
#define D64_GEOM_H

#define D64_MAX_TRACKS        42

static inline int d64_region(int track)
{
    static const int region_end[4] = { 17, 24, 30, 35 };
    for (int i = 0; i < 4; i++) {
        if (track < region_end[i]) {
            return i;
        }
    }
    return 3;
}

static inline int d64_region_sectors(int region)
{
    static const int sectors_per_track[4] = { 21, 19, 18, 17 };
    return sectors_per_track[region];
}

static inline int d64_sectors(int track) { return d64_region_sectors(d64_region(track)); }

static inline int d64_sectors_before(int track)
{
    int count = 0;
    for (int i = 0; i < track; i++) {
        count += d64_sectors(i);
    }
    return count;
}

#endif
