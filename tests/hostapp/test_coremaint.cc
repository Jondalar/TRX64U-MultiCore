// T148 S6: the "Cores and ROMs" maintenance - the scan (per core: folder, ROMs present or missing, saves; orphans; duplicates) and the removal of
// what belongs to no core (host-app/software/hostapp/coremaint.cc) over a RAM disk.
#include "coremaint.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <map>
#include <set>
#include <string>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define CHECK_STR(a, b) do { std::string _a = (a), _b = (b); if (_a != _b) { printf("FAIL %s:%d \"%s\" vs \"%s\"\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); fails++; } } while (0)

typedef std::vector<uint8_t> Bytes;

struct Ram : FoFs, CmFs {
    std::map<std::string, Bytes> files;
    std::set<std::string> dirs;
    std::map<std::string, std::string> names;           // .cor path -> manifest name
    struct H { std::string path; size_t pos; };

    void put(const std::string &p, size_t n, int seed)
    {
        Bytes b(n);
        for (size_t i = 0; i < n; i++) b[i] = (uint8_t)((i * 7 + seed + (i >> 8)) & 0xFF);
        files[p] = b;
        for (size_t s = p.find('/', 1); s != std::string::npos; s = p.find('/', s + 1)) dirs.insert(p.substr(0, s));
    }
    bool has(const std::string &p) { return files.count(p) || dirs.count(p); }
    // FoFs
    bool stat(const char *path, FoStat *st)
    {
        std::string p = path;
        if (files.count(p)) { st->dir = false; st->size = files[p].size(); return true; }
        if (dirs.count(p)) { st->dir = true; st->size = 0; return true; }
        return false;
    }
    int dir_entries(const char *path)
    {
        CmEntry e[200];
        return list(path, e, 200);
    }
    bool free_bytes(const char *, uint64_t *) { return false; }
    bool same_volume(const char *, const char *) { return true; }
    void *open_read(const char *path) { if (!files.count(path)) return 0; H *h = new H; h->path = path; h->pos = 0; return h; }
    void *open_write(const char *) { return 0; }
    int read(void *hh, void *buf, uint32_t n)
    {
        H *h = (H *)hh;
        Bytes &b = files[h->path];
        size_t k = b.size() - h->pos < n ? b.size() - h->pos : n;
        memcpy(buf, b.data() + h->pos, k);
        h->pos += k;
        return (int)k;
    }
    bool write(void *, const void *, uint32_t) { return false; }
    bool close(void *hh) { delete (H *)hh; return true; }
    bool remove(const char *path) { std::string p = path; if (files.erase(p)) return true; return dirs.erase(p) > 0; }
    bool rename(const char *, const char *) { return false; }
    // CmFs
    int list(const char *dir, CmEntry *out, int max)
    {
        std::string d = dir;
        if (d == "/") {
            std::set<std::string> top;
            for (auto &p : dirs) if (p.find('/', 1) == std::string::npos) top.insert(p.substr(1));
            int n = 0;
            for (auto &t : top) if (n < max) { memset(&out[n], 0, sizeof(CmEntry)); strncpy(out[n].name, t.c_str(), 55); out[n].dir = true; n++; }
            return n;
        }
        if (!dirs.count(d)) return -1;
        std::string pre = d + "/";
        std::map<std::string, CmEntry> found;
        for (auto &kv : files) {
            if (kv.first.compare(0, pre.size(), pre)) continue;
            std::string rest = kv.first.substr(pre.size());
            size_t sl = rest.find('/');
            CmEntry e; memset(&e, 0, sizeof(e));
            strncpy(e.name, (sl == std::string::npos ? rest : rest.substr(0, sl)).c_str(), 55);
            e.dir = sl != std::string::npos;
            e.size = e.dir ? 0 : kv.second.size();
            found[e.name] = e;
        }
        for (auto &p : dirs) {
            if (p.compare(0, pre.size(), pre)) continue;
            std::string rest = p.substr(pre.size());
            size_t sl = rest.find('/');
            std::string nm = sl == std::string::npos ? rest : rest.substr(0, sl);
            CmEntry e; memset(&e, 0, sizeof(e));
            strncpy(e.name, nm.c_str(), 55);
            e.dir = true;
            found[nm] = e;
        }
        int n = 0;
        for (auto &kv : found) if (n < max) out[n++] = kv.second;
        return n;
    }
    bool core_name(const char *p, char *name, int n)
    {
        if (!names.count(p)) return false;
        snprintf(name, n, "%s", names[p].c_str());
        return true;
    }
};

