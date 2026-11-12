/* miniz_export.h - configuration for the unzip app */
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

/* The app only reads archives, through callbacks onto the firmware's file
   calls, and allocates through the firmware: no stdio, no clock, no
   compression, no zlib API, no malloc of its own. */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC

/* Apps have no assert(); a broken archive surfaces as a failed call. */
#define MZ_ASSERT(x) ((void)0)

#endif /* MINIZ_EXPORT_H */
