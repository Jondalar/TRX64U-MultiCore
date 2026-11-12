// T192.2-S3 / S4: the MultiCore update as a job (host_core_flow.cc: hc_update_job / hc_update_run). The flow is run by two drivers against a rig with no screen:
//   - blocking: hc_update_run, the old call, with the keys pushed by the flow's own hook points;
//   - stepped: hc_update_job, `step(input)` by hand, the keys given as the step's input when the page on top asks for them.
// Both record the page data (every distinct page: title, frame, each line with its tone, the message, the bar, the hint), the notes and the flash's erase and program
// counts; the record of every case equals the golden file (tests/hostapp/golden/hostjobs_update.txt, captured from the flow before it was cut into steps), field by field.
// S4: a job that asks does nothing until it is answered: stepped 1000 times with no input it has written nothing.
//   test_hostjobs <dir of mktxutest.py> <golden file> [--write-golden]
#include "host_core_flow.h"
#include "host_core_job.h"
#include "txu.h"
#include "flash.h"
#include "flashfw.h"
#include "cor.h"
#include "keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <signal.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
typedef std::vector<uint8_t> Bytes;

static std::string dir;
static Bytes load(const std::string &name)
{
    Bytes b;
    FILE *f = fopen((dir + "/" + name).c_str(), "rb");
    if (!f) { printf("FAIL cannot read %s\n", name.c_str()); fails++; return b; }
    fseek(f, 0, SEEK_END);
    b.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(b.data(), 1, b.size(), f) != b.size()) fails++;
    fclose(f);
    return b;
}

struct MemSrc : CorSource {
    Bytes b;
    explicit MemSrc(const Bytes &x) : b(x) { }
    uint32_t size() { return (uint32_t)b.size(); }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if ((uint64_t)off + len > b.size()) return false;
        memcpy(buf, &b[off], len);
        return true;
    }
};

// a flash with the real semantics (erase to 0xFF, program only clears bits) that counts every erase and program per region; it can fail a program once
class FakeFlash : public Flash {
public:
    Bytes mem;
    int er_bit, er_app, er_rest, pr_bit, pr_app, pr_rest;
    std::string type;
    int fail_program_at, fail_count;              // from the n-th program on, fail_count programs fail (a page has three tries), then the flash heals
    int programs;
    FakeFlash() : mem(16 << 20, 0xFF), type("Winbond W25Q128"), fail_program_at(0), fail_count(3), programs(0) { clear_counts(); }
    void clear_counts() { er_bit = er_app = er_rest = pr_bit = pr_app = pr_rest = 0; programs = 0; }
    const char *get_type_string(void) { return type.c_str(); }
    int get_page_size(void) { return 256; }
    int get_sector_size(int) { return 4096; }
    int page_to_sector(int page) { return page / 16; }
    bool need_erase(void) { return true; }
    bool erase_sector(int s)
    {
        uint32_t a = (uint32_t)s * 4096;
        if (a < FW_BIT_SLOT) er_bit++; else if (a < FW_APP_ADDR + FW_APP_SLOT) er_app++; else er_rest++;
        memset(&mem[a], 0xFF, 4096);
        return true;
    }
    bool read_page(int p, void *b) { memcpy(b, &mem[(size_t)p * 256], 256); return true; }
    bool write_page(int p, const void *b)
    {
        programs++;
        if (fail_program_at && programs >= fail_program_at && programs < fail_program_at + fail_count) {
            return false;
        }
        uint32_t a = (uint32_t)p * 256;
        if (a < FW_BIT_SLOT) pr_bit++; else if (a < FW_APP_ADDR + FW_APP_SLOT) pr_app++; else pr_rest++;
        const uint8_t *s = (const uint8_t *)b;
        for (int i = 0; i < 256; i++) mem[(size_t)p * 256 + i] &= s[i];
        return true;
    }
    void fill(uint32_t addr, const Bytes &v) { memcpy(&mem[addr], v.data(), v.size()); }
    int writes() const { return er_bit + er_app + er_rest + pr_bit + pr_app + pr_rest; }
};

