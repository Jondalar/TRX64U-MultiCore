// T97 5.10 `fbwin`: the picture is the full frame (90 words x 270 lines) centred on the middle of the chipset's display window, and shrinks
// symmetrically (letterbox, equal borders) where that does not fit the stream; the layer is cut with set_view only when the size changes. The compute step is pure (the mapping is the one the gateware bench cores/amiga/sim/run_fbwin.sh measured: the
// standard window 0x81..0x1C1 x 0x2C..0x12C is source x 76..716, lines 17..273); the service looks at the display window now and then and, when
// ten looks agree, moves the crop window with the frame writer off and waits until every buffer has it. The pixel layer is only touched when the size changes.
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "video.h"


static void set_diw(AmigaIo &io, uint32_t hs, uint32_t he, uint32_t vs, uint32_t ve)
{
    uint32_t h = hs | (he << 16), v = vs | (ve << 16);
    for (int i = 0; i < 4; i++) {
        io.regs[AM_DIW_H + i] = (uint8_t)(h >> (8 * i));
        io.regs[AM_DIW_V + i] = (uint8_t)(v >> (8 * i));
    }
}

static int count_size(const AmigaRig &r, size_t from = 0)
{
    int n = 0;
    for (size_t i = from; i < r.px.w.size(); i++) n += r.px.w[i].reg == PX_SIZE;
    return n;
}

static int count_reg(const AmigaIo &io, uint32_t reg, size_t from = 0)
{
    int n = 0;
    for (size_t i = from; i < io.log.size(); i++) n += io.log[i].reg == reg;
    return n;
}

// looks at the display window, a frame every poll, for a while
static void run(AmigaRig &r, int polls, int ms_per_poll = 20, bool frames = true)
{
    for (int n = 0; n < polls; n++) {
        r.io.delay_ms(ms_per_poll);
        if (frames) r.io.regs[AM_FRAME_SEQ]++;
        fbwin_poll(&r.c);
    }
}

int main()
{
    // ---- the Amiga 500 has no record
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga_desc);
        CHECK(amiga_desc.fbwin == 0);
        r.io.regs[AM_FEAT] = AM_FEAT_FBWIN | 1;
        engines_of(&r.c);
        set_diw(r.io, 0x91, 0x1A1, 0x2C, 0x12C);                             // a window that would move the picture
        run(r, 20, 100);
        CHECK_EQ(r.io.log.size(), 0u);
    }
    DONE("fbwin");
}
