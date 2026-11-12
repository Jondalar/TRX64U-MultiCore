// The words of the media level of the File Browser and of the format pages (T111 D6, D7). Pure code, so the bench reads exactly what the
// person reads and the layout code in hostui.cc only places it.
#ifndef HOST_MEDIAUI_H
#define HOST_MEDIAUI_H

#include <stdint.h>
#include "mediasniff.h"
#include "fmt.h"

// "14.4 GiB", "512 MiB", "1.9 TiB": binary units, one decimal from GiB up. bytes may be 64 bit (a 9,008,283,648-byte image reads "8.4 GiB").
void ui_size_text(char *out, int n, uint64_t bytes);

// "exFAT", "FAT32  'MEDIA'", "blank", "2 partitions", "GPT, 1 partition", "exFAT without partition table (the host cannot mount it)".
void ui_fs_text(char *out, int n, const MediaSniff *s);

// "3 boot menu rows live here" / "no boot menu row lives here"
void ui_rows_text(char *out, int n, int rows);

// The name of a step of the format as the progress page shows it.
const char *ui_step_name(int step);

// One line for the progress page: "Writing the exFAT structures  37 %  (1.2 MiB of 3.4 MiB)"
void ui_progress_text(char *out, int n, int step, uint32_t written, uint32_t total);

// A whole-number percentage of the work, never 100 before the format is verified (step < FST_VERIFY and written < total gives at most 99).
int ui_percent(int step, uint32_t written, uint32_t total, bool done);

#endif
