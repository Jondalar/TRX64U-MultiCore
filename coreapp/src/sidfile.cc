#include "sidfile.h"
#include <stdio.h>
#include <string.h>

// FW filetype_sid.cc: string_offsets, petscii[], ascii[]
static const int string_offsets[4] = { 0x16, 0x36, 0x56, 0x76 };
static const uint8_t petscii[59] = { 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'E', 'E', 'E', 'E', 'e', 'e', 'e', 'e', 'I', 'I', 'I', 'I', 'i', 'i', 'i', 'i',
                                     'O', 'O', 'O', 'O', 'O', 'O', 'o', 'o', 'o', 'o', 'o', 'o', 'U', 'U', 'U', 'U', 'u', 'u', 'u', 'u', 'C', 'c', 'N', 'n', 'Y', 'Y', 'y', 'y', 'B' };
static const uint8_t ascii_[59] = { 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xC8, 0xC9, 0xCA, 0xCB, 0xE8, 0xE9, 0xEA, 0xEB,
                                    0xCC, 0xCD, 0xCE, 0xCF, 0xEC, 0xED, 0xEE, 0xEF, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD8, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF8,
                                    0xD9, 0xDA, 0xDB, 0xDC, 0xF9, 0xFA, 0xFB, 0xFC, 0xC7, 0xE7, 0xD1, 0xF1, 0xDD, 0x9F, 0xFD, 0xFF, 0xDF };

static void process_header(SidTune *t)
{
    uint8_t *h = t->hdr;
    t->version = (h[0x04] << 8) | h[0x05];
    t->data_offset = ((uint32_t)h[0x06] << 8) | h[0x07];
    if (t->data_offset < 0x7C) {                         // convert the header to a V2 header
        for (uint32_t i = t->data_offset; i < 0x7C; i++) {
            h[i] = 0;
        }
        h[0x04] = 0;
        h[0x05] = 2;
        t->version = 2;
        h[0x07] = 0x7C;                                  // only the data offset in the header is updated, not t->data_offset (FW)
    }
    t->songs = (h[0x0E] << 8) | h[0x0F];
}

// FileTypeSID::createMusHeader
static void make_mus_header(SidTune *t, const char *name)
{
    uint8_t *h = t->hdr;
    t->mus = true;
    t->version = 3;                                      // version 3 for the support of a 2nd SID chip
    t->songs = 1;
    t->song = 1;
    if (t->mus && !t->sid) {
        memset(h, 0, 0x80);
    } else {
        memset(h, 0, 0x16);
        memset(h + 0x36, 0, 0x80 - 0x36);
    }
    h[0x00] = 'P'; h[0x01] = 'S'; h[0x02] = 'I'; h[0x03] = 'D';
    h[0x05] = (uint8_t)t->version;
    h[0x07] = 0x7C;                                      // header size
    h[0x0A] = 0xEC; h[0x0B] = 0x60;                      // init address
    h[0x0C] = 0xEC; h[0x0D] = 0x80;                      // play address
    h[0x0F] = (uint8_t)t->songs;
    h[0x11] = 0x01;                                      // default song 1
    h[0x15] = 0x01;                                      // play at CIA speed
    h[0x36] = 0x3C; h[0x37] = 0x3F; h[0x38] = 0x3E;      // default author unknown
    h[0x56] = 0x31; h[0x57] = 0x39; h[0x58] = 0x3F; h[0x59] = 0x3F;   // default release year unknown
    h[0x5A] = 0x20;
    h[0x5B] = 0x3C; h[0x5C] = 0x3F; h[0x5D] = 0x3E;      // default publisher unknown
    h[0x77] = 0x29;                                      // default flags set for 8580, NTSC and MUS data only
    if (h[0x16] == 0 && name) {                          // the file's name as the title
        int size = (int)strlen(name);
        for (int i = size - 1; i > 0; i--) {
            if (name[i] == '.') {
                size = i > 0x20 ? 0x20 : i;
                break;
            }
        }
        for (int b = 0; b < size; b++) {
            char c = name[b];
            if (c == 0) {
                break;
            }
            h[0x16 + b] = (uint8_t)(c == 0x5f ? 0x20 : c);   // underscore to space
        }
    }
    t->valid = true;
}

bool sid_parse(const uint8_t *head, uint32_t n, const char *name, bool ext_mus, SidTune *t)
{
    memset(t, 0, sizeof(*t));
    t->mus = ext_mus;
    t->sid = !ext_mus;
    if (ext_mus) {
        make_mus_header(t, name);
        return true;
    }
    if (n < SID_HDR_READ) {
        return false;
    }
    memcpy(t->hdr, head, SID_HDR_READ);
    if (memcmp(t->hdr, "PSID", 4) && memcmp(t->hdr, "RSID", 4)) {
        return false;
    }
    t->valid = true;
    process_header(t);
    if ((t->hdr[0x77] & 1) == 1) {
        make_mus_header(t, name);
    }
    return true;
}

