// The applications a core has on its medium (T104.3 D6, T104 D9.4): "<core folder>/apps/*.u2a", where the core folder is the one beside the .cor (T148:
// "/SD/cores/trx64u/" for "/SD/cores/trx64u.cor") or, for a start without a .cor (JTAG, ue2emu), "<root>/cores/<core name>/apps" on each root. After them
// the folder every core reads (T180 D9, APP_SHARED_FOLDER): "<dir of the .cor>/apps" ("/SD/cores/apps"), else "<root>/cores/apps". What is
// used now and then - the cartridge tools, the monitor, Unzip - is not linked into the application; it is a file here and is loaded when it is opened.
//
// The index reads only what the menu needs, from the header and the menu record of each file, and loads nothing: the image comes into memory when the
// application is opened (apphost.h). The menu record is the application's declaration (T104.3 D0): the entries it puts into the core's menu (file
// actions "Write to Cartridge" on .crt, nodes in the Tools area) and the screens it brings (a route of its own, "APP", with its tabs and the nodes
// anchored "APP/<tab>[/<group>]"). The two halves are told apart here; the first joins the menu merge (contrib.h) like any contributor's, the second is
// merged into a menu of its own when the application is opened.
#ifndef COREAPP_APPINDEX_H
#define COREAPP_APPINDEX_H

#include "ctx.h"
#include "contrib.h"
#include "descrec.h"
#include "libload.h"
#include "libscan.h"

#define APP_FOLDER "apps"
#define APPS_MAX 8
#define APP_SHARED_FOLDER "apps"          // T180 D9: "<dir of the .cor>/apps" (else "<root>/cores/apps"), read by every core after its own folder; the first of a name counts, so the core's own wins
#define APP_FOLDERS_MAX 12                // the core's own folders and the shared ones: one of each per root
#define APP_PATH_MAX 200
#define APP_AREA "APP"                    // the key of the area an application's own route is made of

struct AppEntry {
    char path[APP_PATH_MAX];
    char name[U2A3_NAME_MAX];
    uint32_t version;
    uint8_t residence;                    // U2A3_RES_*
    uint32_t fsize;
    DescRecord *menu;                     // the menu record the file carries; null: none
    uint8_t *menu_bytes;
    LibRecord host;                       // what joins the core's menu: file actions, settings and the nodes anchored in the core's own route
    LibRecord screens;                    // the application's own nodes, anchored in "APP"
    LibNodeDecl *nodes;                   // both halves of the node list, host first
    bool has_screens;                     // the record gives the application a route of its own
};

class AppIndex {
public:
    AppIndex() : n_(0), sig_(0), has_extra_(false) { memset(app_, 0, sizeof(app_)); memset(&extra_, 0, sizeof(extra_)); }
    ~AppIndex() { clear(); }

    // The folders of this core's apps on the media: from the .cor path, else one per root. Returns how many (at most `max`).
    static int folders(const CoreCtx *c, char out[][APP_PATH_MAX], int max);
    // The names and sizes of the *.u2a files in the folders, folded into one number: a stick that came, a file that was copied.
    static uint32_t signature(CoreFs *fs, char folders[][APP_PATH_MAX], int nf);

    // Reads the files again (the old entries go). Each refusal is a line in `log`: a file that is not an application, a menu record that is damaged, a name
    // that two files carry. Never fails as a whole.
    void scan(CoreFs *fs, char folders[][APP_PATH_MAX], int nf, LibLog *log);
    void clear();

    int count() const { return n_; }
    const AppEntry *at(int i) const { return i >= 0 && i < n_ ? &app_[i] : 0; }
    const AppEntry *find(const char *name) const;
    // An application file from anywhere on the media (opened in the Library): read, and kept as an entry of its own until the next scan. Null: it is not usable (log).
    const AppEntry *adhoc(CoreFs *fs, const char *path, LibLog *log);
    uint32_t signature() const { return sig_; }

    // The contributors for the menu merge: one per application whose record has something for the core's menu (rank CR_APP, by name).
    int contributors(Contributor *out, int max) const;

private:
    bool read_file(CoreFs *fs, const char *path, AppEntry &a, LibLog *log);
    AppEntry app_[APPS_MAX];
    int n_;
    uint32_t sig_;
    AppEntry extra_;
    bool has_extra_;
};

// A LibSource over a CoreFile (the file stays open as long as the source is used).
class CoreFileSource : public LibSource {
public:
    explicit CoreFileSource(CoreFile *f) : f_(f) { }
    uint32_t size() { return (uint32_t)f_->size(); }
    bool read(uint32_t offset, void *dst, uint32_t n) { return f_->read(offset, dst, n); }

private:
    CoreFile *f_;
};

#endif
