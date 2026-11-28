// T93 D9: the video service - the layer set for a core's picture, the flip to the newest complete frame, hide and show with
// exactly the registers that were written (the layer's registers cannot be read back, T93 R2).
#include "check.h"
#include "fakes.h"
#include "video.h"
#include "videomode.h"
#include "trxcanvas.h"
#include "cores.h"
// the NES's app (run.sh nesapp, nes_video): ADR-0057 on its record - the NES draws 256 x 224 at scale 3 and fills 93 % of 720p's height where 1080p leaves
// 38 % of it black; the mode comes out of the picture, no table names it
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
int main()
{
    PxRec px2;
    FakeIo io2;
    Video nes_v(nes_desc.video, &px2, &io2);
    CHECK_EQ(nes_v.wanted_hdmi(), VID_HDMI_720P);
    DONE("test_video");
}