int sid_info_text(const SidTune &t, char *out, int outlen)
{
    int p = 0;
    for (int b = 0; b < 3; b++) {
        int len = string_offsets[b + 1] - string_offsets[b];
        char nm[36];
        memcpy(nm, &t.hdr[string_offsets[b]], len);
        nm[len] = 0;
        for (int j = 0; j < len; j++) {
            for (unsigned i = 0; i < sizeof(ascii_); i++) {
                if ((uint8_t)nm[j] == ascii_[i]) {
                    nm[j] = (char)petscii[i];
                    break;
                }
            }
        }
        p += snprintf(out + p, outlen - p, "%s\n", nm);
        if (t.mus) {
            break;                                       // only the title of a MUS file
        }
    }
    if (!t.mus) {
        p += snprintf(out + p, outlen - p, "\nSID version: %d\n", t.version);
    }
    p += snprintf(out + p, outlen - p, "\nNumber of songs: %d\n", t.songs);
    int sng = (t.hdr[0x10] << 8) | t.hdr[0x11];
    p += snprintf(out + p, outlen - p, "Default song: %d\n", sng);
    return p < outlen ? p : outlen - 1;
}

int sid_plan(SidTune *t, uint32_t fsize, uint16_t load_le, int song, const char **why)
{
    uint8_t *h = t->hdr;
    *why = 0;
    t->skip = 0;
    if (t->mus && !t->sid) {                             // a .mus / .str: its own header, the data at $1000
        t->data_offset = 0;
        t->start = 0x1000;
        t->skip = 2;
    } else if ((h[0x77] & 1) == 1) {
        t->start = 0x1000;                               // sidplayer music data at $1000
        t->skip = 2;
    } else {
        t->start = (uint16_t)((h[0x08] << 8) | h[0x09]);
        if (t->start == 0) {
            t->start = load_le;
        }
        t->skip = 2;
    }
    int32_t length = (int32_t)fsize - (int32_t)t->data_offset - t->skip;
    if (length < 0) {
        *why = "Can't read header";
        return 1;
    }
    t->end = (uint16_t)(t->start + length);
    h[0x7E] = (uint8_t)(t->end & 0xFF);
    h[0x7F] = (uint8_t)(t->end >> 8);
    if (t->end < t->start) {
        *why = "Illegal: Load rolls over $FFFF";
        return 1;
    }
    if (t->start >= 0x03C0) {
        t->header_location = 0x0340;
    } else if (t->end < 0xFF70) {
        t->header_location = 0xFF70;
    } else {
        *why = "No memory location for player data";
        return 1;
    }
    t->default_song = (uint16_t)((h[0x10] << 8) | h[0x11]);
    if (!t->default_song) {
        t->default_song = 1;
    }
    t->song = song ? (uint16_t)song : t->default_song;
    if (t->song > t->songs) {
        *why = "Song Number out of Range";
        return 1;
    }
    h[0x10] = (uint8_t)((t->song - 1) >> 8);             // written back for the players that only look here
    h[0x11] = (uint8_t)((t->song - 1) & 0xFF);
    t->flags = (uint16_t)((h[0x76] << 8) | h[0x77]);
    for (int i = 0; i < 7; i++) {                        // big endian words to little endian
        uint8_t a = h[4 + 2 * i];
        h[4 + 2 * i] = h[5 + 2 * i];
        h[5 + 2 * i] = a;
    }
    return 0;
}

int sid_requests(const SidTune &t, SidReq out[3])
{
    int count = 1;
    out[0].base = 0x40;                                  // D400
    out[0].type = 3;                                     // not specified
    if (t.version >= 2) {
        if ((t.flags >> 4) & 3) {
            out[0].type = (uint8_t)((t.flags >> 4) & 3);
        }
        if (t.version >= 3 && t.hdr[0x7A]) {
            out[1].base = t.hdr[0x7A];
            out[1].type = (uint8_t)(((t.flags >> 6) & 3) ? ((t.flags >> 6) & 3) : out[0].type);
            count = 2;
        }
        if (t.version >= 4 && t.hdr[0x7B]) {
            out[2].base = t.hdr[0x7B];
            out[2].type = (uint8_t)(((t.flags >> 8) & 3) ? ((t.flags >> 8) & 3) : out[0].type);
            count = 3;
        }
    }
    return count;
}

bool sid_mus_stereo(const SidTune &t, uint32_t load_end, uint8_t (*rd)(void *, uint32_t), void *user, uint32_t *first_end)
{
    uint32_t s = t.start;
    uint32_t text = s + ((uint32_t)(rd(user, s + 1) + rd(user, s + 3) + rd(user, s + 5)) << 8) + rd(user, s) + rd(user, s + 2) + rd(user, s + 4) + 6;
    uint32_t next = load_end;
    for (uint32_t i = text; i < load_end; i++) {
        if (rd(user, i) == 0) {
            next = i + 3;
            break;
        }
    }
    *first_end = next;
    return next + 5 < load_end;                          // one file holds a stereo song
}

void sid_mus_header(SidTune *t, bool stereo)
{
    if (stereo) {
        t->hdr[0x0A] = 0x90;
        t->hdr[0x0B] = 0xFC;                             // stereo init address (little endian)
        t->hdr[0x0C] = 0x96;
        t->hdr[0x0D] = 0xFC;                             // stereo play address
        t->hdr[0x7A] = 0x50;                             // the second SID at $D500
        t->hdr[0x77] |= 0x80;                            // set second SID to 8580
    }
    if (t->end >= 0xC400) {
        t->hdr[0x78] = 0x04;
        t->hdr[0x79] = 0x0C;
    }
}
