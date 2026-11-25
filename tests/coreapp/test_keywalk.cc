// T192.3 S6 / S15: the keys of the menu, walked on EVERY core of the table. For each core, each tab of its menu and a handful of starting states (the tab as it opens, after
// RETURN, after DOWN RETURN, after RIGHT, after RETURN RETURN), every key of the menu's grammar is pressed once on a fresh controller; what the controller says after it (result,
// area, tab, cursor, rows, title, dialog, status, toast, the flags of the prompts) is hashed. One line per (core, tab, start) is the golden: a change of what any key does on any
// core moves a hash. KEYWALK_VERBOSE=1 prints every probe (to find what moved), UPDATE_GOLDEN=1 writes the golden. The golden was made from the controller before the intent
// cut (T192 D23), so a controller that is a client of the intent table must reproduce it exactly.
#include <string>
#include <vector>
#include <fstream>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "menuctl.h"
#include "keyboard.h"
#include "ops_glue.h"
#include "record_menu.h"
#include "instfiles.h"

struct Host : MenuHost {
    void info(int, char *out, int) { out[0] = 0; }
    bool return_to_host(char *, int) { return false; }
};

#ifdef TEST_APP_NES                      // the NES's app (run.sh nesapp, nes_keywalk): the NES's descriptor, then the record core over it, its goldens keywalk_nes.txt and ids_nes.txt
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#define KW_GOLDEN "_nes"
#elif defined(TEST_APP_ATARI800)         // the Atari's app is its own build (run.sh a8app, the unit a8_keywalk): its table is its core only, its walk has goldens of its own
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
const CoreDesc *const core_table[] = { &atari800_desc, 0 };
#define KW_GOLDEN "_atari800"
#elif defined(TEST_APP_C264)             // the Commodore 264's app is its own build (run.sh c264app): its table is its core only, its walk has goldens of its own
extern const CoreDesc c264_desc;         // no shared header declares it (cores/c264/app/lib holds this one)
const CoreDesc *const core_table[] = { &c264_desc, 0 };
#define KW_GOLDEN "_c264"
#elif defined(TEST_APP_ZXNEXT)           // the ZX Spectrum Next's app is its own build (run.sh zxnextapp): its table is its core only, its rows are golden/keywalk_zxnext.txt and ids_zxnext.txt
extern const CoreDesc zxnext_desc;       // cores.h lists the shared app's cores; the ZX Spectrum Next is its own app (cores/zxnext/app/lib)
const CoreDesc *const core_table[] = { &zxnext_desc, 0 };
#define KW_GOLDEN "_zxnext"
#elif defined(TEST_APP_NEOGEO)           // the Neo Geo's app (run.sh neogeoapp, the unit neogeo_keywalk): its table is its core only, its golden is its own
extern const CoreDesc neogeo_desc;       // cores.h lists the shared app's cores; the Neo Geo is its own app (cores/neogeo/app/lib)
const CoreDesc *const core_table[] = { &neogeo_desc, 0 };
#define KW_GOLDEN "_neogeo"
#elif defined(TEST_APP_AMIGA)            // the Amiga 500's app (run.sh amigaapp, the unit amiga_keywalk): its table is its core only, its rows are golden/keywalk_amiga.txt and ids_amiga.txt
extern const CoreDesc amiga_desc;        // cores.h lists the shared app's cores; the Amiga 500 is its own app (cores/amiga/app/lib)
const CoreDesc *const core_table[] = { &amiga_desc, 0 };
#define KW_GOLDEN "_amiga"
#elif defined(TEST_APP_CPC)              // the CPC's app (run.sh cpcapp, the unit cpc_keywalk): its table is its core only, its rows are golden/keywalk_cpc.txt and ids_cpc.txt
extern const CoreDesc cpc_desc;          // cores.h lists the shared app's cores; the CPC is its own app (cores/cpc/app/lib)
const CoreDesc *const core_table[] = { &cpc_desc, 0 };
#define KW_GOLDEN "_cpc"
#elif defined(TEST_APP_C128)             // the C128's app (run.sh c128app, the unit c128_keywalk): its table is its core only, its rows are golden/keywalk_c128.txt and ids_c128.txt
extern const CoreDesc c128_desc;         // cores.h lists the shared app's cores; the C128 is its own app (cores/c128/app/lib)
const CoreDesc *const core_table[] = { &c128_desc, 0 };
#define KW_GOLDEN "_c128"
#elif defined(TEST_APP_PET)              // the PET's app (run.sh petapp, the unit pet_keywalk): its table is its core only, its rows are golden/keywalk_pet.txt and ids_pet.txt
extern const CoreDesc pet_desc;          // cores.h lists the shared app's cores; the PET is its own app (cores/pet/app/lib)
const CoreDesc *const core_table[] = { &pet_desc, 0 };
#define KW_GOLDEN "_pet"
#elif defined(TEST_APP_DDT)              // DDT Look Ma, No VIC's app (run.sh ddtapp, the unit ddt_keywalk): its table is its core only, its rows are golden/keywalk_ddt_novic.txt and ids_ddt_novic.txt
extern const CoreDesc ddt_novic_desc;    // cores.h lists the shared app's cores; DDT Look Ma, No VIC is its own app (cores/ddt_novic/app/lib)
const CoreDesc *const core_table[] = { &ddt_novic_desc, 0 };
#define KW_GOLDEN "_ddt_novic"
#elif defined(TEST_APP_ZX)               // the ZX Spectrum's app (run.sh zxapp, the unit zx_keywalk): its table is its core only, its rows are golden/keywalk_zx.txt and ids_zx.txt
extern const CoreDesc zx_desc;           // cores.h lists the shared app's cores; the ZX Spectrum is its own app (cores/zx/app/lib)
const CoreDesc *const core_table[] = { &zx_desc, 0 };
#define KW_GOLDEN "_zx"
#else                                    // the shared app: the cores of its table
extern const CoreDesc *const core_table[];
#define KW_GOLDEN ""
#endif

