// T122 §5: the three things the board showed on 2026-10-24 (Atari 800, "Create blank ATR"), for every core that has a Create task.
//
//   (a) the name prompt starts with a clean name, never garbage - the suggestion of every core's state in every state of the medium, and a
//       state that hands over rubbish (unterminated, control characters, 8-bit bytes) shows an empty field or the clean start of the text;
//   (b) Create asks for the folder first: where the folder browser starts (the menu's folder, the folder of the last file the menu ran, the
//       first stick, never /sd), the folder that was chosen is the folder the file lands in, and no other medium takes it if that fails;
//   (c) the image is written in chunks, one per tick of the run loop, with a percentage on screen, and can be cancelled; a failed write
//       leaves no half image.
//
// Everything runs through MenuCtl and the real descriptors' tasks against FakeFs; a tick is one pass of the run loop's busy step.
#include <string>
#include <vector>
#include "check.h"
#include "boardprintf.h"
#include "fakes.h"
#include "cores.h"
#include "ops_glue.h"
#include "menuctl.h"
#include "keyboard.h"
#include "lib_skel.h"
#include "newdisk.h"
#include "newfile.h"
#include "d1541_drive.h"
#if !defined(TEST_APP_VC20)
#error "test_createflow: every core's create flow is its app's unit (run.sh vc20app, a12app, cpcapp, amigaapp, a8app, apple2app); the shared app has no core of it any more"
#endif
#include "sectors.h"

typedef std::vector<uint8_t> Bytes;

#ifdef TEST_APP_VC20
const CoreDesc *const core_table[] = { &vc20_desc, 0 };
#endif

struct Host : MenuHost {
    void info(int, char *out, int n) { snprintf(out, n, "x"); }
    bool return_to_host(char *msg, int n) { snprintf(msg, n, "no"); return false; }
};

