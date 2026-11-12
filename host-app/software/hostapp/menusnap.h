// T146: the saved picture of the menu (Flash:/menu.snap). Pure code: an RLE codec for an RGB565 frame, the key that says which
// menu the picture belongs to, the settle timer that decides when to save, and the queue for keys read while the picture is up.
// Nothing here touches LVGL, a file or the layer; hostui.cc does that.
#ifndef HOST_MENUSNAP_H
#define HOST_MENUSNAP_H

#include <stdint.h>

#define MS_W 960
#define MS_H 540
#define MS_PIXELS (MS_W * MS_H)
#define MS_RAW_BYTES (MS_PIXELS * 2)
#define MS_HDR_BYTES 24
#define MS_LIMIT (1024 * 1024)            // D5: a picture above this is not saved
#define MS_QUIET_MS 2000                  // D2: unchanged for this long before it is saved
#define MS_PATH "/Flash/menu.snap"
#define MS_DIR "/Flash"
#define MS_FILE "menu.snap"

enum MsResult {
    MS_OK = 0, MS_TOO_SHORT, MS_MAGIC, MS_FORMAT, MS_SIZE, MS_KEY, MS_LEN, MS_CRC, MS_DATA
};
const char *ms_result_text(MsResult r);

uint32_t ms_crc32(uint32_t crc, const void *data, uint32_t n);      // IEEE CRC-32; start with 0, chain by passing the last result

// The key: CRC-32 over boot.cfg's text, the picture mode (system, hdmi) and the host app's git id.
uint32_t ms_key(const char *cfg_text, int cfg_len, int vid_sys, int vid_hdmi, const char *git_id);

// Largest encoded size of npx pixels (header included): the encoder never needs more than this.
uint32_t ms_max_encoded(uint32_t npx);

// Header + RLE data of a w x h frame into out (max_out bytes). Returns the file length, -1 when it does not fit
// (min(max_out, MS_LIMIT)). The data CRC and the length are in the header.
int ms_encode(const uint16_t *px, int w, int h, uint32_t key, uint8_t *out, uint32_t max_out);

// The file image `file` (len bytes) into px (w x h). Refuses, writing nothing useful, when the header is not ours, the size or
// the key differ, the length or the CRC do not fit, or the RLE data does not make exactly w*h pixels.
MsResult ms_load(const uint8_t *file, uint32_t len, int w, int h, uint32_t key, uint16_t *px);

// The key and the data CRC a file image's header holds (for "the same picture again"); false when it is no header.
bool ms_header_info(const uint8_t *file, uint32_t len, uint32_t *key, uint32_t *data_crc);

// ---- when to save (D2) ----
struct MsSettle {
    bool primed;                      // a signature has been seen
    uint32_t sig;
    uint32_t since;                   // ms: the signature has been this one since
    bool done;                        // a picture of `done_sig` has been saved (or tried)
    uint32_t done_sig;
};
void ms_settle_init(MsSettle *s);
// Called every few hundred ms with the signature of what is on the screen; eligible = the menu is in the state a picture may
// show (menu view, nothing running, nothing pending). True once, when the signature has been unchanged for quiet_ms, is
// eligible and differs from the one saved last.
bool ms_settle(MsSettle *s, uint32_t now, uint32_t sig, bool eligible, uint32_t quiet_ms);
void ms_settle_saved(MsSettle *s);      // the caller saved (or tried to) for the current signature
void ms_settle_touch(MsSettle *s, uint32_t now);   // a key was handled: the quiet time starts again

// ---- D4 keys read while the picture is up ----
#define MS_KEYQ 32
struct MsKeyQ {
    int k[MS_KEYQ];
    int n;
    int dropped;
};
void ms_keyq_init(MsKeyQ *q);
bool ms_keyq_push(MsKeyQ *q, int key);  // false (and counted) when full
int ms_keyq_pop(MsKeyQ *q);             // -1 when empty

#endif
