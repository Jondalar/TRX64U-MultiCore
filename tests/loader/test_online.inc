// Base 4.0 (T239.3 D58): the search of the "online" table is TRX64-Ultimate's library now, linked into its app through its TOML B, and the file manager (netfs.cc) reads the table it exports
// by name from the slots of the app (LibLoader::find_export, online_api.h). Included by test_libload.cc before main. What is real: netfs.cc, compiled for the host, over the slot table of this
// test (g_libslot: the slot "online" answers g_online_tab). What is a model: the table itself, a stub that records its calls and answers what the test set.
#include <string>

static bool g_o_has = false, g_o_loading = false, g_o_loading_for = false, g_o_fail = false;
static std::string g_o_log;
static const char *g_o_err = "the server said no";

static bool o_has_results(void) { g_o_log += "has;"; return g_o_has; }
static int o_list(const char *rel, FsEntry *out, int max)
{
    g_o_log += std::string("list(") + rel + ");";
    if (g_o_fail) {
        return -1;
    }
    int n = 0;
    for (const char *nm : { "Result One", "Result Two" }) {
        if (n < max) {
            memset(&out[n], 0, sizeof(out[n]));
            snprintf(out[n].name, sizeof(out[n].name), "%s", nm);
            out[n].dir = true;
            n++;
        }
    }
    return n;
}
static bool o_stat(const char *rel, uint64_t *size, bool *dir)
{
    g_o_log += std::string("stat(") + rel + ");";
    *size = 3;
    *dir = !strcmp(rel, "") || !strcmp(rel, "/Result One");
    return true;
}
static uint8_t *o_fetch(const char *rel, uint8_t *(*alloc)(uint32_t n), uint32_t *len)
{
    g_o_log += std::string("fetch(") + rel + ");";
    if (g_o_fail) {
        return 0;
    }
    uint8_t *b = alloc(3);
    memcpy(b, "abc", 3);
    *len = 3;
    return b;
}
static const char *o_error(void) { return g_o_err; }
static bool o_loading(void) { return g_o_loading; }
static bool o_loading_for(const char *) { return g_o_loading_for; }
static const OnlineApi g_ostub = { sizeof(OnlineApi), o_has_results, o_list, o_stat, o_fetch, o_error, o_loading, o_loading_for };
static const uint32_t g_otiny = 4;                       // a table of a size that does not cover OnlineApi

static bool has_entry(FsEntry *e, int n, const char *name)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(e[i].name, name)) {
            return true;
        }
    }
    return false;
}

static void online_netfs()
{
    FakeFs inner;
    NetFs nf(&inner);
    FsEntry e[16];
    uint64_t size = 0;
    bool dir = false;
    char buf[8] = "";

    // the slot answers a table: the folder "Online" is listed at the root once there are results, and every path below it goes to the table
    g_online_tab = &g_ostub;
    g_o_has = false;
    g_o_log.clear();
    int n = nf.list("/", e, 16);
    CHECK(n >= 0 && !has_entry(e, n, "Online"));                       // a table with no results: no folder
    g_o_has = true;
    n = nf.list("/", e, 16);
    CHECK(n >= 1 && has_entry(e, n, "Online") && g_o_log.find("has;") != std::string::npos);
    g_o_log.clear();
    n = nf.list("/Online", e, 16);
    CHECK(n == 2 && !strcmp(e[0].name, "Result One") && e[0].dir && g_o_log == "list();");
    n = nf.list("/Online/Result One", e, 16);
    CHECK_EQ(n, 2);
    CHECK(g_o_log.find("list(/Result One);") != std::string::npos);
    CHECK(nf.stat("/Online/Result One", &size, &dir) && dir && size == 3);
    CHECK(nf.stat("/Online/Result One/f.d64", &size, &dir) && !dir);
    CHECK_EQ(nf.load("/Online/Result One/f.d64", buf, sizeof(buf)), 3);
    CHECK(!memcmp(buf, "abc", 3));
    CoreFile *f = nf.open("/Online/Result One/f.d64", false);
    CHECK(f != 0 && f->size() == 3 && !f->writable());
    if (f) {
        nf.close(f);
    }
    CHECK(!nf.save("/Online/x", "x", 1) && !nf.remove("/Online/x") && !nf.mkdir("/Online/y") && !nf.writable("/Online/x"));    // the results are read only

    // a failing call: the table's error, and a job that is still fetching is a wait, not a failure
    g_o_fail = true;
    g_o_loading = false;
    CHECK_EQ(nf.list("/Online", e, 16), -1);
    CHECK(!nf.loading() && !strcmp(nf.why(), g_o_err) && !strcmp(nf.last_error(), g_o_err));
    g_o_loading = true;
    CHECK_EQ(nf.list("/Online", e, 16), -1);
    CHECK(nf.loading() && nf.why()[0] == 0);
    g_o_loading_for = true;
    CHECK(nf.open("/Online/Result One/f.d64", false) == 0 && nf.file_loading(true) && !strcmp(nf.open_note(), g_o_err));
    g_o_fail = false;
    g_o_loading = g_o_loading_for = false;

    // no table on this core (the slot's function answers null), a table that is too small, and the slot read at another major: netfs answers as for an absent table
    for (int variant = 0; variant < 2; variant++) {
        g_online_tab = variant == 0 ? 0 : (const void *)&g_otiny;
        g_o_log.clear();
        n = nf.list("/", e, 16);
        CHECK(n >= 0 && !has_entry(e, n, "Online"));
        CHECK_EQ(nf.list("/Online", e, 16), -1);
        CHECK(!nf.loading() && !strcmp(nf.why(), "No online search in this app"));
        CHECK(!nf.stat("/Online/Result One", &size, &dir));
        CHECK_EQ(nf.load("/Online/Result One/f.d64", buf, sizeof(buf)), -1);
        CHECK(nf.open("/Online/Result One/f.d64", false) == 0 && !nf.file_loading(false));
        CHECK(g_o_log.empty());                                        // the table was never called
    }
    CHECK(LibLoader::find_export(ONLINE_API_NAME, U2A3_VERSION(ONLINE_API_MAJOR + 1, 0), 0) == 0);
    g_online_tab = &g_ostub;
    CHECK(LibLoader::find_export(ONLINE_API_NAME, U2A3_VERSION(ONLINE_API_MAJOR, 0), 0) == &g_ostub);
}
