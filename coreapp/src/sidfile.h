// The header of a .sid / .mus / .str file and what the SID player needs from it (T228 R21, item 7). Pure: bytes in, numbers and text out, so tests/coreapp/
// can run it on a byte vector. PROVENANCE: TRX64-Ultimate software/ultimate-app/software/filetypes/filetype_sid.cc (Gideon Zweijtzer, GPL-3.0):
// FileTypeSID::readHeader, processHeader, createMusHeader, showInfo, prepare (the part that reads the header), ConfigSIDs. The order of the
// steps, the byte offsets and the quirks (the 2 bytes of the load address that are subtracted from the length when the header names the address
// itself) are FW's, taken over as they are.
#ifndef COREAPP_SIDFILE_H
#define COREAPP_SIDFILE_H

#include <stdint.h>

#define SID_HDR_BYTES 0x80
#define SID_HDR_READ  0x7E            // the bytes FW reads from a .sid
#define SID_SSL_BYTES 512             // the song-lengths table the player takes (cart image +0x3000)

struct SidTune {
    uint8_t hdr[SID_HDR_BYTES];       // FW's sid_header: the file's header (big endian words) until sid_plan swaps it to the player's little endian form
    int version;                      // header_version
    uint32_t data_offset;
    int songs;                        // number_of_songs
    bool mus;                         // mus_file: a .mus / .str file, or a .sid whose flag bit 0 says "MUS data"
    bool sid;                         // sid_file: the file itself is a .sid
    bool valid;                       // the header is usable (header_valid)
    uint16_t flags;                   // 0x76/0x77
    uint16_t song, default_song;
    uint16_t start, end, header_location;
    int skip;                         // bytesToSkip: the bytes at data_offset that are not music data
    uint8_t extra_lo;                 // unused, keeps the struct a multiple of 4 on every compiler
};

// One SID the tune asks for (t_sid_definition): base is the high byte pair of the address as the FPGA decodes it (0x40 = $D400), type 1 6581, 2 8580,
// 3 either / not said.
struct SidReq {
    uint8_t base;
    uint8_t type;
};

// FileTypeSID::fetch_context_items: the header of the file, or a made-up one for a .mus / .str. `name` is the file's name (the title of a .mus is
// its name without the extension, underscores as blanks), `ext_mus` says the extension is MUS or STR. `n` bytes of the file's start are in `head`.
// False when the file is no tune (FW offers no action then).
bool sid_parse(const uint8_t *head, uint32_t n, const char *name, bool ext_mus, SidTune *t);

// FileTypeSID::showInfo: the text of "Show Info" (title, author, released, version, number of songs, default song), PETSCII look-alikes mapped
// as FW does. Returns the bytes written (no terminator counted).
int sid_info_text(const SidTune &t, char *out, int outlen);

// The part of FileTypeSID::prepare after the file is open: start, end, where the header goes, the song, flags, the header's words little endian.
// `fsize` the file's size, `load_le` the little endian word at data_offset (read by the caller when the header's load address is 0), `song` 0 = main
// tune. 0 or a reason ("Illegal: Load rolls over $FFFF" ...) in *why.
int sid_plan(SidTune *t, uint32_t fsize, uint16_t load_le, int song, const char **why);

// FileTypeSID::ConfigSIDs: the SIDs the tune asks for (1 to 3).
int sid_requests(const SidTune &t, SidReq out[3]);

// configureMusEnv, the decision: `rd(user, a)` reads one byte of the C64's RAM where the music data was loaded, `load_end` is the offset after its last
// byte. True when the data holds a second (stereo) song behind the first; *first_end is where the second one starts (nextSongIndex). False: mono, the
// caller then tries the .str file of the same name at load_end (tryLoadStereoMus).
bool sid_mus_stereo(const SidTune &t, uint32_t load_end, uint8_t (*rd)(void *, uint32_t), void *user, uint32_t *first_end);

// configureMusEnv, the header part: a stereo tune gets the stereo player's init and play address and the second SID at $D500 (8580); a tune that ends
// at or above $C400 gets the two bytes at 0x78/0x79 FW sets.
void sid_mus_header(SidTune *t, bool stereo);

#endif
