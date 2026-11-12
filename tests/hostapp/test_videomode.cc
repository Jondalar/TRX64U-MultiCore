// The picture the applications choose (T105): what an EDID says, and the hand-over's packing.
// The control the whole thing exists for: a screen that lists no 50 Hz mode must not be given one.
#include "videomode.h"
#include "edid.h"
#include <stdio.h>
#include <string.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// The block's checksum byte, so that the 128 bytes add up to 0 mod 256 (a corrupt block is the EDID's own refusal, tests/coreapp/test_edid.cc).
static void fix(uint8_t *b)
{
    unsigned t = 0;
    for (int i = 0; i < 127; i++) {
        t += b[i];
    }
    b[127] = (uint8_t)(0x100 - (t & 0xFF));
}

// A base block with a valid header; `ext` says how many extensions follow.
static void base(uint8_t *e, int ext)
{
    memset(e, 0, 128);
    static const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    memcpy(e, hdr, 8);
    e[126] = (uint8_t)ext;
    fix(e);
}

// A CEA-861 extension holding one Video Data Block with the given VICs.
static void cea(uint8_t *x, const uint8_t *vics, int n)
{
    memset(x, 0, 128);
    x[0] = 0x02;                      // CEA extension
    x[1] = 0x03;                      // revision 3
    x[3] = 0x00;
    x[4] = (uint8_t)((2 << 5) | n);   // tag 2 = video data block, length n
    for (int i = 0; i < n; i++) {
        x[5 + i] = vics[i];
    }
    x[2] = (uint8_t)(5 + n);          // the detailed timings start after the collection
    fix(x);
}

int main()
{
    uint8_t e[256];

    // --- a European television: it lists 1080p50, and that is what it gets
    { const uint8_t v[] = { 31, 19, 17 }; base(e, 1); cea(e + 128, v, 3); }
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_1080P);

    // --- the screen this was written for: 60 Hz only. In PAL it must fall back, never to 1080p50.
    { const uint8_t v[] = { 16, 4, 3 }; base(e, 1); cea(e + 128, v, 3); }
    CHECK(video_mode_from_edid(e, 256, true) != VID_HDMI_1080P);
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_480_576);
    CHECK(video_mode_from_edid(e, 256, false) == VID_HDMI_1080P);     // the same screen in NTSC

    // --- a television that stops at 720p
    { const uint8_t v[] = { 19, 17 }; base(e, 1); cea(e + 128, v, 2); }
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_720P);
    { const uint8_t v[] = { 4, 3 }; base(e, 1); cea(e + 128, v, 2); }
    CHECK(video_mode_from_edid(e, 256, false) == VID_HDMI_720P);

    // --- the native bit is not part of the number
    { const uint8_t v[] = { (uint8_t)(31 | 0x80) }; base(e, 1); cea(e + 128, v, 1); }
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_1080P);

    // --- a PC monitor: an EDID with no CEA extension at all
    base(e, 0);
    CHECK(video_mode_from_edid(e, 128, true) == VID_HDMI_1024x768);
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_1024x768);

    // --- nothing answered, or nonsense answered
    CHECK(video_mode_from_edid(0, 0, true) == VID_HDMI_480_576);
    memset(e, 0, sizeof(e));
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_480_576);
    base(e, 1); memset(e + 128, 0xFF, 128); fix(e + 128);  // an extension that is not CEA
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_1024x768);

    // --- a collection whose lengths walk out of the block is not believed
    { const uint8_t v[] = { 31 }; base(e, 1); cea(e + 128, v, 1); }
    e[128 + 4] = (uint8_t)((2 << 5) | 31);                  // claims 31 bytes where 1 was written
    e[128 + 2] = 6;
    fix(e + 128);
    CHECK(video_mode_from_edid(e, 256, true) == VID_HDMI_480_576);

    // --- the hand-over's packing: 0 means nobody chose
    int s = -9, h = -9;
    CHECK(!video_unpack(0, &s, &h));
    CHECK(video_unpack(video_pack(VID_SYS_NTSC60, VID_HDMI_720P), &s, &h));
    CHECK(s == VID_SYS_NTSC60 && h == VID_HDMI_720P);
    CHECK(video_pack(VID_SYS_PAL50, VID_HDMI_480_576) != 0);          // mode 0 is still a choice
    CHECK(video_unpack(video_pack(VID_SYS_PAL50, VID_HDMI_480_576), &s, &h) && s == 0 && h == 0);
    CHECK(video_pack(0, VID_HDMI_COUNT) == 0);                        // out of range is no choice
    CHECK(!video_unpack(0x80000000u | 99, &s, &h));
    for (int i = 0; i < VID_HDMI_COUNT; i++) {
        CHECK(strcmp(video_hdmi_name(i), "?") != 0);
    }

    // ADR-0057: the mode is derived from what the core draws, so a core never has one written down by
    // hand. The two numbers Alex saw on the board are the cases.
    CHECK(video_mode_fits(768, 672, false) == VID_HDMI_720P);         // NES 256x224 at scale 3: 93 % of 720
    CHECK(video_mode_fits(1152, 864, false) == VID_HDMI_1080P);       // Atari 384x288 at scale 3: 864 needs 1080
    CHECK(video_mode_fits(720, 540, false) == VID_HDMI_720P);         // the C64's own layer
    CHECK(video_mode_fits(1920, 1080, false) == VID_HDMI_1080P);      // exactly a frame
    CHECK(video_mode_fits(2000, 1200, false) == VID_HDMI_1080P);      // fits nowhere: the mode we came up in
    CHECK(video_mode_fits(0, 0, false) == VID_HDMI_1080P);
    CHECK(video_mode_fits(640, 400, false) == VID_HDMI_480_576);      // small: the smallest frame that holds it
    CHECK(video_mode_fits(900, 700, false) == VID_HDMI_720P);         // too wide for 720x480
    // the PC modes only when they are allowed: 760 lines do not fit 720p, so 1024x768 holds them and
    // 1080p would waste 30 % of the height
    CHECK(video_mode_fits(1000, 760, true) == VID_HDMI_1024x768);
    CHECK(video_mode_fits(1000, 760, false) == VID_HDMI_1080P);
    int mw = 0, mh = 0;
    video_mode_size(VID_HDMI_1080P, &mw, &mh);
    CHECK(mw == 1920 && mh == 1080);
    video_mode_size(VID_HDMI_COUNT, &mw, &mh);
    CHECK(mw == 0 && mh == 0);

    printf("test_videomode: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
