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
#define LIBS_MAX 4                        // T197: libraries found in the same folders (miniz), kept as a name, a version, a path and a size
#define APP_WORK_BYTES (64u * 1024u)      // T197 P4: what a running application needs from the heap beside its image (miniz's inflate state and window, a zip directory, buffers)
#define PRG_APPS_MAX 16                   // T228 R21 #14: the <ext>.prg files of the flash's apps folder that the library offers as "Run with App" (FW user_file_interaction.cc:55-76)
#define PRG_APP_OP "files.run_with_app"   // the operation a core registers to take them; a core without it gets no such action
#define PRG_APP_NEED "standalone"         // an application that names this need is offered only when the board is not the MultiCore host (T228 DC1); it is not a library
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
    bool standalone_only;                 // PRG_APP_NEED
    LibRecord screens;                    // the application's own nodes, anchored in "APP"
    LibNodeDecl *nodes;                   // both halves of the node list, host first
    bool has_screens;                     // the record gives the application a route of its own
    uint32_t image_bytes;                 // T197: what the image takes from the heap (image + bss + alignment)
    uint8_t nneeds;                       // the libraries it needs (not "base"), by name and minimum version
    U2a3Need needs[U2A3_NEEDS_MAX];
};

// A library file of the apps folders (T197): not an application, so no entry in the menu; applications that need it load it from here.
struct LibFile {
    char path[APP_PATH_MAX];
    char name[U2A3_NAME_MAX];
    uint32_t version;
    uint32_t image_bytes;
};

class AppIndex {
public:
    AppIndex() : nlibs_(0), room_(0xFFFFFFFFu), room_ld_(0), n_(0), sig_(0), has_extra_(false) { memset(app_, 0, sizeof(app_)); memset(&extra_, 0, sizeof(extra_)); memset(libs_, 0, sizeof(libs_)); }
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

    // ---- libraries of the apps folders (T197)
    int nlibs() const { return nlibs_; }
    const LibFile *find_lib(const char *name, uint32_t min_version = 0) const;
    // Does an application fit the loader window (the heap the board can give, `room`) with the libraries it needs that `loaded` does not have yet, plus
    // APP_WORK_BYTES? Missing or too old a library: no. `ld` (may be null) says which libraries are in memory already.
    bool fits(const AppEntry &a, uint32_t room, const LibLoader *ld) const;
    // Are the application and every library it needs there (loaded in `ld`, or a file in the folders, new enough)? The menu lists the actions of such an
    // application whatever the room is (D9); the memory is checked at the press, by prepare_open.
    bool available(const AppEntry &a, const LibLoader *ld) const;
    // The bytes `a` takes from the heap now: image + APP_WORK_BYTES + the libraries not in memory. False: a library is missing or too old (*need is not set).
    bool need_bytes(const AppEntry &a, const LibLoader *ld, uint64_t *need) const;
    // The room and loader the menu merge sees (D9: only the loader matters now, for which libraries are in memory; the room no longer hides anything).
    void set_room(uint32_t room, const LibLoader *ld) { room_ = room; room_ld_ = ld; }
    // Loads the libraries `a` needs that are not in memory (the base ones excepted), in the order it lists them; false and a sentence in err if one is missing,
    // too old or refused. A library stays loaded (libraries are resident).
    bool load_needs(CoreFs *fs, LibLoader *ld, const AppEntry &a, char *err, int errlen) const;
    // T197 D8, at the press: when `a` does not fit `ld->room()`, every loaded library nothing uses (no loaded application, no loaded library; LibLoader::use) and `a` does not need is unloaded, then the
    // room is asked again; still too small: false and "Not enough memory for <app> (needs N KB, M KB free)." in err. Then load_needs.
    bool prepare_open(CoreFs *fs, LibLoader *ld, const AppEntry &a, char *err, int errlen) const;

    // The contributors for the menu merge: one per application whose record has something for the core's menu (rank CR_APP, by name).
    // At most APPS_MAX + 1 contributors (the applications, then the flash's .prg apps).
    int contributors(Contributor *out, int max) const;
    // T228 DC1: under the MultiCore host the applications that need PRG_APP_NEED are not offered. Set before scan().
    void set_multicore(bool mc) { multicore_ = mc; }
    // T228 R21 #14: scan() lists <flash>/apps/*.prg and offers each as "Run with App" for the files of its extension. Set before scan(), and only when the core registered PRG_APP_OP.
    void set_prg_apps(bool on) { prg_on_ = on; }
    int nprg() const { return nprg_; }

private:
    bool read_file(CoreFs *fs, const char *path, AppEntry &a, LibLog *log, LibFile *lib = 0);
    LibFile libs_[LIBS_MAX];
    int nlibs_;
    uint32_t room_;
    const LibLoader *room_ld_;
    AppEntry app_[APPS_MAX];
    int n_;
    uint32_t sig_;
    AppEntry extra_;
    bool has_extra_;
    bool multicore_ = false;
    bool prg_on_ = false;
    void scan_prg(CoreFs *fs, FsEntry *e);
    int nprg_ = 0;
    char prg_ext_[PRG_APPS_MAX][8];
    const char *prg_extp_[PRG_APPS_MAX];
    OpBind prg_bind_[1];
    LibFileActionDecl prg_fa_[PRG_APPS_MAX];
    LibRecord prg_rec_;
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
