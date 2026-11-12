// T198 (S2, S3, S6, S9, S12, S15): the config overlay of the host over a fake file system. The listing is configlist.cc (the core app's own), the state is
// host_core_cfgov.cc; the default config lives in the boot.cfg entry (bootcfg.cc, host_core.cc). No negative control: not a flash operation of ours (T174 D5.4);
// the delete and the rename act on the person's own config files only.
#include "host_core.h"
#include "host_core_cfgov.h"
#include "keyboard.h"
#include "host_core_flow_wifi.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include "bootcfg.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Fake : CmFs, FoFs {
    std::set<std::string> files;
    int lists = 0;
    int list(const char *dir, CmEntry *out, int max) override
    {
        lists++;
        std::string d = std::string(dir) + "/";
        int n = 0;
        for (const auto &f : files) {
            if (f.compare(0, d.size(), d) == 0 && f.find('/', d.size()) == std::string::npos && n < max) {
                snprintf(out[n].name, sizeof(out[n].name), "%s", f.c_str() + d.size());
                out[n].dir = false;
                out[n].size = 10;
                n++;
            }
        }
        return n;
    }
    bool core_name(const char *, char *, int) override { return false; }
    bool stat(const char *p, FoStat *st) override { if (!files.count(p)) return false; st->dir = false; st->size = 10; return true; }
    int dir_entries(const char *) override { return 0; }
    bool free_bytes(const char *, uint64_t *b) override { *b = 1 << 30; return true; }
    bool same_volume(const char *, const char *) override { return true; }
    void *open_read(const char *) override { return 0; }
    void *open_write(const char *) override { return 0; }
    int read(void *, void *, uint32_t) override { return -1; }
    bool write(void *, const void *, uint32_t) override { return false; }
    bool close(void *) override { return false; }
    bool remove(const char *p) override { return files.erase(p) == 1; }
    bool rename(const char *a, const char *b) override { if (!files.count(a) || files.count(b)) return false; files.erase(a); files.insert(b); return true; }
};

static std::string g_saved;
static int g_saves;
static int p_load(char *, int) { return -1; }
static bool p_save(const char *t, int n) { g_saved.assign(t, n); g_saves++; return true; }
static void p_changed(void) { }