static Ram *world()
{
    Ram *r = new Ram;
    // Apple IIe: folder, system ROM there, video ROM missing, two saves
    r->put("/SD/cores/apple2.cor", 100, 1);
    r->names["/SD/cores/apple2.cor"] = "Apple IIe";
    r->put("/SD/cores/apple2/apple2.cfg", 40, 2);
    r->put("/SD/cores/apple2/roms/apple2e.rom", 16384, 3);
    r->put("/SD/cores/apple2/saves/a.sav", 10, 4);
    r->put("/SD/cores/apple2/saves/b.sav", 10, 5);
    // Atari: complete, and the old .cfg still beside the .cor
    r->put("/SD/cores/atari800.cor", 100, 6);
    r->names["/SD/cores/atari800.cor"] = "Atari 800 XL";
    r->put("/SD/cores/atari800.cfg", 30, 7);
    r->put("/SD/cores/atari800/roms/ATARIXL.ROM", 16384, 8);
    r->put("/SD/cores/atari800/roms/basic.rom", 8192, 9);
    // ZX: a ROM set - one complete set (48K) is enough
    r->put("/SD/cores/zx.cor", 100, 10);
    r->names["/SD/cores/zx.cor"] = "ZX Spectrum 48K / 128K";
    r->put("/SD/cores/zx/roms/48.rom", 16384, 11);
    // a core with no folder yet, and a core nobody listed ROMs for
    r->put("/SD/cores/nes.cor", 100, 12);
    r->names["/SD/cores/nes.cor"] = "Nintendo Entertainment System";
    // the one-folder layout of T112: a .cor in its own folder is a core's folder, not an orphan
    r->put("/SD/cores/amiga/amiga.cor", 100, 13);
    r->put("/SD/cores/amiga/amiga.cfg", 20, 14);
    // #43: the applications every core reads (/SD/cores/apps, coreapp/src/appindex.h APP_SHARED_FOLDER): no core's folder, not an orphan
    r->put("/SD/cores/apps/unzip.u2a", 30000, 26);
    r->put("/SD/cores/apps/notes.txt", 10, 27);
    // what belongs to no core
    r->put("/SD/cores/ghost/ghost.cfg", 20, 15);
    r->put("/SD/cores/ghost/roms/x.rom", 4096, 16);
    r->put("/SD/cores/ghost/saves/s.sav", 50, 17);
    r->put("/SD/cores/gone/gone.cfg", 20, 18);                       // a folder of no core without saves: goes entirely
    r->put("/SD/cores/gone/roms/y.rom", 2048, 19);
    r->put("/SD/cores/junk.txt", 77, 20);
    r->put("/SD/cores/old.cfg", 12, 21);
    // the old places
    r->put("/SD/cores/roms/apple2e-video.rom", 8192, 22);           // no copy anywhere else
    r->put("/SD/roms/atari800/atarixl.rom", 16384, 8);              // the same bytes as the core folder's: a copy exists
    r->put("/SD/roms/atari800/basic.rom", 8192, 99);                // the same size as the core folder's, other bytes: not a copy
    // the flash is Gideon's: never looked at
    r->put("/Flash/cores/junk.txt", 5, 23);
    r->put("/Flash/roms/kernal.bin", 8192, 24);
    // a medium without cores/
    r->put("/USB1/stuff/file.bin", 10, 25);
    return r;
}

static const CmCore *core_of(const CmReport &rep, const char *file)
{
    for (int i = 0; i < rep.ncores; i++) if (!strcasecmp(rep.core[i].file, file)) return &rep.core[i];
    return 0;
}
static const CmOrphan *orph_of(const CmReport &rep, const char *path)
{
    for (int i = 0; i < rep.norph; i++) if (!strcmp(rep.orph[i].path, path)) return &rep.orph[i];
    return 0;
}