static uint32_t fnv(uint32_t h, const char *s)
{
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return (h ^ 0xFFu) * 16777619u;
}
static uint32_t fnvi(uint32_t h, long v)
{
    char b[24];
    snprintf(b, sizeof(b), "%ld", v);
    return fnv(h, b);
}

static std::string state_of(MenuCtl &m, int result, uint32_t *hash)
{
    char text[400];
    const char *dk = m.dialog(text, sizeof(text));
    char l[200] = "", r[200] = "", s[200] = "";
    bool act = false;
    int sel = m.selected();
    if (m.rows() > 0) {
        m.row(sel < 0 ? 0 : sel, l, sizeof(l), r, sizeof(r), &act, s, sizeof(s));
    }
    char fl[200] = "";
    m.footer_legend(fl, sizeof(fl));
    char line[1600];
    snprintf(line, sizeof(line), "R%d a%d t%d s%d n%d |%s|%s|%s|%s|%s|%d|%s|%s|%s|%d|%s c%d p%d b%d e%d v%d m%d q%d u%d br%d prog%d", result, m.area(), m.tab(), sel, m.rows(), m.title(), dk, text,
             m.status(), m.toast_text(), m.status_bad(), l, r, s, act, fl, m.chooser(), m.picking(), m.busy(), m.entering(), m.viewer(), m.mapping(), m.search_editing(), m.unsaved(),
             m.browsing(), m.progress());
    *hash = fnv(2166136261u, line);
    return line;
}

static const int KEYS[] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_PAGEUP, KEY_PAGEDOWN, KEY_HOME, KEY_END, KEY_RETURN, KEY_SPACE, KEY_ESCAPE, 0x60, KEY_BACK, KEY_TAB, KEY_BREAK,
    KEY_F10, KEY_SCRLOCK, KEY_MENU, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, '1', '2', '3', '4', '5', '6', '7', '8', '9', '/', '?', 'a', 'e', 'x', 'r', 'p', 'k', 'z', 'H', 'T',
    '+', '-', '{', '}', '_', '=', KEY_CTRL_A, KEY_CTRL_C, KEY_CTRL_V, KEY_CTRL_N, 'D', 'Y', 'O' };
