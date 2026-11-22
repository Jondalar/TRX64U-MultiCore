// The export "sid" (major 1): the header of a .sid / .mus / .str file and what the SID player needs from it, the shared SID library of T239.3 D12 (today coreapp/src/sidfile.h
// and sidfile.cc: pure functions, bytes in, numbers and text out). A library of this repository puts the table into LibRecord::exports (base 3.2); the TRX64-Ultimate base
// library finds it with SvcLib::find("sid", 1, &size). Not a member of LibApi.
//
// The structures SidTune and SidReq and the SID_HDR_* sizes are defined here, once: sidfile.h includes this header for them (they had the same layout field for field).
// The functions take pointers where sidfile.h takes C++ references. No call takes a CoreCtx and none touches the machine.
//
// Rules: `size` first; members only appended inside a major. Task: any task (no shared state; a tune is the caller's). Memory: every pointer is the caller's; `why` of sid_plan
// points at a constant string.
#ifndef COREAPP_SID_API_H
#define COREAPP_SID_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SID_HDR_BYTES 0x80
#define SID_HDR_READ  0x7E            // the bytes read from a .sid
#define SID_SSL_BYTES 512             // the song-lengths table the player takes (cartridge image +0x3000)

struct SidTune {
    uint8_t hdr[SID_HDR_BYTES];       // the file's header (big endian words) until sid_plan swaps it to the player's little endian form
    int version;                      // header_version
    uint32_t data_offset;
    int songs;                        // number_of_songs
    bool mus;                         // a .mus / .str file, or a .sid whose flag bit 0 says "MUS data"
    bool sid;                         // the file itself is a .sid
    bool valid;                       // the header is usable
    uint16_t flags;
    uint16_t song, default_song;
    uint16_t start, end, header_location;
    int skip;                         // the bytes at data_offset that are not music data
    uint8_t extra_lo;                 // unused, keeps the struct a multiple of 4 on every compiler
};

// One SID the tune asks for: base is the high byte pair of the address as the FPGA decodes it (0x40 = $D400), type 1 6581, 2 8580, 3 either or not said.
struct SidReq {
    uint8_t base;
    uint8_t type;
};

typedef struct SidApi {
    uint32_t size;
    // The header of the file, or a made-up one for a .mus / .str. `name` is the file's name (the title of a .mus is its name without the extension, underscores as blanks),
    // `ext_mus` says the extension is MUS or STR; `n` bytes of the file's start are in `head` (at least SID_HDR_READ for a .sid). False: the file is no tune.
    bool (*parse)(const uint8_t *head, uint32_t n, const char *name, bool ext_mus, struct SidTune *t);
    // The text of "Show Info" (title, author, released, version, songs, default song). Result: the bytes written to out, no terminator counted.
    int (*info_text)(const struct SidTune *t, char *out, int outlen);
    // The part of the start after the file is open: start, end, where the header goes, the song, the flags, the header's words little endian. `fsize` the file's size,
    // `load_le` the little endian word at data_offset (read by the caller when the header's load address is 0), `song` 0 = the main tune. Result: 0, or non-zero with a
    // reason in *why ("Illegal: Load rolls over $FFFF"); `why` may be null.
    int (*plan)(struct SidTune *t, uint32_t fsize, uint16_t load_le, int song, const char **why);
    // The SIDs the tune asks for. Result: 1 to 3, written to out[0 .. result - 1].
    int (*requests)(const struct SidTune *t, struct SidReq out[3]);
    // Whether the data holds a second (stereo) song behind the first: `rd(user, a)` reads one byte of the C64's RAM where the music data was loaded, `load_end` is the offset
    // after its last byte. True: *first_end is where the second starts. False: mono; the caller then tries the .str file of the same name at load_end.
    bool (*mus_stereo)(const struct SidTune *t, uint32_t load_end, uint8_t (*rd)(void *, uint32_t), void *user, uint32_t *first_end);
    // A MUS tune's header: a stereo tune gets the stereo player's init and play address and the second SID at $D500 (8580); a tune that ends at or above $C400 gets the two
    // bytes at 0x78/0x79 set.
    void (*mus_header)(struct SidTune *t, bool stereo);
} SidApi;

#ifdef __cplusplus
}
#endif

#endif
