#include <stdint.h>
// picture.cc is part of the base every core application links (it registers the Picture operation) and calls the board's video functions. The
// benches that do not look at the picture link this one, which does nothing and finds no gateware and no screen; a bench that does has its own
// definitions, and theirs win.
void video_apply(int, int) __attribute__((weak));
void video_apply(int, int) { }
bool video_apply_gateware(int, int) __attribute__((weak));
bool video_apply_gateware(int, int) { return true; }
bool video_gateware_present(void) __attribute__((weak));
bool video_gateware_present(void) { return false; }
int video_gateware_row(void) __attribute__((weak));
int video_gateware_row(void) { return -1; }
bool video_gateware_vic_scale(void) __attribute__((weak));
bool video_gateware_vic_scale(void) { return false; }
bool video_set_scale_gateware(int) __attribute__((weak));
bool video_set_scale_gateware(int) { return true; }
bool video_gateware_vic_smooth(void) __attribute__((weak));
bool video_gateware_vic_smooth(void) { return false; }
bool video_set_smooth_gateware(int) __attribute__((weak));
bool video_set_smooth_gateware(int) { return true; }
bool video_gateware_follow(void) __attribute__((weak));
bool video_gateware_follow(void) { return false; }
bool video_set_follow_gateware(int) __attribute__((weak));
bool video_set_follow_gateware(int) { return true; }
bool video_edid_sig(uint32_t *) __attribute__((weak));
bool video_edid_sig(uint32_t *) { return false; }
bool video_read_edid(uint8_t *, int, int *len) __attribute__((weak));
bool video_read_edid(uint8_t *, int, int *len) { *len = 0; return false; }