struct Rig {
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    Settings *st;
    Host host;
    MenuCtl *m;
    explicit Rig(const CoreDesc *d)
    {
        memset(&c, 0, sizeof(c));
        c.desc = d;
        c.io = &io;
        c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, "/sd/cores/x.cfg", 0);
        st->load();
        c.settings = st;
        m = new MenuCtl(&c, &host);
    }
    ~Rig()
    {
        delete m;
        delete st;
        delete (CbmDrive *)c.drive;
    }
    // selects the row of that name on the page that is up
    bool pick(const char *name)
    {
        char l[100], rt[100], sb[100];
        bool act;
        for (int i = 0; i < m->rows(); i++) {
            m->row(i, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
            if (!strcmp(l, name)) {
                while (m->selected() > i) m->key(KEY_UP);
                while (m->selected() < i) m->key(KEY_DOWN);
                return true;
            }
        }
        printf("FAIL no row \"%s\" (in %s)\n", name, m->title());
        g_fails++;
        return false;
    }
    std::string row0()
    {
        char l[100], rt[100], sb[100];
        bool act;
        m->row(0, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
        return std::string(l) + "|" + rt;
    }
    std::string sub0()
    {
        char l[100], rt[100], sb[120];
        bool act;
        m->row(0, l, sizeof(l), rt, sizeof(rt), &act, sb, sizeof(sb));
        return sb;
    }
    void type(const char *s) { for (; *s; s++) m->key(*s); }
    void clear_name() { for (int i = 0; i < 30; i++) m->key(KEY_BACK); }
    // T212 D5: the blank medium is a row of the empty drive's list on Home; selects the task there
    bool drive_task(const char *label)
    {
        m->key(KEY_F1);
        const char *drive = 0;                                           // the first drive whose empty list has the task
        for (int i = 0; !drive && i < c.desc->nmedia; i++) {
            if (c.desc->media[i].empty_tasks && strstr(c.desc->media[i].empty_tasks, label)) drive = c.desc->media[i].name;
        }
        if (!drive) {
            printf("FAIL no drive has the task \"%s\"\n", label);
            g_fails++;
            return false;
        }
        if (!pick(drive)) return false;
        m->key(KEY_RETURN);
        return pick(label);
    }
    // the drive's list on Home, the task, and its folder browser: leaves the picker up
    void begin(const char *label)
    {
        if (drive_task(label)) m->key(KEY_RETURN);
    }
    // all the way to the name prompt, in `folder`, from wherever the picker starts
    void to_prompt(const char *label, const char *folder)
    {
        begin(label);
        CHECK(m->picking());
        goto_folder(folder);
        m->key(KEY_RETURN);                                              // "Create here"
    }
    // walks the picker to `folder`: up to the list of the media, then down
    void goto_folder(const std::string &folder)
    {
        for (int i = 0; i < 8 && strcmp(m->title(), "/") != 0; i++) m->key(KEY_LEFT);
        std::string rest = folder;
        while (rest.size() > 1) {
            size_t s = rest.find('/', 1);
            std::string part = rest.substr(1, s == std::string::npos ? std::string::npos : s - 1);
            if (!pick((part + "/").c_str())) return;
            m->key(KEY_RETURN);
            rest = s == std::string::npos ? "" : rest.substr(s);
        }
        CHECK_STR(m->title(), folder.c_str());
    }
    // the run loop, until the job is over; the percentages seen after each tick
    std::vector<int> run_job(int limit = 5000)
    {
        std::vector<int> seen;
        for (int i = 0; m->busy() && i < limit; i++) {
            m->tick();
            seen.push_back(m->busy() ? m->progress() : 100);
        }
        return seen;
    }
};

static Bytes bytes_of(FakeFs &fs, const std::string &path)
{
    auto it = fs.files.find(lower(path));
    return it == fs.files.end() ? Bytes() : it->second;
}

static bool clean_name(const char *s)
{
    if (!s || !s[0] || strlen(s) > 24) return false;
    for (; *s; s++) {
        if (*s < 0x20 || *s > 0x7E || strchr("/\\:*?\"<>|", *s)) return false;
    }
    return true;
}

// ---- (a) the suggestion ---------------------------------------------------------------------------------------------------------------------
struct Create { const CoreDesc *d; const char *label; const char *ext; };

static const Create creates[] = {
#ifdef TEST_APP_VC20
    { &vc20_desc,      "Create blank D64", ".d64" },
    { &vc20_desc,      "Create blank G64", ".g64" },
#endif
};

static const Task *task_of(const CoreDesc *d, const char *label)
{
    for (int i = 0; i < d->ntasks; i++) if (!strcmp(d->tasks[i].label, label)) return &d->tasks[i];
    return 0;
}

static void check_suggestions()
{
    for (const Create &k : creates) {
        printf("-- suggestion: %s: %s\n", k.d->title, k.label);
        const Task *t = task_of(k.d, k.label);
        CHECK(t != 0 && t->ask_state != 0 && t->pick_dir);
        if (!t || !t->ask_state) continue;
        std::string ext = k.ext;
        // no medium answers: the field starts with the plain stem
        {
            Rig r(k.d);
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
            r.fs.gone = true;
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
        }
        // a stick with the name taken, and the next one
        {
            Rig r(k.d);
            r.fs.put("/usb0/blank" + ext, Bytes(4, 1));
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank2");
            r.fs.put("/usb0/blank2" + ext, Bytes(4, 1));
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank3");
            // the same buffer after a longer answer: nothing of the old one stays
            r.fs.files.clear();
            r.fs.names.clear();
            r.fs.put("/usb0/x.bin", Bytes(4, 1));
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
            // the folder that was chosen decides
            r.c.fs = &r.fs;
            r.fs.put("/usb1/disks/blank" + ext, Bytes(4, 1));
            snprintf(r.c.media_dir, sizeof(r.c.media_dir), "/usb1/disks");
            int lists = r.fs.lists;
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank2");
            CHECK_EQ(r.fs.lists, lists);                         // the folder was listed by the folder browser: not read again to find a name
            snprintf(r.c.media_dir, sizeof(r.c.media_dir), "/usb0");
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
        }
        // every name up to 99 is taken: the stem again, never the end of a buffer
        {
            Rig r(k.d);
            r.fs.put("/usb0/blank" + ext, Bytes(4, 1));
            for (int i = 2; i < 100; i++) r.fs.put("/usb0/blank" + std::to_string(i) + ext, Bytes(4, 1));
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
        }
        // a folder with a long path
        {
            Rig r(k.d);
            std::string deep = "/usb0";
            while (deep.size() < 180) deep += "/folder_with_a_long_name";
            r.fs.put(deep + "/x.bin", Bytes(4, 1));
            snprintf(r.c.media_dir, sizeof(r.c.media_dir), "%s", deep.c_str());
            CHECK_STR(ops_state(&r.c, t->ask_state), "blank");
        }
        // and what the prompt shows: the suggestion, the cursor behind it, nothing else
        {
            Rig r(k.d);
            r.fs.put("/usb0/games/blank" + ext, Bytes(4, 1));
            r.m->open();
            r.to_prompt(k.label, "/usb0/games");
            CHECK(r.m->entering());
            CHECK_STR(r.row0().c_str(), (std::string(t->ask) + "|blank2_").c_str());
            CHECK(clean_name(ops_state(&r.c, t->ask_state)));
        }
    }
    // Every state a task prefills a prompt from (Save as ... too) answers a clean name on a medium that works.
    #ifdef TEST_APP_VC20
    for (const CoreDesc *d : { &vc20_desc }) {
#endif
        for (int i = 0; i < d->ntasks; i++) {
            if (!d->tasks[i].ask_state) continue;
            Rig r(d);
            r.fs.put("/usb0/x.bin", Bytes(4, 1));
            const char *s = ops_state(&r.c, d->tasks[i].ask_state);
            if (!clean_name(s)) { printf("FAIL %s: %s -> \"%s\"\n", d->title, d->tasks[i].ask_state, s ? s : "(null)"); g_fails++; }
        }
    }
}


// ---- (c) the progress -------------------------------------------------------------------------------------------------------------------------
static void check_progress_for(const CoreDesc *desc, const char *label, const char *ext, int fmt, uint32_t bytes)
{
    printf("-- progress: %s: %s\n", desc->title, label);
    std::string e = ext;
    Rig r(desc);
    r.fs.put("/usb0/games/x.bin", Bytes(4, 1));
    r.m->open();
    r.to_prompt(label, "/usb0/games");
    r.clear_name();
    r.type("prog");
    CHECK_EQ(r.m->key(KEY_RETURN), MenuCtl::R_REDRAW);
    // the first screen after the name: busy, 0 %, nothing is written yet - the person sees "Building" before the first byte moves
    CHECK(r.m->busy());
    CHECK_EQ(r.m->progress(), 0);
    CHECK_EQ(r.fs.writes, 0);
    CHECK_EQ(r.m->rows(), 1);
    CHECK_STR(r.row0().c_str(), "Building the image|0%");
    CHECK(strstr(r.m->status(), "Building") != 0);
    // keys wait while it writes
    CHECK_EQ(r.m->key('a'), MenuCtl::R_NONE);
    CHECK_EQ(r.m->key(KEY_F10), MenuCtl::R_NONE);
    CHECK(r.m->busy());
    // the ticks: each is one chunk
    std::vector<int> seen;
    size_t before = 0;
    uint32_t most = 0;
    int ticks = 0;
    std::string firstrow;
    while (r.m->busy() && ticks < 5000) {
        r.m->tick();
        ticks++;
        for (size_t i = before; i < r.fs.write_sizes.size(); i++) if (r.fs.write_sizes[i] > most) most = r.fs.write_sizes[i];
        for (uint32_t s : r.fs.save_sizes) if (s > most) most = s;
        before = r.fs.write_sizes.size();
        if (r.m->busy()) {
            seen.push_back(r.m->progress());
            if (ticks == 1) firstrow = r.row0();
        }
    }
    CHECK(!r.m->busy());
    uint32_t chunks = (bytes + NF_CHUNK - 1) / NF_CHUNK;
    CHECK(most <= NF_CHUNK && most > 0);                                 // no write was longer than a chunk: no call holds the lock for the whole image
    CHECK_EQ(ticks, (int)chunks);
    CHECK(ticks >= 5);
    CHECK_EQ(seen.size(), (size_t)chunks - 1);                           // the last tick ends the job
    for (size_t i = 1; i < seen.size(); i++) CHECK(seen[i] > seen[i - 1]);   // the percentage moves with every chunk
    CHECK(!seen.empty() && seen.front() > 0 && seen.back() < 100);
    CHECK_EQ(seen.empty() ? -1 : seen.front(), (int)((uint64_t)NF_CHUNK * 100 / bytes));
    CHECK(firstrow.find("Writing /usb0/games/prog" + e) == 0);           // the row names the file, with its whole path, and its percentage
    CHECK(!seen.empty() && firstrow.find("|" + std::to_string(seen.front()) + "%") != std::string::npos);
    CHECK_STR(r.m->status(), ("Created /usb0/games/prog" + e + ".").c_str());
    CHECK(!r.m->status_bad());
    // the file is the whole image
    std::string path = "/usb0/games/prog" + e;
    CHECK_EQ(bytes_of(r.fs, path).size(), (size_t)bytes);
    if (fmt >= 0) {
        Bytes want(bytes);
        nd_build(fmt, want.data(), bytes, "prog");
        CHECK(bytes_of(r.fs, path) == want);
    }
    CHECK_EQ(r.fs.syncs, 1);                                             // closed once, at the end
    // the display is gone: the page of the tasks again
    CHECK(r.m->rows() > 1);

    // cancelled in the middle: the partial file is removed
    r.to_prompt(label, "/usb0/games");
    r.clear_name();
    r.type("cut");
    r.m->key(KEY_RETURN);
    for (int i = 0; i < 3; i++) r.m->tick();
    CHECK(r.m->busy());
    CHECK(r.fs.has("/usb0/games/cut" + e));
    CHECK_EQ(r.m->key(KEY_ESCAPE), MenuCtl::R_REDRAW);
    CHECK(r.m->busy());                                                  // dropped by the next tick, which is where the lock is taken
    r.m->tick();
    CHECK(!r.m->busy());
    CHECK(!r.fs.has("/usb0/games/cut" + e));
    CHECK(strstr(r.m->status(), "Cancelled") != 0);
    CHECK(!r.m->status_bad());
    // cancelled before the first chunk: nothing was written
    int w = r.fs.writes;
    r.to_prompt(label, "/usb0/games");
    r.clear_name();
    r.type("none");
    r.m->key(KEY_RETURN);
    r.m->key(KEY_ESCAPE);
    r.m->tick();
    CHECK(!r.m->busy());
    CHECK_EQ(r.fs.writes, w);
    CHECK(!r.fs.has("/usb0/games/none" + e));
    // the stick is pulled in the middle: the half image goes, the status says so
    r.fs.write_fail_at = r.fs.chunks_ok + 2;
    r.to_prompt(label, "/usb0/games");
    r.clear_name();
    r.type("pull");
    r.m->key(KEY_RETURN);
    r.run_job();
    CHECK(!r.m->busy());
    CHECK(r.m->status_bad());
    CHECK(strstr(r.m->status(), "pull") != 0);
    CHECK(!r.fs.has("/usb0/games/pull" + e));
    r.fs.write_fail_at = -1;
    // and the next one works: no job is left over
    r.to_prompt(label, "/usb0/games");
    r.clear_name();
    r.type("after");
    r.m->key(KEY_RETURN);
    r.run_job();
    CHECK(!r.m->status_bad());
    CHECK_EQ(bytes_of(r.fs, "/usb0/games/after" + e).size(), (size_t)bytes);
}

static void check_progress()
{
#ifdef TEST_APP_VC20
    // the 1541 library: its own builder and writer state, same display
    check_progress_for(&vc20_desc, "Create blank D64", ".d64", -1, 174848);
#else
    check_progress_for(&apple2_desc, "Create blank DSK", ".dsk", ND_APPLE_DSK, ND_APPLE_BYTES);
#endif
}

int main()
{
#if !defined(TEST_APP_VC20)          // the cases that stand on the shared app's cores
    // the grammar of the board's printf, itself (what the shim below lets through)
    CHECK(board_format_ok("%s_ %d %02x %-5s %*d %u KB %c"));
    CHECK(!board_format_ok("%.*s"));
    CHECK(!board_format_ok("%.4s"));
    CHECK(!board_format_ok("%d%%"));
    CHECK(!board_format_ok("%lu"));
    CHECK(!board_format_ok("100%"));
    // what the product prints is checked as it runs: a format with a precision (or %%) in any of the paths below is a failed check
#endif
    check_suggestions();
    check_progress();
    DONE("createflow");
}