int main()
{
    // ---- the table: what a core needs, against the files in its folder
    {
        CmEntry e[2];
        memset(e, 0, sizeof(e));
        int need, have;
        char miss[120];
        cm_rom_status("Atari 800 XL", e, 0, &need, &have, miss, sizeof(miss));
        CHECK(need == 2 && have == 0);
        CHECK_STR(miss, "ATARIXL.ROM, BASIC.ROM");
        strcpy(e[0].name, "atarixl.ROM"); e[0].size = 16384;               // case-blind
        strcpy(e[1].name, "ataribas.rom"); e[1].size = 100;                // the second name, a wrong size
        cm_rom_status("Atari 800 XL", e, 2, &need, &have, miss, sizeof(miss));
        CHECK(need == 2 && have == 1);
        CHECK_STR(miss, "BASIC.ROM");
        e[1].size = 8192;
        cm_rom_status("Atari 800 XL", e, 2, &need, &have, miss, sizeof(miss));
        CHECK(need == 2 && have == 2 && !miss[0]);
        cm_rom_status("Nintendo Entertainment System", e, 2, &need, &have, miss, sizeof(miss));
        CHECK(need == 0 && have == 0 && !miss[0]);
    }
    Ram *w = world();
    CmReport *rep = new CmReport;
    cm_scan(*w, *w, rep);

    // ---- S6: each core shows present / folder / ROMs / saves
    CHECK(rep->ncores == 4);
    const CmCore *a2 = core_of(*rep, "apple2.cor");
    CHECK(a2 && a2->folder && a2->need == 2 && a2->have == 1 && a2->saves == 2);
    CHECK(a2 && strstr(a2->missing, "apple2e-video.rom") && !strstr(a2->missing, "apple2e.rom"));
    CHECK(a2 && !strcmp(a2->medium, "SD") && !strcmp(a2->name, "Apple IIe"));
    const CmCore *at = core_of(*rep, "atari800.cor");
    CHECK(at && at->folder && at->need == 2 && at->have == 2 && !at->missing[0] && at->saves == 0);
    const CmCore *zx = core_of(*rep, "zx.cor");
    CHECK(zx && zx->folder && zx->need == 1 && zx->have == 1 && !zx->missing[0]);            // one complete set is enough
    const CmCore *nes = core_of(*rep, "nes.cor");
    CHECK(nes && !nes->folder);
    CHECK(rep->core[0].medium[0] && !core_of(*rep, "junk.txt"));

    // ---- orphans
    CHECK(orph_of(*rep, "/SD/cores/junk.txt") && orph_of(*rep, "/SD/cores/junk.txt")->deletable);
    CHECK(orph_of(*rep, "/SD/cores/old.cfg") && orph_of(*rep, "/SD/cores/old.cfg")->deletable);
    CHECK(!orph_of(*rep, "/SD/cores/atari800.cfg"));                                         // the .cfg of a present core
    CHECK(!orph_of(*rep, "/SD/cores/apps") && !orph_of(*rep, "/SD/cores/apps/unzip.u2a") && !orph_of(*rep, "/SD/cores/apps/notes.txt"));   // #43
    CHECK(!orph_of(*rep, "/SD/cores/amiga") && !orph_of(*rep, "/SD/cores/amiga/amiga.cfg"));  // a folder with its own .cor
    CHECK(!orph_of(*rep, "/SD/cores/apple2") && !orph_of(*rep, "/SD/cores/apple2/saves/a.sav"));
    const CmOrphan *g = orph_of(*rep, "/SD/cores/ghost");
    CHECK(g && g->dir && !g->deletable);                                                     // it holds a save
    CHECK(orph_of(*rep, "/SD/cores/ghost/ghost.cfg") && orph_of(*rep, "/SD/cores/ghost/ghost.cfg")->deletable);
    CHECK(orph_of(*rep, "/SD/cores/ghost/roms/x.rom") && orph_of(*rep, "/SD/cores/ghost/roms/x.rom")->deletable);
    CHECK(orph_of(*rep, "/SD/cores/ghost/saves/s.sav") && !orph_of(*rep, "/SD/cores/ghost/saves/s.sav")->deletable);
    CHECK(orph_of(*rep, "/SD/cores/gone") && orph_of(*rep, "/SD/cores/gone")->deletable);
    CHECK(orph_of(*rep, "/SD/cores/gone/roms") && orph_of(*rep, "/SD/cores/gone/roms")->deletable);
    // the old places: a copy in a core folder makes a file deletable, a file with no copy is listed and stays
    CHECK(orph_of(*rep, "/SD/roms/atari800/atarixl.rom") && orph_of(*rep, "/SD/roms/atari800/atarixl.rom")->deletable);
    CHECK(orph_of(*rep, "/SD/roms/atari800/basic.rom") && !orph_of(*rep, "/SD/roms/atari800/basic.rom")->deletable);
    CHECK(orph_of(*rep, "/SD/cores/roms/apple2e-video.rom") && !orph_of(*rep, "/SD/cores/roms/apple2e-video.rom")->deletable);
    // the flash and a medium without cores/ are not looked at
    CHECK(!orph_of(*rep, "/Flash/cores/junk.txt") && !orph_of(*rep, "/Flash/roms/kernal.bin"));
    // 7 files go (junk, old.cfg, ghost.cfg, ghost x.rom, gone.cfg, gone y.rom, the Atari copy) and 3 folders (gone, gone/roms, ghost/roms)
    CHECK(rep->ndelete == 10);
    CHECK(orph_of(*rep, "/SD/cores/ghost/roms") && orph_of(*rep, "/SD/cores/ghost/roms")->deletable);
    CHECK(rep->delete_bytes == 77 + 12 + 20 + 4096 + 20 + 2048 + 16384);

    // ---- duplicates: the same ROM content in several places (size and SHA-256), not the same size with other bytes
    CHECK(rep->ndups == 1);
    CHECK(rep->ndups >= 1 && rep->dup[0].n == 2 && rep->dup[0].size == 16384 && !strcasecmp(rep->dup[0].name, "ATARIXL.ROM"));

    // ---- the removal: only what is listed, never a .cor, never inside a present core's folder
    Ram before = *w;
    CmDelete dl;
    cm_delete(*w, *rep, &dl);
    CHECK(dl.deleted == 10 && dl.failed == 0 && dl.refused == 0);
    CHECK(!w->has("/SD/cores/junk.txt") && !w->has("/SD/cores/old.cfg") && !w->has("/SD/cores/gone") && !w->has("/SD/cores/gone/gone.cfg"));
    CHECK(!w->has("/SD/cores/ghost/ghost.cfg") && !w->has("/SD/cores/ghost/roms/x.rom") && !w->has("/SD/roms/atari800/atarixl.rom"));
    CHECK(w->has("/SD/cores/ghost/saves/s.sav") && w->has("/SD/cores/ghost"));                  // the save stays, so does its folder
    CHECK(w->has("/SD/roms/atari800/basic.rom") && w->has("/SD/cores/roms/apple2e-video.rom"));  // no copy: stays
    for (const char *keep : { "/SD/cores/apple2.cor", "/SD/cores/atari800.cor", "/SD/cores/zx.cor", "/SD/cores/nes.cor", "/SD/cores/amiga/amiga.cor",
                              "/SD/cores/apple2/apple2.cfg", "/SD/cores/apple2/roms/apple2e.rom", "/SD/cores/apple2/saves/a.sav",
                              "/SD/cores/atari800.cfg", "/SD/cores/atari800/roms/ATARIXL.ROM", "/SD/cores/zx/roms/48.rom", "/Flash/cores/junk.txt",
                              "/Flash/roms/kernal.bin", "/USB1/stuff/file.bin", "/SD/cores/apps/unzip.u2a", "/SD/cores/apps/notes.txt" }) {
        CHECK(w->has(keep));
    }
    // what is gone is exactly what was listed: every other file is as it was
    int lost = 0;
    for (auto &kv : before.files) if (!w->files.count(kv.first)) lost++;
    CHECK(lost == 7);

    // ---- a tampered report cannot get a .cor or a present core's file removed (cm_may_delete looks at the card, not at the report)
    Ram *w2 = world();
    CmReport *bad = new CmReport;
    memset(bad, 0, sizeof(*bad));
    const char *tamper[] = { "/SD/cores/apple2.cor", "/SD/cores/apple2/roms/apple2e.rom", "/SD/cores/apple2/saves/a.sav", "/SD/cores/apple2", "/SD/cores/nes.cor",
                             "/Flash/cores/junk.txt", "/Flash/roms/kernal.bin", "/SD/cores/apps/unzip.u2a", "/SD/cores/apps" };
    for (const char *t : tamper) {
        CmOrphan &o = bad->orph[bad->norph++];
        snprintf(o.path, sizeof(o.path), "%s", t);
        o.deletable = true;
        o.dir = !strcmp(t, "/SD/cores/apple2") || !strcmp(t, "/SD/cores/apps");
    }
    cm_delete(*w2, *bad, &dl);
    CHECK(dl.deleted == 0 && dl.refused == 9);
    for (const char *t : tamper) CHECK(w2->has(t));
    CHECK(cm_may_delete(*w2, "/SD/cores/ghost/ghost.cfg") && cm_may_delete(*w2, "/SD/cores/junk.txt") && cm_may_delete(*w2, "/SD/roms/atari800/basic.rom"));
    CHECK(!cm_may_delete(*w2, "/SD/cores/amiga/amiga.cfg"));                                  // the one-folder layout: the .cor is in the folder
    printf("test_coremaint: %d failed\n", fails);
    return fails ? 1 : 0;
}
