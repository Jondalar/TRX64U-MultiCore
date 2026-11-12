#include "mediaui.h"
#include <stdio.h>
#include <string.h>

void ui_size_text(char *out, int n, uint64_t bytes)
{
    const uint64_t KiB = 1024, MiB = KiB * 1024, GiB = MiB * 1024, TiB = GiB * 1024;
    if (bytes >= TiB) {
        uint64_t t10 = bytes * 10 / TiB;
        snprintf(out, n, "%u.%u TiB", (unsigned)(t10 / 10), (unsigned)(t10 % 10));
    } else if (bytes >= GiB) {
        uint64_t g10 = bytes * 10 / GiB;
        snprintf(out, n, "%u.%u GiB", (unsigned)(g10 / 10), (unsigned)(g10 % 10));
    } else if (bytes >= MiB) {
        uint64_t m10 = bytes * 10 / MiB;
        snprintf(out, n, "%u.%u MiB", (unsigned)(m10 / 10), (unsigned)(m10 % 10));
    } else {
        snprintf(out, n, "%u KiB", (unsigned)((bytes + KiB - 1) / KiB));
    }
}

void ui_fs_text(char *out, int n, const MediaSniff *s)
{
    char lab[24] = "";
    if (s->label[0]) {
        snprintf(lab, sizeof(lab), "  '%s'", s->label);
    }
    switch (s->type) {
    case MT_NONE:
        snprintf(out, n, "blank");
        break;
    case MT_FAT12: case MT_FAT16: case MT_FAT32:
        snprintf(out, n, "%s%s%s", media_type_name(s->type), lab, s->mbr ? "" : ", no partition table");
        break;
    case MT_EXFAT:
        if (s->mbr) {
            snprintf(out, n, "exFAT%s", lab);
        } else {
            snprintf(out, n, "exFAT%s, no table: not mountable", lab);
        }
        break;
    case MT_GPT:
        snprintf(out, n, "GPT, %d partition%s", s->parts, s->parts == 1 ? "" : "s");
        break;
    case MT_MULTI:
        snprintf(out, n, "%d partitions", s->parts);
        break;
    case MT_OTHER:
        snprintf(out, n, "unknown file system");
        break;
    default:
        snprintf(out, n, "unreadable");
    }
}

void ui_rows_text(char *out, int n, int rows)
{
    if (rows <= 0) {
        snprintf(out, n, "no boot menu row lives here");
    } else if (rows == 1) {
        snprintf(out, n, "1 boot menu row lives here");
    } else {
        snprintf(out, n, "%d boot menu rows live here", rows);
    }
}

const char *ui_step_name(int step)
{
    switch (step) {
    case FST_RELEASE: return "Releasing the old volume";
    case FST_CLEAR: return "Clearing the old partition table";
    case FST_WRITE: return "Writing the exFAT structures";
    case FST_TABLE: return "Writing the partition table";
    case FST_SYNC: return "Flushing the medium";
    case FST_VERIFY: return "Reading it back, mounting it";
    default: return "";
    }
}

int ui_percent(int step, uint32_t written, uint32_t total, bool done)
{
    if (done) {
        return 100;
    }
    if (step >= FST_VERIFY) {
        return 99;
    }
    if (!total) {
        return 0;
    }
    uint64_t p = (uint64_t)written * 100 / total;
    return p > 99 ? 99 : (int)p;
}

void ui_progress_text(char *out, int n, int step, uint32_t written, uint32_t total)
{
    char a[16], b[16];
    ui_size_text(a, sizeof(a), written);
    ui_size_text(b, sizeof(b), total);
    snprintf(out, n, "%s  %d", ui_step_name(step), ui_percent(step, written, total, false));
    strncat(out, " %", n - strlen(out) - 1);               // not through the format: the firmware's printf has no %%
    size_t l = strlen(out);
    snprintf(out + l, n - l, "  (%s of %s)", a, b);
}
