// What the screen takes (T164 G11, D7): the EDID parsed into a mask over the host's HDMI mode table, and the choice among the rows.
//
// The table is the 9 rows of host/hdmi_modes.vh (Q5), numbered as the gateware's request register numbers them: row m = family * 6 + hdmi,
// family 0 = 50 Hz, 1 = 60 Hz, hdmi = a VID_HDMI_* value. Rows 3, 4, 5 (the PC modes at 50 Hz) do not exist. Pure code, no board: the
// applications read the EDID (video_read_edid) and hand the bytes in.
#ifndef APP_VIDEO_EDID_H
#define APP_VIDEO_EDID_H

#include <stdint.h>
#include "videomode.h"

#define EDID_ROWS 12
#define EDID_ROW_MASK   0x0FC7u   // the rows that exist: 0, 1, 2 and 6 .. 11
#define EDID_LIST_NONE  0x01C7u   // no EDID, or a corrupt one (Q9): the six television rows, both families

struct EdidInfo {
    bool     valid;      // header, and the checksum of every block that was read
    bool     has_cea;    // a CEA-861 extension is there
    uint16_t mask;       // the rows the screen names (bit m), after the vetoes
    uint16_t native;     // the rows its short video descriptors flag as native
    int      max_tmds_mhz;   // the HDMI vendor block's limit, 0 when it names none
};

// One row of the table, for tests and for the menus' names. NULL for a row that does not exist.
struct EdidRow {
    uint16_t w, h;               // active area
    uint16_t htotal, vtotal;     // the raster
    uint32_t pixel_hz;           // hm_rate
    uint8_t  vic[3];             // the CEA codes that name this row (0 = unused)
};
const EdidRow *edid_row(int m);

inline int video_row_sys(int m)  { return m / 6; }
inline int video_row_hdmi(int m) { return m % 6; }
inline int video_row_of(int hdmi, int sys) { return (sys == VID_SYS_NTSC60 ? 6 : 0) + hdmi; }

// Parses the base block and the CEA extension. `valid` false: nothing else in `*out` is to be believed (mask = 0).
bool edid_parse(const uint8_t *edid, int len, EdidInfo *out);

// The rows the application may offer and apply (D11a). With no EDID or a corrupt one: EDID_LIST_NONE (Q9). A valid EDID that names none of
// the rows: the television rows when it has a CEA block (it wants something we cannot make), else 1024 x 768 at 60 Hz (a DVI monitor has it).
uint16_t video_modes_offered(const uint8_t *edid, int len);

// Q6: the machine's family first (1080p > 720p > 576p/480p > PC modes), the other family only when the mask has nothing in the machine's
// family. `sys` is VID_SYS_*. Returns a row, -1 when the mask names none.
int video_modes_best(uint16_t mask, int sys);

// T164 WP16, Q6 / D11a: the resolutions a menu offers for the machine's family `sys`, from the mask. A television row of the machine's family is
// listed when the mask names it; a PC mode (it has no 50 Hz row) when the mask names its 60 Hz row; when the mask has nothing in the machine's family
// at all, every resolution the other family names (the silent fallback: the frames are repeated or dropped). Never both families of one
// resolution and never a Hz choice. `hdmi` receives VID_HDMI_* values in ascending order (at most VID_HDMI_COUNT); returns their number.
int video_modes_hdmi_list(uint16_t mask, int sys, int *hdmi);
// The row the gateware is asked for when the person chose `hdmi`: the machine's family row when the mask names it, else the other family's.
// False when `hdmi` is not in video_modes_hdmi_list() (a stored choice the screen no longer takes).
bool video_modes_resolve(uint16_t mask, int sys, int hdmi, int *row);

// What the application starts with when nobody chose: the best row of the screen's mask, and for no or a corrupt EDID (or one that names
// nothing we drive, see above) the default of Q9, 576p/480p of the machine's family. Returns a row.
int video_modes_default(const uint8_t *edid, int len, int sys);

#endif