static uint8_t bit_stage[FW_BIT_SLOT + 8192], app_stage[FW_APP_SLOT + 8192];
static uint32_t KEY;
static Bytes want_bit, want_app;

// ---- the rig: an environment with no screen -------------------------------------------------------------------------------------------------------------
typedef std::function<int(const HcPage &page, int &state)> Responder;     // the key the person presses when this page appears, -1 none (`state`: the person's memory in this run)
struct Rig {
    FakeFlash *flash;
    std::string file;
    std::vector<std::string> log;
    std::vector<int> keys;
    uint32_t now;
    HcPage top;
    bool have_top;
    Responder respond;
    bool blocking;                                 // the blocking driver: the key goes into the environment's key queue; else it is the next step's input
    int pending;                                   // the stepped driver: the key for the next step
    int rstate;                                    // the responder's memory
    int idles;
};

static const char *tone_name(int t)
{
    static const char *const n[] = { "-", "ink", "muted", "ok", "warn", "bad", "host" };
    return t >= 0 && t < 7 ? n[t] : "?";
}

static std::string page_text(const HcPage &p)
{
    char b[4096];
    int n = snprintf(b, sizeof(b), "page|%s|%s|", p.title, tone_name(p.border));
    for (int i = 0; i < HC_PAGE_LINES; i++) {
        if (p.line[i][0] || p.lcol[i]) {
            n += snprintf(b + n, sizeof(b) - n, "l%d=%s:%s|", i, tone_name(p.lcol[i]), p.line[i]);
        }
    }
    snprintf(b + n, sizeof(b) - n, "msg=%s:%s|bar=%d|pct=%s|hint=%s", tone_name(p.mcol), p.msg, p.bar, p.pct, p.hint);
    return b;
}

static void r_draw(void *c, const HcPage &p, bool)
{
    Rig *r = (Rig *)c;
    r->top = p;
    r->have_top = true;
    std::string t = page_text(p);
    if (r->log.empty() || r->log.back() != t) {
        r->log.push_back(t);
    }
    int k = r->respond ? r->respond(p, r->rstate) : -1;                       // the person looks at the page and presses the key it asks for
    if (k >= 0) {
        if (r->blocking) r->keys.push_back(k);
        else r->pending = k;
    }
}
static int r_getch_nowait(void *c)
{
    Rig *r = (Rig *)c;
    if (r->keys.empty()) return -1;
    int k = r->keys.front();
    r->keys.erase(r->keys.begin());
    return k;
}
static int r_getch(void *c) { return r_getch_nowait(c); }
static void r_idle(void *c) { ((Rig *)c)->idles++; }
static void r_sleep(void *c, int ms) { ((Rig *)c)->now += ms; }
static void r_hook(void *, int, uint64_t, uint64_t) { }
static void r_note(void *c, const char *text, int tone)
{
    char b[300];
    snprintf(b, sizeof(b), "note|%s|%s", tone_name(tone), text);
    ((Rig *)c)->log.push_back(b);
}
static void r_refused(void *c, const char *l0)
{
    char b[300];
    snprintf(b, sizeof(b), "refused|%s", l0);
    ((Rig *)c)->log.push_back(b);
}
static void r_write_failed(void *c, const char *why)
{
    char b[300];
    snprintf(b, sizeof(b), "write_failed|%s", why);
    ((Rig *)c)->log.push_back(b);
}
static void r_log(void *c, const char *line)
{
    // the console lines carry the times of the run: only what a person can read in them is kept (the first word)
    char b[300];
    snprintf(b, sizeof(b), "log|%.40s", line);
    ((Rig *)c)->log.push_back(b);
}
static uint32_t r_now(void *c) { return ((Rig *)c)->now; }
static Flash *r_flash(void *c) { return ((Rig *)c)->flash; }
static uint8_t *r_bit(void *) { return bit_stage; }
static uint8_t *r_app(void *) { return app_stage; }
static CorSource *r_open(void *c, const char *path)
{
    Rig *r = (Rig *)c;
    (void)path;
    if (r->file == "no_such_file.txu") return 0;                    // the file cannot be opened
    Bytes b = load(r->file);
    if (b.empty()) return 0;
    return new MemSrc(b);
}
static int r_list(void *, const char *, HostDirEntry *, int, bool) { return 0; }
static bool r_power_cycle(void *, const char **why)
{
    *why = "no control module";
    return false;
}

