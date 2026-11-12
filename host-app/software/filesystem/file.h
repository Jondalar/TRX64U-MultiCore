#ifndef FILE_H
#define FILE_H

#include "integer.h"
#include "path.h"
#include "file_system.h"

/*
 * The File class is a wrapper to hide the actual implementation
 * of a stream. Right now, the diversification is handled by
 * derivates of the FileSystem class, but future specials
 * could handle direct file implementations as well. However,
 * one thing needs to be noted: the 'node' field should always
 * refer to a valid object. If this variable is set to NULL,
 * the file is invalid. This can be caused by a tree cleanup,
 * at the removal of a media.
 */
#define COPY_INFO 1

class File
{
    FileSystem *filesystem;
    mstring pathString;
    bool write_intent; // opened for writing, so its size is only final on close

	// the following function shall only be called by the file manager
	friend class FileManager;
public:
    File(FileSystem *fs) { filesystem = fs; write_intent = false; }
    virtual ~File() { }

    FileSystem *get_file_system() { return filesystem; }
    bool isValid(void) { return (filesystem != NULL); }
    void invalidate(void) { filesystem = NULL; }

    const char *get_path() {
    	return pathString.c_str();
    }
    mstring &get_path_reference() {
    	return pathString;
    }

    // functions for reading and writing files
    virtual FRESULT close(void) { delete this; return FR_NO_FILESYSTEM; }
    virtual FRESULT sync(void) { return FR_NO_FILESYSTEM; }
    virtual FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) { return FR_NO_FILESYSTEM; }
    virtual FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) { return FR_NO_FILESYSTEM; }
    virtual FRESULT seek(uint64_t pos) { return FR_NO_FILESYSTEM; }
    virtual uint32_t get_inode() { return 0; }
    virtual uint64_t get_size(void) { return 0; }
    // The same as seek and get_size, under the names the Amiga application (T102) uses.
    // For the code that holds a size in 32 bits (a file read into RAM, a header field): a file of 4 GiB and more reads as 0xFFFFFFFF,
    // so a size limit on it still refuses the file, where a truncated value would let a small number through.
    uint32_t get_size32(void) { uint64_t s = get_size(); return (s > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)s; }
    FRESULT seek64(uint64_t pos) { return seek(pos); }
    uint64_t get_size64(void) { return get_size(); }
};

#endif
