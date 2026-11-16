// Settings of a core application: three layers and one place to write (T93 D10, T84 D11).
//
//   value = the core layer (<name>.cfg beside the .cor) if the item is set there,
//           else the host layer (the flash settings, read only),
//           else the descriptor's default.
//   Every change goes into the .cfg at once (Q3); nothing here can write the flash - FlashLayer has no write method, and
//   tests/coreapp/ counts the flash accesses of a whole session.
// A medium that cannot be written is reported once; the settings keep working for the session. Pure.
#ifndef COREAPP_SETTINGS_H
#define COREAPP_SETTINGS_H

#include <stdint.h>
#include "desc.h"

// The host's settings, as the flash holds them. Read only on purpose. get() fills val with the item's label and returns
// true when the flash has a value for [store] key.
class FlashLayer {
public:
    virtual ~FlashLayer() { }
    virtual bool get(const char *store, const char *key, char *val, int n) = 0;
};

#define SETTINGS_MAX 340                 // the descriptor's own, the application's Picture and what libraries contribute (T104.1 D5)

class Settings {
public:
    enum Layer { L_DEFAULT = 0, L_HOST, L_CORE };

    // items/n/store from the descriptor. cfg_path may be "" (no hand-over): the settings then live in memory only.
    Settings(const Setting *items, int n, const char *store, CoreFs *fs, const char *cfg_path, FlashLayer *flash);

    // Reads the flash layer, then the .cfg. A missing .cfg is the normal case; an unreadable one is told once.
    void load();

    int count() const { return n_; }
    const Setting &item(int i) const { return items_[i]; }
    int value(int i) const;
    Layer layer(int i) const;
    const char *label(int i) const { return items_[i].values[value(i)]; }
    int find(const char *key) const;

    // Sets the core layer and writes the .cfg. Returns false when the file could not be written (the value holds anyway).
    bool set(int i, int v);

    // A text setting (Setting::kind 1 or 2): the text in force (the core layer, else the default), and a new one. A text setting has no value index.
    const char *text(int i) const;
    bool set_text(int i, const char *s);

    // T164 G12: the labels of item i changed (the Picture list follows the screen). The stored indexes of the host and core layers are looked up again
    // by their old label in the new list; a label the new list lacks falls back to the next layer. Nothing is written.
    void remap(int i, const char *const *old_values, int old_n);

    // Removes the core layer: deletes the .cfg (or, when it cannot be deleted, empties it). The values fall back to the host
    // layer and then to the defaults. Returns false when the file could not be removed.
    bool reset_to_host();

    // Auto Save Config (T104.2 UI, Gideon's item): 0 Off and 1 Ask keep a change in memory until save() writes the .cfg, 2 Auto writes at once (the default).
    void set_autosave(int mode) { autosave_ = mode; }
    int autosave() const { return autosave_; }
    bool dirty() const { return dirty_; }
    bool item_held(int i) const { return i >= 0 && i < n_ && held_[i]; }   // T192.3 S11: this setting was changed and waits for save()
    bool save();                                      // writes the .cfg now; false when it could not be written
    // T224 D2/D3: a value in force for this run only. value() answers it; no file (the .cfg, a config exported by "Save as config") is ever written with it, they get the stored value
    // (the core layer, else the host layer, else the default). set() on the same item ends the override: what the person chooses is stored as always. load() and rebind() drop all.
    void override_value(int i, int v);
    // T156: keys of the core's own that are not choices from a list (the recorded pad maps, "Pad 1 map=..."). Only keys that start with
    // "Pad " are kept; they are written with the settings and removed with them.
    bool extra_get(const char *key, char *val, int n) const;
    bool extra_set(const char *key, const char *val);
    // T180 D2: the media that are mounted, one key per slot ("Mount DF0=/usb0/WB32/Install.adf"). They are not settings (no descriptor item): the file holds
    // them beside the items and the application writes and reads them (mediamounts.h). mount_put writes the .cfg at once - a mount is stored when it happens,
    // whatever Auto Save Config says - and with it any other change that is held back. A null `val` removes the key. False: no room, a value that does not fit
    // a line of the file, or the file could not be written (the value holds in memory).
    enum { MOUNT_MAX = 12, MOUNT_KEY_LEN = 16, MOUNT_VAL_LEN = 200 };
    bool mount_get(const char *key, char *val, int n) const;
    bool mount_put(const char *key, const char *val);
    // Removes every stored mount; writes the file when there was one.
    bool mounts_clear();

    // T198: a config is a full copy of the settings and the mounts in the text format of the .cfg. export_config writes every item at the value in force and the given mounts
    // ("Mount DF0" -> spec; null/0: the ones the settings keep) to `path`, never to the settings' own file; false when it could not be written. *nsettings: the items written.
    bool export_config(const char *path, const char *const *mk, const char *const *mv, int nm, int *nsettings) const;
    // T198 D4, D5: the settings now live in another file (a config): the core layer, the extras and the mounts are read from it and every later change is written to it. The
    // file the settings had before is not touched. A missing file leaves everything at the host layer and the defaults.
    void rebind(const char *path);
    const char *path() const { return cfg_path_; }

    // The one line the screen shows when saving failed, once; "" afterwards (until the next failure after a success).
    const char *take_notice();
    bool saving_possible() const { return cfg_path_[0] != 0 && !save_failed_; }
    bool has_path() const { return cfg_path_[0] != 0; }

private:
    const Setting *items_;
    int n_;
    const char *store_;
    CoreFs *fs_;
    char cfg_path_[256];
    FlashLayer *flash_;
    int16_t host_[SETTINGS_MAX];      // -1: not set in the flash
    int16_t core_[SETTINGS_MAX];      // -1: not set in the .cfg
    int16_t ovr_[SETTINGS_MAX];       // -1: none; else the value this run uses instead (override_value)
    enum { TEXT_N = 120, TEXT_LEN = 128 };
    int8_t tix_[SETTINGS_MAX];        // a text setting's place in tpool_, -1 none
    char tpool_[TEXT_N][TEXT_LEN];    // the texts the core layer holds (T104.2 UI)
    int ntext_;
    enum { HTEXT_N = 8 };
    int8_t hix_[SETTINGS_MAX];        // T212 D2/S9: a text setting's host value, its place in hpool_, -1 none
    char hpool_[HTEXT_N][TEXT_LEN];
    int autosave_;
    bool dirty_;
    bool held_[SETTINGS_MAX];         // per setting: changed, not written yet (Auto Save Config Off or Ask)
    enum { EXTRA_MAX = 4 };
    struct Extra { char key[24]; char val[80]; bool used; } extra_[EXTRA_MAX];
    struct Mount { char key[MOUNT_KEY_LEN]; char val[MOUNT_VAL_LEN]; bool used; } mnt_[MOUNT_MAX];
    bool save_failed_;
    bool notice_pending_;
    char notice_[160];

    bool write_cfg();
    bool build(char *buf, int max, int *pos, bool all, const char *const *mk, const char *const *mv, int nm) const;
    bool extra_set_mem(const char *key, const char *val);
    bool mount_set_mem(const char *key, const char *val);
    void fail(const char *what);
    static void on_item(void *user, const char *store, const char *key, const char *value);
    int label_index(int i, const char *label) const;
    int stored_value(int i) const;    // value() without the override
};

#endif
