// bench stand-in for the FileManager (T163): config.cc only reaches it in the original's page-file fallback, which a host backend never takes.
// The test counts the calls; a backend that touched it would show.
#pragma once
#include "file.h"
class FileManager
{
public:
    static int calls;
    static FileManager *getFileManager() { static FileManager fm; return &fm; }
    FRESULT create_dir(const char *) { calls++; return FR_OK; }
    FRESULT fopen(const char *, const char *, uint8_t, File **) { calls++; return FR_NO_FILE; }
    FRESULT fclose(File *) { calls++; return FR_OK; }
};