static HcFlowEnv make_env(Rig *r)
{
    HcFlowEnv e;
    memset(&e, 0, sizeof(e));
    e.draw = r_draw;
    e.getch_nowait = r_getch_nowait;
    e.getch = r_getch;
    e.idle = r_idle;
    e.sleep_ms = r_sleep;
    e.hook = r_hook;
    e.note = r_note;
    e.refused = r_refused;
    e.write_failed = r_write_failed;
    e.log = r_log;
    e.now = r_now;
    e.flash = r_flash;
    e.bit_buf = r_bit;
    e.app_buf = r_app;
    e.open = r_open;
    e.list = r_list;
    e.power_cycle = r_power_cycle;
    e.ctx = r;
    return e;
}

// ---- the cases ---------------------------------------------------------------------------------------------------------------------------------------------
struct Case {
    const char *name;
    const char *file;
    std::function<void(FakeFlash &)> setup;
    Responder respond;
};

// "the page on top asks": the key the person would press for it
static int answer(const HcPage &p, bool retry_once, int &retries)
{
    std::string h = p.hint;
    if (h.find("Y continues") != std::string::npos) return 'y';
    if (h.find("RETURN starts the write (9 s)") != std::string::npos) return KEY_RETURN;       // the last question takes no key in its first second
    if (h.find("any key restarts now") != std::string::npos) return 'x';
    if (h.find("RETURN writes again") != std::string::npos) {
        if (retry_once && retries++ == 0) return KEY_RETURN;
        return KEY_ESCAPE;
    }
    if (h.find("any key returns") != std::string::npos) return 'x';
    return -1;
}

