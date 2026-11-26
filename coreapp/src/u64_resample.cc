// The C64U's audio sample-rate converter (u64_resample.h): the filter coefficients of the U64's resampler for a video mode. PROVENANCE: vendor/gideon/software/u64/u64_config.cc (Gideon Zweijtzer,
// GPL-3.0), U64Config::SetResampleFilter; the register addresses are vendor/gideon/software/system/u64.h. Board code (it writes the U64 registers): the native tests stand without it.
#include "u64_resample.h"
#include "u64.h"
#include "color_timings.h"
#include <stdint.h>

#include "u64_resample.inc"

void u64_resample_filter(t_video_mode m)
{
    const t_video_color_timing *ct = color_timings[(int)m];
    int mode = (ct->audio_div == 77) ? 0 : 1;
    const uint32_t *pf = (mode == 0) ? (const uint32_t *)c_filter2_coef_pal_alt : (const uint32_t *)c_filter2_coef_ntsc;
    int size = (mode == 0) ? 675 : 512;
    U64_RESAMPLE_RESET = 1;
    U64_RESAMPLE_FLUSH = 1;
    for (int i = 0; i < size; i++) {
        U64_RESAMPLE_DATA = *(pf++);
    }
    U64_RESAMPLE_LABOR = (mode == 0) ? 45 : 169;
    U64_RESAMPLE_FLUSH = 0;
}
