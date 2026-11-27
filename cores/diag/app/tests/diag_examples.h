// The diag core's shipped tests and presets as the lane's delivery carries them (T260 D6): the folder cores/diag/sd of the repository, copied onto a fake card (the tester's FTP copy to
// /SD/cores/diag). The tests run in the repository root, so the folder is read by its relative path.
#ifndef TESTS_COREAPP_DIAG_EXAMPLES_H
#define TESTS_COREAPP_DIAG_EXAMPLES_H

#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>

// Every file of `dir` (recursive) to `folder` on the card; the number of files written.
static inline int diag_put_dir(CoreFs *fs, const std::string &dir, const std::string &folder)
{
    int n = 0;
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return 0;
    }
    while (dirent *e = readdir(d)) {
        std::string nm = e->d_name;
        if (nm == "." || nm == "..") {
            continue;
        }
        std::string src = dir + "/" + nm, dst = folder + "/" + nm;
        if (e->d_type == DT_DIR) {
            fs->mkdir(dst.c_str());
            n += diag_put_dir(fs, src, dst);
        } else {
            std::ifstream f(src, std::ios::binary);
            std::stringstream s;
            s << f.rdbuf();
            std::string t = s.str();
            n += fs->save(dst.c_str(), t.data(), (uint32_t)t.size()) ? 1 : 0;
        }
    }
    closedir(d);
    return n;
}

static inline int diag_install_examples(CoreFs *fs, const char *folder)
{
    fs->mkdir(folder);
    return diag_put_dir(fs, "cores/diag/sd", folder);
}

#endif