static std::vector<Case> cases()
{
    std::vector<Case> v;
    auto both_flash = [](FakeFlash &f) { f.fill(FW_BIT_ADDR, want_bit); f.fill(FW_APP_ADDR, want_app); };
    auto resp = [](const HcPage &top, int &st) { return answer(top, false, st); };
    auto resp_retry = [](const HcPage &top, int &st) { return answer(top, true, st); };
    auto resp_no = [](const HcPage &top, int &) {                           // 'n' to the first question
        std::string h = top.hint;
        return h.find("Y continues") != std::string::npos ? (int)'n' : -1;
    };
    auto resp_no2 = [](const HcPage &top, int &) {                          // 'y' to the first, 'n' to the last question
        std::string h = top.hint;
        if (h.find("Y continues") != std::string::npos) return (int)'y';
        if (h.find("RETURN starts the write") != std::string::npos) return (int)'n';
        return -1;
    };
    v.push_back({ "good_both", "both.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "app_only_host_equal", "both.txu", [](FakeFlash &f) { f.fill(FW_BIT_ADDR, want_bit); }, resp });
    v.push_back({ "nothing_to_write", "both.txu", both_flash, resp });
    v.push_back({ "new_host_key", "newkey.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "cancel_first_question", "both.txu", [](FakeFlash &) { }, resp_no });
    v.push_back({ "cancel_last_question", "both.txu", [](FakeFlash &) { }, resp_no2 });
    v.push_back({ "corrupt_crc", "crc.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "not_a_txu", "not_txu.cfw", [](FakeFlash &) { }, resp });
    v.push_back({ "wrong_chip_in_package", "idcode100t.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "wrong_flash_chip", "both.txu", [](FakeFlash &f) { f.type = "Winbond W25Q80"; }, resp });
    v.push_back({ "payload_corrupt_app", "payload_app.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "iface_mismatch", "iface_bad.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "unreadable_file", "no_such_file.txu", [](FakeFlash &) { }, resp });
    v.push_back({ "write_fails_then_retry", "both.txu", [](FakeFlash &f) { f.fail_program_at = 40; }, resp_retry });
    v.push_back({ "write_fails_and_leave", "both.txu", [](FakeFlash &f) { f.fail_program_at = 40; }, resp });
    return v;
}

static std::string record(Rig &r, bool ok)
{
    std::string s;
    for (auto &l : r.log) s += l + "\n";
    char b[200];
    snprintf(b, sizeof(b), "flash|erase bit=%d app=%d rest=%d|program bit=%d app=%d rest=%d\nresult|%s\n", r.flash->er_bit, r.flash->er_app, r.flash->er_rest,
             r.flash->pr_bit, r.flash->pr_app, r.flash->pr_rest, ok ? "ok" : "not ok");
    return s + b;
}

static void start_rig(Rig &r, FakeFlash &f, const Case &c)
{
    r.flash = &f;
    r.file = c.file;
    r.log.clear();
    r.keys.clear();
    r.now = 1000;
    r.have_top = false;
    r.respond = c.respond;
    r.blocking = true;
    r.pending = -1;
    r.rstate = 0;
    r.idles = 0;
    c.setup(f);
    f.clear_counts();
}

static std::string run_blocking(const Case &c)
{
    FakeFlash f;
    Rig r;
    start_rig(r, f, c);
    r.blocking = true;
    HcFlowEnv e = make_env(&r);
    bool ok = hc_update_run(e, "/USB0/update.txu");
    return std::string("case ") + c.name + "\n" + record(r, ok);
}

// stepped: the flow is stepped by hand; each time it waits, the key the page on top asks for is the next step's input
#ifndef OLD_FLOW
static bool g_entered_running;                      // the last stepped run: did the job ever enter `running`
static std::string run_stepped(const Case &c, int *steps_out = 0, bool allow_untill_end = true)
{
    FakeFlash f;
    Rig r;
    start_rig(r, f, c);
    r.blocking = false;
    HcFlowEnv e = make_env(&r);
    HcJob *j = hc_update_job(e, "/USB0/update.txu");
    int steps = 0;
    HcJobIn in;
    in.key = -1;
    g_entered_running = false;
    for (;;) {
        bool done = j->step(in);
        steps++;
        g_entered_running = g_entered_running || j->state == HCJ_RUNNING || (j->state == HCJ_RESULT && j->pages > 0 && !j->can_cancel);
        if (done || steps > 200000) break;
        in.key = allow_untill_end ? r.pending : -1;                 // the key the page asked for is the next step's input
        r.pending = -1;
    }
    bool ok = j->ok;
    CHECK(j->done());
    delete j;
    if (steps_out) *steps_out = steps;
    return std::string("case ") + c.name + "\n" + record(r, ok);
}

#endif

static void on_alarm(int)
{
    printf("FAIL the flow did not end within 30 s\n");
    fflush(stdout);
    _exit(1);
}

int main(int argc, char **argv)
{
    signal(SIGALRM, on_alarm);
    alarm(30);
    if (argc < 3) { printf("usage: test_hostjobs <dir> <golden> [--write-golden]\n"); return 2; }
    dir = argv[1];
    std::string golden_path = argv[2];
    bool write = argc > 3 && !strcmp(argv[3], "--write-golden");
    {
        std::map<std::string, std::string> facts;
        FILE *f = fopen((dir + "/facts.txt").c_str(), "r");
        char k[64], v[128];
        while (f && fscanf(f, "%63s %127s", k, v) == 2) facts[k] = v;
        if (f) fclose(f);
        KEY = (uint32_t)strtoul(facts["key"].c_str(), 0, 16);
    }
    cor_host_key = KEY;
    want_bit = load("bit.bin");
    want_app = load("app.bin");
    std::string golden_all;
    std::vector<Case> cs = cases();
    for (auto &c : cs) {
        std::string a = run_blocking(c);
#ifdef OLD_FLOW                                    // the capture of the flow before it was cut into steps (the golden file): it has no job to step
        std::string b = a;
#else
        std::string b = run_stepped(c);
#endif
        golden_all += a;
        if (a != b) {
            printf("the stepped job's record differs from the blocking call's in case %s\n--- blocking\n%s--- stepped\n%s", c.name, a.c_str(), b.c_str());
        }
        CHECK(a == b);
#ifndef OLD_FLOW
        // S3: a refusal reaches `result` from `probing` without ever entering `running` (and writes nothing); a write goes through `running`
        bool refused = a.find("\nrefused|") != std::string::npos;
        bool wrote = a.find("flash|erase bit=0 app=0 rest=0|program bit=0 app=0 rest=0") == std::string::npos;
        CHECK(!(refused && g_entered_running));
        CHECK(!wrote || g_entered_running);
        CHECK(!(!wrote && g_entered_running));
#endif
    }
    if (write) {
        if (FILE *f = fopen(golden_path.c_str(), "w")) {
            fwrite(golden_all.data(), 1, golden_all.size(), f);
            fclose(f);
        }
        printf("golden written: %s (%zu bytes)\n", golden_path.c_str(), golden_all.size());
    } else {
        std::string want;
        if (FILE *f = fopen(golden_path.c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) want.append(buf, n);
            fclose(f);
        }
        if (want != golden_all) {
            printf("the record differs from the golden file %s\n", golden_path.c_str());
            std::string mine = golden_path + ".new";
            if (FILE *f = fopen(mine.c_str(), "w")) { fwrite(golden_all.data(), 1, golden_all.size(), f); fclose(f); }
        }
        CHECK(want == golden_all);
    }
#ifndef OLD_FLOW
    // S4: a job that asks does nothing until it is answered. Stepped 1000 times with no input at the first question, the flash saw no write; a key answers it.
    {
        FakeFlash f;
        Rig r;
        Case c = cs[0];
        start_rig(r, f, c);
        r.blocking = false;
        HcFlowEnv e = make_env(&r);
        HcJob *j = hc_update_job(e, "/USB0/update.txu");
        HcJobIn in;
        in.key = -1;
        int steps = 0;
        while (j->state != HCJ_ASKING && !j->step(in) && steps++ < 1000) {
        }
        CHECK(j->state == HCJ_ASKING);
        uint32_t pages = j->pages;
        for (int i = 0; i < 1000; i++) {
            CHECK(!j->step(in));
            if (!(j->state == HCJ_ASKING)) break;
        }
        CHECK(j->state == HCJ_ASKING && f.writes() == 0);                // waiting, and nothing was written
        CHECK(j->pages >= pages);
        in.key = 'n';                                                    // the answer: no
        for (int i = 0; i < 10 && !j->done(); i++) j->step(in), in.key = -1;
        CHECK(j->done() && !j->ok && f.writes() == 0);
        delete j;
        // the first question has its own idle time-out (60 s, fwconfirm.h): a job nobody answers ends with nothing written
        {
            FakeFlash f3;
            Rig r3;
            start_rig(r3, f3, c);
            r3.blocking = false;
            HcFlowEnv e3 = make_env(&r3);
            HcJob *j3 = hc_update_job(e3, "/USB0/update.txu");
            HcJobIn in3;
            in3.key = -1;
            int n3 = 0;
            while (!j3->step(in3) && n3++ < 20000) {
            }
            CHECK(j3->done() && !j3->ok && f3.writes() == 0 && n3 > 1000);
            delete j3;
        }
        // a key (the confirm) given to a job that is not asking is not taken as an answer: the job in the middle of its check has no question to answer
        FakeFlash f2;
        Rig r2;
        start_rig(r2, f2, c);
        r2.blocking = false;
        HcFlowEnv e2 = make_env(&r2);
        HcJob *j2 = hc_update_job(e2, "/USB0/update.txu");
        HcJobIn in2;
        in2.key = KEY_RETURN;                                            // RETURN before any question: the check page reads no key as an answer
        j2->step(in2);
        CHECK(j2->state == HCJ_ASKING && f2.writes() == 0);
        delete j2;
    }
#endif
    printf("%s (%d failed)\n", fails ? "test_hostjobs FAILED" : "test_hostjobs ok", fails);
    return fails ? 1 : 0;
}
