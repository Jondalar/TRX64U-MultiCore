// A FakeFs whose big files are not in memory (T104.1 D16): the name is listed, the size is a number, the bytes written are kept by offset.
// For the benches that need an image of 4 GiB and more behind CoreFs (test_fs64.cc, test_sectors.cc).
#ifndef TESTS_COREAPP_SPARSEFS_H
#define TESTS_COREAPP_SPARSEFS_H

#include <map>
#include <string>
#include <vector>
#include "fakes.h"

static const uint64_t GiB = 1024ull * 1024ull * 1024ull;
static const uint64_t FOUR_GIB = 4 * GiB;

class SparseFs;

class SparseFile : public CoreFile {
    SparseFs *fs_;
    std::string key_;
public:
    SparseFile(SparseFs *fs, const std::string &key) : fs_(fs), key_(key) { }
    uint64_t size();
    bool writable() { return true; }
    bool read(uint64_t off, void *buf, uint32_t len);
    bool write(uint64_t off, const void *buf, uint32_t len);
    bool sync();
};

// A FakeFs whose big files are not in memory: the name is listed, the size is a number, the bytes written are kept by offset.
class SparseFs : public FakeFs {
public:
    int file_syncs = 0;
    std::map<std::string, uint64_t> sizes;
    std::map<std::string, std::map<uint64_t, uint8_t>> bytes;

    void put_big(const std::string &path, uint64_t size)
    {
        put(path, std::vector<uint8_t>(1, 0));           // the name for the listing
        sizes[lower(path)] = size;
    }
    int list(const char *dir, FsEntry *out, int max)
    {
        int n = FakeFs::list(dir, out, max);
        std::string prefix = lower(dir);
        if (prefix.empty() || prefix.back() != '/') {
            prefix += '/';
        }
        for (int i = 0; i < n; i++) {
            auto it = sizes.find(prefix + lower(out[i].name));
            if (it != sizes.end()) {
                out[i].size = it->second;
            }
        }
        return n;
    }
    bool stat(const char *path, uint64_t *size, bool *dir)
    {
        bool ok = FakeFs::stat(path, size, dir);
        auto it = sizes.find(lower(path));
        if (ok && it != sizes.end() && size) {
            *size = it->second;
        }
        return ok;
    }
    CoreFile *open(const char *path, bool rw)
    {
        std::string k = lower(path);
        if (sizes.count(k)) {
            return new SparseFile(this, k);
        }
        return FakeFs::open(path, rw);
    }
};

inline bool SparseFile::sync() { fs_->file_syncs++; return true; }
inline uint64_t SparseFile::size() { return fs_->sizes[key_]; }
inline bool SparseFile::read(uint64_t off, void *buf, uint32_t len)
{
    uint64_t size = fs_->sizes[key_];
    if (off > size || len > size - off) {
        return false;
    }
    auto &b = fs_->bytes[key_];
    for (uint32_t i = 0; i < len; i++) {
        auto it = b.find(off + i);
        ((uint8_t *)buf)[i] = it == b.end() ? 0 : it->second;
    }
    return true;
}
inline bool SparseFile::write(uint64_t off, const void *buf, uint32_t len)
{
    uint64_t size = fs_->sizes[key_];
    if (off > size || len > size - off) {
        return false;
    }
    for (uint32_t i = 0; i < len; i++) {
        fs_->bytes[key_][off + i] = ((const uint8_t *)buf)[i];
    }
    return true;
}


#endif
