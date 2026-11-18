#include "fsif.h"

// The bench builds the board's CoreFs without the media level (fm_medium.cc needs the host-app headers and FreeRTOS); no scenario asks for it.
bool fm_medium_info(const char *, MediumInfo *) { return false; }
bool fm_path_writable(const char *path) { return !fs_write_refused(path); }