static const int NKEYS = sizeof(KEYS) / sizeof(KEYS[0]);
// the starting states: key sequences pressed before the probe key
static const std::vector<std::vector<int>> STARTS = { {}, { KEY_RETURN }, { KEY_DOWN, KEY_RETURN }, { KEY_RIGHT }, { KEY_RETURN, KEY_RETURN }, { KEY_DOWN, KEY_DOWN, KEY_RIGHT, KEY_RETURN },
    { KEY_RETURN, KEY_RETURN, KEY_RETURN }, { KEY_RETURN, KEY_DOWN, KEY_RETURN, KEY_RIGHT }, { KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_RIGHT, 'T' }, { KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_RIGHT, 'H' },
    { '/', 'a' }, { KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_DOWN, KEY_RIGHT, 'D' }, { KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_DOWN, KEY_RIGHT, 'C' }, { KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_DOWN, KEY_RIGHT, 'N' },
    { KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_DOWN, KEY_RIGHT }, { KEY_RETURN, KEY_RETURN, KEY_SPACE, KEY_SPACE, KEY_CTRL_C }, { KEY_RETURN, KEY_RETURN, KEY_RETURN, KEY_DOWN, KEY_RIGHT, KEY_RETURN } };

static void settle(MenuCtl &m)
{
    for (int i = 0; i < 60 && m.busy(); i++) {
        m.tick();
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    bool verbose = getenv("KEYWALK_VERBOSE") != 0;
    std::string out, ids;
    int probes = 0;
    // the cores of the table (the shared app, or a core's own app: its core), or in the NES's app the NES's descriptor and a core whose menu is a record (golden/desc_cut.toml over the
    // NES: the Timewarp and monitor pages, the key table; T239.3 P1-8, in place of the TRX64U's menu)
    struct Walk { const CoreDesc *d; const MenuMerge *merge; const char *name; };
    std::vector<Walk> walks;
#ifndef TEST_APP_NES
    (void)argc; (void)argv;
    for (const CoreDesc *const *pd = core_table; *pd; pd++) {
        walks.push_back({ *pd, 0, 0 });
    }
#else
    (void)argc;
    walks.push_back({ &nes_desc, 0, 0 });                       // the NES's own menu, as its app shows it
    static CoreDesc base = nes_desc;
    base.needs_program = 0;                                     // the menu of a core with no program loaded does not close
    RecordMenu *rm = new RecordMenu(&base, argv[1]);
    walks.push_back({ &rm->desc, &rm->mm, "Record Core" });
#endif
    for (const Walk &w : walks) {
        const CoreDesc *d = w.d;
        const char *cname = w.name ? w.name : d->cor_name ? d->cor_name : "?";
        const MenuMerge *merge = w.merge;
        FakeIo io; FakeFs fs; CoreCtx c;
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs; c.merge = merge;
        char cor[80]; snprintf(cor, sizeof(cor), "/sd/cores/%s.cor", cname);
        for (char *q = cor; *q; q++) if (*q == ' ' || *q == '(' || *q == ')' || *q == ',') *q = '_';
        snprintf(c.cor_path, sizeof(c.cor_path), "%s", cor);
        Settings st(d->settings, d->nsettings, d->store, &fs, "/sd/cores/kw.cfg", 0);
        st.load();
        c.settings = &st;
        c.started = true;
        c.install_files = core_files_install;       // run.cc sets it for every core; pd.install (Write the example tests) goes through it
        std::vector<uint8_t> diag_cor;               // T175: the example tests are record 8 of diag.cor; the application's installer writes the missing ones at every start of the core, as on the board
        if (!strcmp(cname, "Port Diagnostics") && getenv("DIAG_COR")) {
            std::ifstream df(getenv("DIAG_COR"), std::ios::binary);
            diag_cor.assign((std::istreambuf_iterator<char>(df)), std::istreambuf_iterator<char>());
        }
        auto start_core = [&]() {                    // the bundle is on the card only while the installer reads it: the folder listing is the bench's, not the bundle's
            if (!diag_cor.empty()) {
                fs.put(c.cor_path, diag_cor);
                core_files_install(&c);
                fs.remove(c.cor_path);
            }
        };
        start_core();
        fs.put("/usb0/x/a.bin", std::vector<uint8_t>(64, 1));
        fs.put("/usb0/x/b.txt", std::vector<uint8_t>(40, 'b'));
        fs.put("/usb0/x/e.prg", std::vector<uint8_t>{ 1, 8, 0, 0 });
        fs.put("/usb0/x/d.d64", std::vector<uint8_t>(174848, 0));
        fs.put("/usb0/x/sub/c.bin", std::vector<uint8_t>(16, 3));
        fs.put("/sd/readme.txt", std::vector<uint8_t>(10, 'r'));
        Host host;
        // the tabs of the menu: the area/tab list of a fresh controller
        MenuCtl *probe = new MenuCtl(&c, &host);
        std::vector<std::string> specs;
        const MenuTree &t = probe->tree();
        for (int a = 0; a < t.nareas; a++) {
            for (int b = 0; b < t.a[a].ntabs; b++) {
                if (t.a[a].tabs[b].id) {
                    specs.push_back(std::string(t.a[a].key) + "/" + t.a[a].tabs[b].id);
                }
            }
        }
        delete probe;
        for (const std::string &spec : specs) {                      // T192.3 S7: what every row IS (menuctl.h Item::ident): a renamed key or label is a visible diff of tests/coreapp/golden/ids.txt
            MenuCtl im(&c, &host);
            im.open();
            if (!im.goto_spec(spec.c_str()) || im.browsing()) {
                continue;
            }
            for (int i = 0; i < im.rows(); i++) {
                MenuCtl::Item it;
                im.item(i, &it);
                char ln[220];
                snprintf(ln, sizeof(ln), "%s %s %d %s\n", cname, spec.c_str(), i, it.ident);
                ids += ln;
            }
        }
        for (const std::string &spec : specs) {
            for (size_t si = 0; si < STARTS.size(); si++) {
                uint32_t acc = 2166136261u;
                int n = 0;
                for (int ki = 0; ki < NKEYS; ki++) {
                    start_core();
                    MenuCtl m(&c, &host);
                    io.t += 5000;
                    m.open();
                    m.toast_clear();
                    if (!m.goto_spec(spec.c_str())) {
                        continue;
                    }
                    for (int pk : STARTS[si]) {
                        m.key(pk);
                        settle(m);
                    }
                    int rr = m.key(KEYS[ki]);
                    settle(m);
                    uint32_t h;
                    std::string line = state_of(m, rr, &h);
                    acc = fnvi(fnvi(acc, KEYS[ki]), (long)h);
                    n++;
                    probes++;
                    if (verbose) {
                        printf("PROBE %s %s s%zu key 0x%02X: %s\n", cname, spec.c_str(), si, KEYS[ki], line.c_str());
                    }
                }
                char ln[200];
                snprintf(ln, sizeof(ln), "%s %s s%zu %d %08x\n", cname, spec.c_str(), si, n, (unsigned)acc);
                out += ln;
            }
        }
    }
    printf("keywalk: %d probes\n", probes);
    std::string path = std::string(GOLDEN_DIR) + "/keywalk" KW_GOLDEN ".txt";
    if (getenv("UPDATE_GOLDEN")) {
        std::ofstream(path) << out;
    }
    std::ifstream f(path);
    std::string want((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (want != out) {
        // name the first differing lines
        size_t a = 0, b = 0;
        int shown = 0;
        while (a < out.size() && b < want.size() && shown < 12) {
            size_t ea = out.find('\n', a), eb = want.find('\n', b);
            std::string la = out.substr(a, ea - a), lb = want.substr(b, eb - b);
            if (la != lb) {
                printf("DIFF got  %s\nDIFF want %s\n", la.c_str(), lb.c_str());
                shown++;
            }
            a = ea + 1;
            b = eb + 1;
        }
    }
    CHECK(want == out);
    std::string ipath = std::string(GOLDEN_DIR) + "/ids" KW_GOLDEN ".txt";
    if (getenv("UPDATE_GOLDEN")) {
        std::ofstream(ipath) << ids;
    }
    std::ifstream fi(ipath);
    std::string iwant((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    if (iwant != ids) {
        printf("the identities of the rows differ from tests/coreapp/golden/ids%s.txt (UPDATE_GOLDEN=1 writes it)\n", KW_GOLDEN);
    }
    CHECK(iwant == ids);
    DONE("test_keywalk");
}
