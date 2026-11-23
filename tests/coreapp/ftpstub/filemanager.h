// Test stand-in for the firmware's File (T186): a memory file with the calls ftp_client.cc makes. The calls are virtual as the firmware's are, so that a file of the
// application (the RAM file of ftp_exports.cc, which the client writes into and reads from) is what the client reaches through its File pointer.
#ifndef FTPSTUB_FILEMANAGER_H
#define FTPSTUB_FILEMANAGER_H
#include <stdint.h>
#include <string.h>
#include <string>
typedef int FRESULT;
#define FR_OK 0
#define FR_DENIED 7
#define FR_NOT_ENOUGH_CORE 17
struct FileSystem;
struct File {
    std::string data;
    size_t pos;
    bool fail_read;
    File(FileSystem * = 0) : pos(0), fail_read(false) {}
    virtual ~File() {}
    virtual FRESULT close(void) { return FR_OK; }
    virtual FRESULT sync(void) { return FR_OK; }
    virtual FRESULT write(const void *b, uint32_t n, uint32_t *w) { data.append((const char *)b, n); *w = n; return FR_OK; }
    virtual FRESULT read(void *b, uint32_t n, uint32_t *r) {
        if (fail_read) return 1;
        size_t k = data.size() - pos; if (k > n) k = n;
        memcpy(b, data.data() + pos, k); pos += k; *r = (uint32_t)k; return FR_OK;
    }
    virtual FRESULT seek(uint64_t p) { pos = (size_t)p; return FR_OK; }
    virtual uint64_t get_size(void) { return data.size(); }
};
#endif
