// T198: the names of a core's configs - the one place that decides what is a config name, which files in a core folder are configs, and in what order they
// are shown. Pure code. The host app (listing, overlay, manage) and the core application (save, load, list) both include it, so the two lists are the same list
// (S2, S10), and tests/hostapp/ and tests/coreapp/ run it natively.
//
// A config is a file "<core folder>/configs/<name>.cfg" in the text format of the core's own .cfg (the settings and the "Mount <slot>=..." keys). A media set of
// before T198 (T180 D5, "<core folder>/media/<name>.mnt") is listed as a config too (D3): the name is the same, it is flagged `legacy`, and a config of the same name
// in configs/ hides it.
#ifndef HOST_CONFIGLIST_H
#define HOST_CONFIGLIST_H

#include <stdint.h>
#include <string.h>

#define CONFIG_NAME_MAX 32          // D11: a name of up to 32 characters (it travels in the hand-over page's config[48])
#define CONFIGS_MAX 16              // D11: at most 16 configs per core
#define CONFIG_DIR "configs"
#define CONFIG_EXT ".cfg"
#define CONFIG_LEGACY_DIR "media"
#define CONFIG_LEGACY_EXT ".mnt"
#define CONFIG_FIX_MAX 8            // actions of one recovery pass

// A usable name: 1..CONFIG_NAME_MAX printable ASCII characters, none of / \ : * ? " < > |, no leading dot, no trailing space (it is a file name on FAT).
// The reason a person reads when it is not ("A config name has up to 32 characters.", ...); "" when the name is fine.
// Inline: boot.cfg's reader (bootcfg.cc) validates the default config's name without linking the list code.
static inline const char *config_name_why(const char *name)
{
    int l = name ? (int)strlen(name) : 0;
    if (l == 0) {
        return "Give the config a name.";
    }
    if (l > CONFIG_NAME_MAX) {
        return "A config name has up to 32 characters.";
    }
    if (name[0] == '.' || name[l - 1] == ' ') {
        return "A config name cannot start with a dot or end with a space.";
    }
    for (int i = 0; i < l; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || ch > 0x7E || strchr("/\\:*?\"<>|", ch)) {
            return "A config name cannot hold / \\ : * ? \" < > | or other special characters.";
        }
    }
    return "";
}
static inline bool config_name_ok(const char *name) { return name && config_name_why(name)[0] == 0; }

struct ConfigList {
    int n;                                           // the configs found, at most CONFIGS_MAX
    int more;                                        // files that were configs but did not fit the limit
    char name[CONFIGS_MAX][CONFIG_NAME_MAX + 1];     // sorted case-insensitively
    bool legacy[CONFIGS_MAX];                        // the file is a media set of before T198 (media/<name>.mnt)
};
void configlist_clear(ConfigList *l);
// One directory entry of configs/ (legacy false) or of media/ (legacy true). Foreign files, folders and names config_name_ok refuses are not configs and are skipped;
// a name that is there already (the same name in configs/ and media/) keeps the configs/ one. True when the entry was taken.
bool configlist_add(ConfigList *l, const char *file, bool is_dir, bool legacy);
// Sorts the list (call after the last configlist_add).
void configlist_sort(ConfigList *l);
int configlist_find(const ConfigList *l, const char *name);   // the index, -1

// Recovery of an interrupted save (config_save swaps rename X.cfg -> X.bak, rename X.tmp -> X.cfg, remove X.bak). Given the file names of configs/, says what to do:
// an X.bak without an X.cfg is the old config (power loss between the two renames): rename it to X.cfg; an X.bak beside an X.cfg is a leftover: remove it;
// every X.tmp is a leftover (possibly half written): remove it. Only names config_name_ok accepts are touched. Pure; the caller applies it with its file system.
struct ConfigFix {
    int n;
    bool more;                                       // the plan was cut at CONFIG_FIX_MAX (the next listing continues)
    struct Act {
        bool recover;                                // true: rename `file` to `to`; false: remove `file`
        char file[CONFIG_NAME_MAX + 5];
        char to[CONFIG_NAME_MAX + 5];
    } act[CONFIG_FIX_MAX];
};
// get(ctx, i, &is_dir) returns the i-th file name of the listing.
void configlist_fix_plan(ConfigFix *f, int n, const char *(*get)(void *ctx, int i, bool *is_dir), void *ctx);

// "<dir>/<name>.cor" -> "<dir>/<name>" (the core folder, T148 D1). False when the path is not an absolute path of a .cor with a folder below a medium, or does not fit.
bool config_core_dir(const char *cor_path, char *out, int outlen);
// "<core dir>/configs", "<core dir>/media".
bool config_dir_path(const char *core_dir, bool legacy, char *out, int outlen);
// "<core dir>/configs/<name>.cfg", or for a legacy one "<core dir>/media/<name>.mnt".
bool config_file_path(const char *core_dir, const char *name, bool legacy, char *out, int outlen);

#endif
