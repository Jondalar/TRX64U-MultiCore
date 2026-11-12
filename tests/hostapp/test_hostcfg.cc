// T163 S1/S2: the host's own settings store (hostcfg.cc + the backend hooks in config.cc), against the real ConfigManager / ConfigStore with a
// fake SPI flash (Gideon's config pages: counts every read and write) and a fake file. Store definitions are those of "Network Settings"
// in miniature (same FTP item: id 0x24, "FTP File Service", Disabled / Enabled, default Enabled).
#include "hostcfg.h"
#include "filemanager.h"
#include <stdio.h>
#include <string.h>

int FileManager::calls = 0;
void init_flash_disk(void) { }

// ---- the fake SPI flash with Gideon's config pages --------------------------------------------------------------------------------------------
#define NPAGES 4
#define PSIZE 512
class FakeFlash : public Flash
{
public:
    uint8_t page[NPAGES][PSIZE];
    int reads = 0, writes = 0;
    FakeFlash() { memset(page, 0xFF, sizeof(page)); }
    int get_config_page_size(void) override { return PSIZE; }
    int get_number_of_config_pages(void) override { return NPAGES; }
    void read_config_page(int p, int len, void *buf) override { reads++; memcpy(buf, page[p], len); }
    void write_config_page(int p, void *buf) override { writes++; memcpy(page[p], buf, PSIZE); }
    void clear_config_page(int) override { writes++; }
    // Gideon's firmware writing its page: id, then "FTP File Service" and "Host Name"
    void foreign(int slot, uint32_t id, int ftp, const char *host)
    {
        uint8_t *p = page[slot];
        memset(p, 0xFF, PSIZE);
        memcpy(p, &id, 4);
        int n = 4;
        p[n++] = 0x24; p[n++] = CFG_TYPE_ENUM; p[n++] = 1; p[n++] = (uint8_t)ftp;
        p[n++] = 0x30; p[n++] = CFG_TYPE_STRING; p[n++] = (uint8_t)strlen(host); memcpy(p + n, host, strlen(host)); n += strlen(host);
        p[n] = 0xFF;
    }
};
static FakeFlash fake;
Flash *get_flash(void) { return &fake; }

// ---- the fake file ----------------------------------------------------------------------------------------------------------------------------
static char file[HOSTCFG_MAX];
static int  flen = -1;              // -1: no file
static int  read_err = 0;           // 1: the read fails although the file may exist
static int  fwrites = 0;
static int  io_read(char *buf, int max)
{
    if (read_err) return -2;
    if (flen < 0) return -1;
    memcpy(buf, file, flen);
    (void)max;
    return flen;
}
static bool io_write(const char *buf, int len) { memcpy(file, buf, len); flen = len; fwrites++; return true; }
static const HostCfgIo memio = { io_read, io_write };
static void set_file(const char *t) { strcpy(file, t); flen = (int)strlen(t); }
static bool has(const char *s) { return flen >= 0 && (file[flen] = 0, strstr(file, s) != NULL); }

// ---- the store ----------------------------------------------------------------------------------------------------------------------------
static const char *en_dis_t[] = { "Disabled", "Enabled" };
static t_cfg_definition defs[] = {
    { 0xFE, CFG_TYPE_SEP,    "Services",         "",   NULL,   0, 0, 0 },
    { 0x24, CFG_TYPE_ENUM,   "FTP File Service", "%s", en_dis_t, 0, 1, 1 },
    { 0x30, CFG_TYPE_STRING, "Host Name",        "%s", NULL,   3, 31, (long)"def-host" },
    { 0x31, CFG_TYPE_VALUE,  "Zone",             "%d", NULL,   0, 40, 16 },
    { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 }
};
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// A power-on: the file io starts over (hostcfg_install), the store registers.
static ConfigStore *boot(uint32_t id, const char *name, bool fresh_boot = true)
{
    ConfigManager *cm = ConfigManager::getConfigManager();
    ConfigStore *old = cm->find_store(name);
    if (old) cm->remove_store(old);
    if (fresh_boot) hostcfg_install(&memio);
    return cm->register_store(id, name, defs, NULL);
}
static void counters(void) { fake.reads = fake.writes = 0; fwrites = 0; }

