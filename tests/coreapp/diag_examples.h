// The diag core's example tests as the application installs them: record 8 of diag.cor, made by the real packer (tools/cor_pack.py files-record, run.sh
// puts it in $DIAG_FILES_REC), written by the application's own installer (coreapp/src/instfiles.cc) onto a fake card.
#ifndef TESTS_COREAPP_DIAG_EXAMPLES_H
#define TESTS_COREAPP_DIAG_EXAMPLES_H

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "instfiles.h"

namespace {
class VecSrc : public CorSource {
public:
    explicit VecSrc(const std::vector<uint8_t> &v) : v_(v) { }
    uint32_t size() { return (uint32_t)v_.size(); }
    bool read(uint32_t off, void *buf, uint32_t len) { if ((uint64_t)off + len > v_.size()) return false; memcpy(buf, v_.data() + off, len); return true; }
private:
    const std::vector<uint8_t> &v_;
};
}

static inline std::vector<uint8_t> diag_record()
{
    const char *p = getenv("DIAG_FILES_REC");
    std::ifstream f(p ? p : "", std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static inline int diag_install_examples(CoreFs *fs, const char *folder)
{
    std::vector<uint8_t> rec = diag_record();
    VecSrc src(rec);
    return instfiles_install(fs, src, 0, (uint32_t)rec.size(), folder);
}

#endif
