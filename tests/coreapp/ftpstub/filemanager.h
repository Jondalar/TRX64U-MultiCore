// Test stand-in for the firmware's File (T186): a memory file with the calls ftp_client.cc makes.
#ifndef FTPSTUB_FILEMANAGER_H
#define FTPSTUB_FILEMANAGER_H
#include <stdint.h>
#include <string.h>
#include <string>
typedef int FRESULT;
#define FR_OK 0
struct File {
    std::string data;
    size_t pos;
    bool fail_read;
    File() : pos(0), fail_read(false) {}
    FRESULT write(const void *b, uint32_t n, uint32_t *w) { data.append((const char *)b, n); *w = n; return FR_OK; }
    FRESULT read(void *b, uint32_t n, uint32_t *r) {
        if (fail_read) return 1;
        size_t k = data.size() - pos; if (k > n) k = n;
        memcpy(b, data.data() + pos, k); pos += k; *r = (uint32_t)k; return FR_OK;
    }
    FRESULT seek(uint32_t p) { pos = p; return FR_OK; }
};
#endif
