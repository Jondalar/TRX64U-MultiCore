// The C64U's audio sample-rate converter (the resample library, u64_resample.cc): programs the filter of the U64's resampler for a video mode. PROVENANCE: Gideon Zweijtzer's U64Config::SetResampleFilter
// (vendor/gideon/software/u64/u64_config.cc, GPL-3.0). A placed core's file calls it directly; the generic application does not.
#ifndef COREAPP_U64_RESAMPLE_H
#define COREAPP_U64_RESAMPLE_H

#include "u64.h"   // t_video_mode

// Loads the coefficient table of the mode's colour clock (PAL: 675 taps, NTSC: 512) into the resampler and sets its labour.
void u64_resample_filter(t_video_mode m);

#endif