int main()
{
    static const HcPlatform plat = { p_load, p_save, p_changed, 0, 0, 0 };
    hc_init(&plat);
    Fake fk;
    hc_cfgov_io(&fk, &fk);
    hc_cfg_reset("countdown=0\ndefault=0\ncore=/SD/cores/amiga.cor|Amiga||0|0|00000000|Games\ncore=/SD/cores/vic.cor|VIC||0|0|00000000\n");
    CHECK(!strcmp(hc_cfg().e[0].defcfg, "Games") && !hc_cfg().e[1].defcfg[0]);       // D8: the seventh field of boot.cfg

    // S2: both folders, sorted, configs/ wins over media/; the badge text
    for (const char *n : { "Zed", "alpha", "Games", "Beta" }) fk.files.insert(std::string("/SD/cores/amiga/configs/") + n + ".cfg");
    fk.files.insert("/SD/cores/amiga/media/Beta.mnt");                               // the same name: hidden by configs/ (D3)
    fk.files.insert("/SD/cores/amiga/media/old.mnt");
    fk.files.insert("/SD/cores/amiga/configs/readme.txt");                           // not a config
    CHECK(hc_cfgov_count("/SD/cores/amiga.cor") == 5 && hc_cfgov_count("/SD/cores/vic.cor") == 0);
    char b[16];
    hc_cfgov_badge(5, b, sizeof(b)); CHECK(!strcmp(b, "5 configs"));
    hc_cfgov_badge(1, b, sizeof(b)); CHECK(!strcmp(b, "1 config"));
    hc_cfgov_badge(0, b, sizeof(b)); CHECK(b[0] == 0);

    // S15: 50 polls and many key presses read the folders once per generation
    hc_cfgov_forget();
    fk.lists = 0;
    for (int i = 0; i < 50; i++) { hc_cfgov_count("/SD/cores/amiga.cor"); hc_cfgov_count("/SD/cores/vic.cor"); }
    CHECK(fk.lists == 4);                                                            // 2 cores x 2 folders, once
    hc_cfgov_forget();
    hc_cfgov_count("/SD/cores/amiga.cor");
    CHECK(fk.lists == 6);

    // review 2026-11-11: a save of the core application interrupted between its two renames (X.bak + X.tmp, no X.cfg) is put right by the listing; leftovers go
    fk.files.insert("/SD/cores/amiga/configs/Lost.bak");
    fk.files.insert("/SD/cores/amiga/configs/Lost.tmp");
    fk.files.insert("/SD/cores/amiga/configs/Beta.bak");                             // stale, Beta.cfg exists
    hc_cfgov_forget();
    CHECK(hc_cfgov_count("/SD/cores/amiga.cor") == 6);
    CHECK(fk.files.count("/SD/cores/amiga/configs/Lost.cfg") && !fk.files.count("/SD/cores/amiga/configs/Lost.bak") && !fk.files.count("/SD/cores/amiga/configs/Lost.tmp"));
    CHECK(!fk.files.count("/SD/cores/amiga/configs/Beta.bak") && fk.files.count("/SD/cores/amiga/configs/Beta.cfg"));
    fk.files.erase("/SD/cores/amiga/configs/Lost.cfg");
    hc_cfgov_forget();

    // S3/S4/S5/S12: the keys of the menu and of the overlay, and the intents' stable ids (the intent resolves to the same action as the key)
    CHECK(hc_resolve(HCX_MENU, KEY_RIGHT) == HCA_CONFIG_LIST && hc_resolve(HCX_MENU, KEY_RETURN) == HCA_ENTRY_START);       // RETURN stays the plain start
    CHECK(hc_resolve(HCX_CFG, KEY_RETURN) == HCA_CONFIG_START && hc_resolve(HCX_CFG, KEY_LEFT) == HCA_BACK && hc_resolve(HCX_CFG, KEY_ESCAPE) == HCA_BACK);
    CHECK(hc_resolve(HCX_CFG, KEY_UP) == HCA_CURSOR_UP && hc_resolve(HCX_CFG, KEY_DOWN) == HCA_CURSOR_DOWN);
    CHECK(hc_resolve(HCX_CFG, 'd') == HCA_CONFIG_DEFAULT && hc_resolve(HCX_CFG, 'D') == HCA_CONFIG_DEFAULT && hc_resolve(HCX_CFG, 'r') == HCA_CONFIG_RENAME);
    CHECK(hc_resolve(HCX_CFG, KEY_DELETE) == HCA_CONFIG_DELETE && hc_resolve(HCX_CFG, KEY_BACK) == HCA_CONFIG_DELETE);
    CHECK(hc_act_by_name("config.list") == HCA_CONFIG_LIST && hc_act_by_name("config.start") == HCA_CONFIG_START && hc_act_by_name("config.rename") == HCA_CONFIG_RENAME);
    CHECK(hc_act_by_name("config.delete") == HCA_CONFIG_DELETE && hc_act_by_name("config.default") == HCA_CONFIG_DEFAULT);

    // D9: a core without configs: nothing opens
    CHECK(!hc_cfgov_open("/SD/cores/vic.cor", "VIC", 1) && !hc_cfgov_is_open());

    // D14/D11: opens on the default config; "Plain start" is row 0; the order is case-insensitive
    CHECK(hc_cfgov_open("/SD/cores/amiga.cor", "Amiga", 0));
    const HcCfgOv &o = hc_cfgov();
    CHECK(o.l.n == 5 && !strcmp(o.l.name[0], "alpha") && !strcmp(o.l.name[1], "Beta") && !strcmp(o.l.name[2], "Games") && !strcmp(o.l.name[3], "old") && !strcmp(o.l.name[4], "Zed"));
    CHECK(o.cur == 3 && !strcmp(hc_cfgov_cur_name(), "Games"));
    HcWifiPick pk;
    hc_cfgov_pick(&pk);
    CHECK(pk.n == 6 && !strcmp(pk.item[0].a, "Plain start") && !strcmp(pk.item[3].a, "Games") && !strcmp(pk.item[3].c, "default") && pk.sel == 3);
    CHECK(pk.cfgov && !pk.item[0].b[0] && !pk.item[3].b[0] && !strcmp(pk.sub, "5 configs"));
    hc_cfgov_close();
    CHECK(hc_cfgov_open("/SD/cores/amiga.cor", "Amiga", -1, true));
    hc_cfgov_pick(&pk);
    CHECK(!strcmp(pk.sub, "amiga.cor - 5 configs"));                              // from the File Browser the sub text names the file
    hc_cfgov_close();
    CHECK(hc_cfgov_open("/SD/cores/amiga.cor", "Amiga", 0));
    hc_cfgov_pick(&pk);
    CHECK(!strcmp(pk.title, "Amiga - start with") && !strcmp(pk.sub, "5 configs"));
    // board 11-11: a long entry name stays whole in the title (it read "Amiga 12 - start with")
    CHECK(hc_cfgov_open("/SD/cores/amiga.cor", "Amiga 1200 (AGA, 68020)", 0));
    hc_cfgov_pick(&pk);
    printf("long title: [%s]\n", pk.title);
    CHECK(!strcmp(pk.title, "Amiga 1200 (AGA, 68020) - start with"));

    // cursor limits
    for (int i = 0; i < 10; i++) hc_cfgov_move(-1);
    CHECK(o.cur == 0 && !strcmp(hc_cfgov_cur_name(), ""));
    for (int i = 0; i < 10; i++) hc_cfgov_move(+1);
    CHECK(o.cur == 5);

    // S9: D sets, D again clears; on Plain start it clears; the boot.cfg line carries it
    hc_cfgov_move(-1);                                                               // old
    g_saves = 0;
    CHECK(hc_cfgov_default() && g_saves == 1 && !strcmp(hc_cfg().e[0].defcfg, "old") && g_saved.find("|old\n") != std::string::npos);
    CHECK(hc_cfgov_default() && !hc_cfg().e[0].defcfg[0] && g_saved.find("|old") == std::string::npos);
    hc_cfgov_default();
    for (int i = 0; i < 9; i++) hc_cfgov_move(-1);                                   // Plain start
    CHECK(hc_cfgov_default() && !hc_cfg().e[0].defcfg[0]);

    // S9: rename carries the default along, refuses a clash and a bad name
    hc_cfgov_move(+1); hc_cfgov_move(+1); hc_cfgov_move(+1); hc_cfgov_move(+1);      // old
    CHECK(!strcmp(hc_cfgov_cur_name(), "old"));
    hc_cfgov_default();
    CHECK(!hc_cfgov_rename("Zed") && fk.files.count("/SD/cores/amiga/media/old.mnt"));
    CHECK(!hc_cfgov_rename("a/b") && !hc_cfgov_rename(""));
    CHECK(hc_cfgov_rename("Retro mix") && fk.files.count("/SD/cores/amiga/media/Retro mix.mnt") && !fk.files.count("/SD/cores/amiga/media/old.mnt"));
    CHECK(!strcmp(hc_cfg().e[0].defcfg, "Retro mix") && !strcmp(hc_cfgov_cur_name(), "Retro mix"));

    // S9: delete asks twice; another key between disarms; the default clears with the file
    CHECK(hc_cfgov_delete() == 1 && hc_cfgov().ask == 1 && fk.files.count("/SD/cores/amiga/media/Retro mix.mnt"));
    hc_cfgov_move(-1);
    CHECK(hc_cfgov().ask == 0);
    hc_cfgov_move(+1);
    CHECK(hc_cfgov_delete() == 1 && hc_cfgov_delete() == 2);
    CHECK(!fk.files.count("/SD/cores/amiga/media/Retro mix.mnt") && hc_cfgov().l.n == 4 && !hc_cfg().e[0].defcfg[0]);
    for (int i = 0; i < 9; i++) hc_cfgov_move(-1);
    CHECK(hc_cfgov_delete() == 0 && hc_cfgov().l.n == 4);                            // Plain start is not a config

    // F5 (D13): the default config steps through none / the core's configs, wrapping
    char nx[40];
    CHECK(hc_cfgov_cycle("/SD/cores/amiga.cor", "", +1, nx, sizeof(nx)) && !strcmp(nx, "alpha"));
    CHECK(hc_cfgov_cycle("/SD/cores/amiga.cor", "Zed", +1, nx, sizeof(nx)) && nx[0] == 0);
    CHECK(hc_cfgov_cycle("/SD/cores/amiga.cor", "", -1, nx, sizeof(nx)) && !strcmp(nx, "Zed"));
    CHECK(!hc_cfgov_cycle("/SD/cores/vic.cor", "", +1, nx, sizeof(nx)));
    CHECK(hc_entry_defcfg(1, "x/y") == HCR_REFUSED && hc_entry_defcfg(1, "Zed") == HCR_OK && !strcmp(hc_cfg().e[1].defcfg, "Zed"));

    // the entry survives a move and the next parse (an older host would read the line without the field)
    hc_entry_move(1, -1);
    CHECK(!strcmp(hc_cfg().e[0].defcfg, "Zed"));

    // the core's hand-over: a config named in the start travels once
    // (the hand-over of the name itself: test_hostcore start_sequence; the page: test_handover)
    hc_start_config_set("Games");

    // the cap: 40 files list 16 (D11)
    for (int i = 0; i < 40; i++) { char f[80]; snprintf(f, sizeof(f), "/SD/cores/big/configs/c%02d.cfg", i); fk.files.insert(f); }
    hc_cfgov_forget();
    CHECK(hc_cfgov_count("/SD/cores/big.cor") == CONFIGS_MAX);
    printf("test_cfgov: %d checks, %d failed\n", g_pass + g_fail, g_fail);
    return g_fail != 0;
}
