// T228 R21 item 7: the SID header work of coreapp/src/sidfile.cc against FW's filetype_sid.cc (readHeader, processHeader, createMusHeader, showInfo, prepare, ConfigSIDs,
// configureMusEnv). Not run by the agent that wrote it (T228: the coordinator runs the tests once); the numbers are worked out by hand from FW's code.
#include "check.h"
#include "sidfile.h"

static void put_be16(uint8_t *h, int o, int v) { h[o] = (uint8_t)(v >> 8); h[o + 1] = (uint8_t)v; }

static void psid(uint8_t *h, int version, int load, int songs, int def, int flags)
{
    memset(h, 0, SID_HDR_READ);
    memcpy(h, "PSID", 4);
    put_be16(h, 4, version);
    put_be16(h, 6, 0x7C);
    put_be16(h, 8, load);
    put_be16(h, 0x0A, 0x1000);
    put_be16(h, 0x0C, 0x1003);
    put_be16(h, 0x0E, songs);
    put_be16(h, 0x10, def);
    put_be16(h, 0x76, flags);
    memcpy(h + 0x16, "Title", 5);
    memcpy(h + 0x36, "Author", 6);
    memcpy(h + 0x56, "2026", 4);
}

int main()
{
    uint8_t h[SID_HDR_READ];
    SidTune t;
    // the plain case: header names the load address, 3 songs, default 2
    psid(h, 2, 0x1000, 3, 2, 0);
    CHECK(sid_parse(h, sizeof(h), "x.sid", false, &t));
    CHECK_EQ(t.songs, 3);
    CHECK_EQ(t.version, 2);
    const char *why = 0;
    // 0x7C header + 0x100 bytes of data; the header names the address, so FW's length is 2 short of the data: end = 0x1000 + 0x100 - 2
    CHECK_EQ(sid_plan(&t, 0x7C + 0x100, 0, 0, &why), 0);
    CHECK_EQ(t.start, 0x1000);
    CHECK_EQ(t.end, 0x1000 + 0x100 - 2);
    CHECK_EQ(t.header_location, 0x0340);
    CHECK_EQ(t.song, 2);                                   // the default song
    CHECK_EQ(t.hdr[0x10], 1);                              // song - 1, written back big endian, then turned little endian with the other six words (FW filetype_sid.cc:692-700)
    CHECK_EQ(t.hdr[0x11], 0);
    CHECK_EQ(t.hdr[4], 2);                                 // the words are little endian now: version 2 = 02 00
    CHECK_EQ(t.hdr[5], 0);
    CHECK_EQ(t.hdr[0x7E], (uint8_t)t.end);
    // a song out of range
    psid(h, 2, 0x1000, 3, 1, 0);
    sid_parse(h, sizeof(h), "x.sid", false, &t);
    CHECK(sid_plan(&t, 0x7C + 0x100, 0, 4, &why) != 0);
    // load address 0: the first two bytes of the data say it; a tune low in memory gets its header above
    psid(h, 2, 0, 1, 1, 0);
    sid_parse(h, sizeof(h), "x.sid", false, &t);
    CHECK_EQ(sid_plan(&t, 0x7C + 0x50, 0x0200, 0, &why), 0);
    CHECK_EQ(t.start, 0x0200);
    CHECK_EQ(t.header_location, 0xFF70);
    // a tune that runs to the top leaves no room
    psid(h, 2, 0x0200, 1, 1, 0);
    sid_parse(h, sizeof(h), "x.sid", false, &t);
    CHECK(sid_plan(&t, 0x7C + 0xFE00, 0, 0, &why) != 0);
    // not a tune
    memset(h, 0, sizeof(h));
    CHECK(!sid_parse(h, sizeof(h), "x.sid", false, &t));
    // ConfigSIDs: version 3 with a second SID at $D500, 8580 asked for the first, nothing said for the second (it takes the first's type)
    psid(h, 3, 0x1000, 1, 1, 0x20);
    h[0x7A] = 0x50;
    sid_parse(h, sizeof(h), "x.sid", false, &t);
    CHECK_EQ(sid_plan(&t, 0x7C + 0x40, 0, 0, &why), 0);
    SidReq r[3];
    CHECK_EQ(sid_requests(t, r), 2);
    CHECK_EQ(r[0].base, 0x40);
    CHECK_EQ(r[0].type, 2);
    CHECK_EQ(r[1].base, 0x50);
    CHECK_EQ(r[1].type, 2);
    // a .mus: its own header, the name is the title (underscore to blank), one song, data at $1000
    CHECK(sid_parse(0, 0, "My_Tune.mus", true, &t));
    CHECK_EQ(t.songs, 1);
    CHECK_STR((const char *)t.hdr + 0x16, "My Tune");
    CHECK_EQ(sid_plan(&t, 2 + 0x300, 0, 0, &why), 0);
    CHECK_EQ(t.start, 0x1000);
    CHECK_EQ(t.end, 0x1000 + 0x300);
    // Show Info
    char txt[256];
    psid(h, 2, 0x1000, 3, 2, 0);
    sid_parse(h, sizeof(h), "x.sid", false, &t);
    int n = sid_info_text(t, txt, sizeof(txt));
    CHECK(n > 0);
    CHECK(strstr(txt, "Title\n") != 0);
    CHECK(strstr(txt, "SID version: 2") != 0);
    CHECK(strstr(txt, "Number of songs: 3") != 0);
    CHECK(strstr(txt, "Default song: 2") != 0);
    DONE("sidfile");
}
