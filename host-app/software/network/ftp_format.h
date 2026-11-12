#ifndef FTP_FORMAT_H
#define FTP_FORMAT_H

// The lines the FTP server sends that carry a file size. Sizes are 64 bit (exFAT files of 4 GiB and more) and the firmware's printf
// has no 64 bit conversion, so they go through u64_dec. In a header of their own so that tests/ can build the very same lines.

#include <stdio.h>
#include <stdint.h>
#include "u64_str.h"
#include "vfs.h"

// "213 <size>" (reply to SIZE)
static inline int ftp_fmt_size_reply(char *buf, uint64_t size)
{
    char sz[21];
    return sprintf(buf, "213 %s", u64_dec(size, sz));
}

// One line of LIST in the Unix style. with_time: the entry is of this year, so the time of day is given, else the year.
static inline int ftp_fmt_list_line(char *buf, const vfs_stat_t *st, const char *month, bool with_time)
{
    char sz[21];
    int len;
    if (with_time) {
        len = sprintf(buf, "-rw-rw-rw-   1 user     ftp  %s %s %02d %02d:%02d %s\r\n", u64_dec_pad(st->st_size, sz, 11), month, st->day,
                st->hr, st->min, st->name);
    } else {
        len = sprintf(buf, "-rw-rw-rw-   1 user     ftp  %s %s %02d %5d %s\r\n", u64_dec_pad(st->st_size, sz, 11), month, st->day,
                st->year, st->name);
    }
    if (VFS_ISDIR(st->st_mode)) {
        buf[0] = 'd';
    }
    return len;
}

// One line of MLSD
static inline int ftp_fmt_mlsd_line(char *buf, const vfs_stat_t *st)
{
    char sz[21];
    return sprintf(buf, "type=%s;size=%s;modify=%04d%02d%02d%02d%02d%02d; %s\r\n", VFS_ISDIR(st->st_mode) ? "dir" : "file",
            u64_dec(st->st_size, sz), st->year, st->month, st->day, st->hr, st->min, st->sec, st->name);
}

// The reply to MLST of a file
static inline int ftp_fmt_mlst_file(char *buf, size_t n, const vfs_stat_t *st, const char *type)
{
    char sz[21];
    return snprintf(buf, n, "250- Listing %s\r\ntype=%s;size=%s;modify=%04d%02d%02d%02d%02d%02d; %s\r\n250 End", st->name, type,
            u64_dec(st->st_size, sz), st->year, st->month, st->day, st->hr, st->min, st->sec, st->name);
}

#endif
