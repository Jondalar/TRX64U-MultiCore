// T213.3 P3 (the A1200's cases of test_home, test_reopen, test_volume, test_tone and test_baseset, which run on the A500's record in its own app, run.sh amigaapp, T239.5 D13): the
// application's own rows on the A1200's record. No descriptor declares Volume, Tone or the Home items; the base adds them (Volume and Tone one row each, Home three) and leaves the
// A1200's own mouse rows (operations mouse.sens / mouse.accel) alone. Built as the A1200's app (run.sh a12app, TEST_APP_A1200).
#include <string.h>
#include <strings.h>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "settings.h"
#include "volume.h"
#include "tone.h"
#include "home.h"
#include "baseset.h"
#include "video.h"
#include "amiga_regs.h"

static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcasecmp(d.settings[i].key, key) == 0;
    return n;
}

int main()
{
    static CoreDesc vd, td, hd, h1, h2;
    static Setting vs[SETTINGS_MAX], ts[SETTINGS_MAX], hs[SETTINGS_MAX], s1[SETTINGS_MAX], s2[SETTINGS_MAX];
    CHECK(amiga1200_desc.nreport_regs > 0 && amiga1200_desc.report_regs);   // T213.3: the core report's registers (test_corereport's real-descriptor case, D3)
    CHECK_EQ(count_key(amiga1200_desc, VOLUME_KEY), 0);                  // no descriptor declares them: the application adds them
    CHECK_EQ(count_key(amiga1200_desc, TONE_KEY), 0);
    CHECK_EQ(count_key(amiga1200_desc, HOME_DIR_KEY), 0);
    CHECK_EQ(count_key(amiga1200_desc, HOME_ENTER_KEY), 0);
    CHECK(!volume_extend(&amiga1200_desc, &vd, vs));                     // the A1200 gets the same item as every core
    CHECK_EQ(vd.nsettings, amiga1200_desc.nsettings + 1);
    CHECK_EQ(count_key(vd, VOLUME_KEY), 1);
    CHECK(volume_is_generic(vd.settings[vd.nsettings - 1]));
    CHECK(!tone_extend(&amiga1200_desc, &td, ts));
    CHECK_EQ(td.nsettings, amiga1200_desc.nsettings + 1);
    CHECK_EQ(count_key(td, TONE_KEY), 1);
    CHECK(!home_extend(&amiga1200_desc, &hd, hs));
    CHECK_EQ(hd.nsettings, amiga1200_desc.nsettings + 3);
    CHECK_EQ(count_key(hd, HOME_DIR_KEY), 1);
    CHECK_EQ(count_key(hd, HOME_ENTER_KEY), 1);
    CHECK(!home_extend(&amiga1200_desc, &h1, s1));                       // a description that has the items keeps them: no second pair
    CHECK(!home_extend(&h1, &h2, s2));
    CHECK_EQ(h2.nsettings, h1.nsettings);
    // D25: the A1200 declares Mouse Sensitivity / Acceleration itself; the application adds neither
    CHECK_EQ(count_key(amiga1200_desc, "Mouse Sensitivity"), 1);
    CHECK_EQ(count_key(amiga1200_desc, "Mouse Acceleration"), 1);
    CHECK(!baseset_adds(&amiga1200_desc, "Mouse Sensitivity") && !baseset_adds(&amiga1200_desc, "Mouse Acceleration"));
    // T136 S3: the A1200 shows 32-bit pixels (the layer's format 2, its own pixel layer) from buffers at 0x4000000, 2 MB apart
    {
        CHECK_EQ(Video::ctrl_value(2, true, 2), 0x25);
        const VideoDesc &a = amiga1200_desc.video;
        CHECK_EQ(a.pixel_bytes(), 4u);
        CHECK_EQ(a.fb_base, 0x04000000u);
        CHECK_EQ(a.fb_stride, 0x200000u);
        CHECK(a.fb_stride >= 720u * 540u * 4u);
        FakeIo aio;
        PxRec apx;
        Video av(a, &apx, &aio);
        av.init();                                            // outside the application's window: nothing to clear, nothing written
        CHECK(aio.log.empty());
        av.show();
        CHECK_EQ(apx.last(PX_CTRL), 0x25);
        CHECK_EQ(apx.last(PX_STRIDE), 720u * 4u);
        CHECK_EQ(apx.last(PX_FRONT), 0x04000000u);
        aio.regs[AM_FB_LATEST] = 0x80 | 2;
        aio.regs[AM_FRAME_SEQ] = 1;
        CHECK(av.poll());
        CHECK_EQ(apx.last(PX_BACK), 0x04000000u + 2 * 0x200000u);
    }
    DONE("a12base");
}