int main()
{
    // The first start without anything: our default stands, the file is made.
    flen = -1; counters();
    ConfigStore *s = boot(0x5100, "T-default");
    CHECK(s && s->get_value(0x24) == 1);
    CHECK(!strcmp(s->get_string(0x30), "def-host") && s->get_value(0x31) == 16);
    CHECK(has("[T-default]") && has("FTP File Service=Enabled") && has("Host Name=def-host") && has("Zone=16"));
    CHECK(fake.writes == 0);

    // S1: foreign firmware does not change ours. FTP on in our file; Gideon's page says Disabled.
    set_file("[S1]\nFTP File Service=Enabled\nHost Name=ours\n"); counters();
    fake.foreign(0, 0x5101, 0, "gideon");
    s = boot(0x5101, "S1");
    CHECK(s->get_value(0x24) == 1);                                  // FTP stays on
    CHECK(!strcmp(s->get_string(0x30), "ours"));
    CHECK(fake.reads == 0);                                          // the foreign page is not even looked at (id scan included)
    CHECK(fake.writes == 0 && fwrites == 0);
    // the foreign firmware writes again, we start again
    fake.foreign(0, 0x5101, 0, "gideon2"); counters();
    s = boot(0x5101, "S1");
    CHECK(s->get_value(0x24) == 1 && !strcmp(s->get_string(0x30), "ours") && fake.reads == 0);

    // S2: no store of ours, Gideon's page present: taken over once, the file is made, flash only read.
    flen = -1; counters();
    fake.foreign(1, 0x5102, 0, "gideon");
    s = boot(0x5102, "S2");
    CHECK(s->get_value(0x24) == 0 && !strcmp(s->get_string(0x30), "gideon"));
    CHECK(has("[S2]") && has("FTP File Service=Disabled") && has("Host Name=gideon"));
    CHECK(fwrites == 1 && fake.writes == 0 && fake.reads > 0);
    // a later change in Gideon's page is not seen
    fake.foreign(1, 0x5102, 1, "changed"); counters();
    s = boot(0x5102, "S2");
    CHECK(s->get_value(0x24) == 0 && !strcmp(s->get_string(0x30), "gideon"));
    CHECK(fake.reads == 0 && fwrites == 0);

    // two stores in the first boot: both are taken over (the file exists after the first one's section)
    flen = -1; counters();
    fake.foreign(0, 0x5103, 0, "first"); fake.foreign(1, 0x5104, 1, "second");
    ConfigStore *a = boot(0x5103, "S2-A");
    ConfigStore *b = boot(0x5104, "S2-B", false);
    CHECK(a->get_value(0x24) == 0 && !strcmp(a->get_string(0x30), "first"));
    CHECK(b->get_value(0x24) == 1 && !strcmp(b->get_string(0x30), "second"));
    CHECK(has("[S2-A]") && has("[S2-B]") && fake.writes == 0);

    // the file exists, a store has no section in it: the defaults stay, the flash page is not taken over (the first start is over)
    set_file("[Other]\nkeep=me\n"); counters();
    fake.foreign(2, 0x5105, 0, "foreign");
    s = boot(0x5105, "S2-late");
    CHECK(s->get_value(0x24) == 1 && !strcmp(s->get_string(0x30), "def-host") && fake.reads == 0 && fwrites == 0);
    // ... and a save keeps the section we do not know
    s->set_string(0x30, "mine"); s->write();
    CHECK(has("[Other]\nkeep=me\n") && has("[S2-late]") && has("Host Name=mine") && fake.writes == 0);

    // a change is saved to our file, never to the flash; the next boot reads it
    counters();
    s->set_value(0x24, 0); s->set_value(0x31, 3); s->write();
    CHECK(has("FTP File Service=Disabled") && has("Zone=3") && fwrites == 1 && fake.writes == 0);
    s->get_page()->write();                                          // the page itself must not reach the flash either
    CHECK(fake.writes == 0);
    s = boot(0x5105, "S2-late");
    CHECK(s->get_value(0x24) == 0 && s->get_value(0x31) == 3 && !strcmp(s->get_string(0x30), "mine") && fake.reads == 0);

    // an enum label is read as Gideon's reader does (case, outer spaces); a wrong number is ignored; a long string is cut to the item
    set_file("[T-parse]\nFTP File Service = disabled \nZone=99\nHost Name=a-name-that-is-much-longer-than-thirty-one-chars\nunknown=1\n");
    s = boot(0x5106, "T-parse");
    CHECK(s->get_value(0x24) == 0 && s->get_value(0x31) == 16 && strlen(s->get_string(0x30)) == 31);

    // the file cannot be read (disk trouble): defaults, nothing taken over, nothing written
    set_file("[T-unreadable]\nFTP File Service=Disabled\n"); counters(); read_err = 1;
    fake.foreign(3, 0x5107, 0, "gideon");
    s = boot(0x5107, "T-unreadable");
    CHECK(s->get_value(0x24) == 1 && fake.reads == 0);
    s->set_value(0x24, 0); s->write();
    CHECK(fwrites == 0 && fake.writes == 0);
    read_err = 0;

    // T164 picsave: the picture's store (the definitions of hostvideo_board.cc) keeps every row across a reboot, labels with spaces and slashes included
    {
        static const char *mode_items[] = { "From the screen", "576p / 480p", "1280 x 720", "1920 x 1080", "800 x 600", "1024 x 768", "1280 x 1024" };
        static const char *sys_items[] = { "PAL 50 Hz", "NTSC 60 Hz" };
        static t_cfg_definition vdefs[] = {
            { 0x01, CFG_TYPE_ENUM, "Mode",   "%s", mode_items, 0, 6, 0 },
            { 0x02, CFG_TYPE_ENUM, "System", "%s", sys_items,  0, 1, 0 },
            { CFG_TYPE_END, CFG_TYPE_END, "", "", NULL, 0, 0, 0 }
        };
        flen = -1;
        for (int m = 0; m <= 6; m++) {
            for (int y = 0; y <= 1; y++) {
                ConfigManager *cm = ConfigManager::getConfigManager();
                ConfigStore *old = cm->find_store("Video");
                if (old) cm->remove_store(old);
                hostcfg_install(&memio);
                s = cm->register_store(0x48535401, "Video", vdefs, NULL);
                s->set_value(0x01, m);
                s->set_value(0x02, y);
                s->write();
                cm->remove_store(s);
                hostcfg_install(&memio);                                 // the reboot: the file is read again
                s = cm->register_store(0x48535401, "Video", vdefs, NULL);
                CHECK(s->get_value(0x01) == m && s->get_value(0x02) == y);
            }
        }
    }

    // the page files of the original's fallback and the FileManager are never touched
    CHECK(FileManager::calls == 1);                                 // the one create_dir of the ConfigManager constructor
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
