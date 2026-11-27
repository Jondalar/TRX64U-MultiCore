// T93 S14: the application never writes the flash. The guard the board's CoreFs uses (fm_fs.cc) and a scan of the application's
// own sources for the flash driver's write and erase calls (the linked image carries them anyway: they hang on the vendored Flash
// vtable, W25Q_Flash::write_page and friends - the chip's own write protection, FLASH_EXTRA_PROTECTION, is the second lock).
#include <string>
#include <string.h>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include "check.h"
#include "fsif.h"

static std::string slurp(const std::string &p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main()
{
    CHECK(fs_path_is_flash("/flash"));
    CHECK(fs_path_is_flash("/Flash/boot.cfg"));
    CHECK(fs_path_is_flash("/FLASH/Cores/a.cfg"));
    CHECK(!fs_path_is_flash("/flashy/x"));
    CHECK(!fs_path_is_flash("/usb0/flash/x"));
    CHECK(!fs_path_is_flash("/sd/a.cfg"));
    CHECK(!fs_path_is_flash(""));

    // T104.2 Q1, T239.3 P1-5: the exceptions are the standalone core's two files, registered from its record (board_set_core); the host's boot menu file is fixed.
    // The whole path, case-insensitive like FAT; nothing next to it. Before a core is registered only the host's file passes.
    CHECK(!fs_flash_exception("/flash/trx64u/c64.cfg") && !fs_flash_exception("/flash/trx64u/boot.cfg") && fs_flash_exception("/Flash/boot.cfg"));
    CHECK(fs_write_refused("/flash/trx64u/c64.cfg") && !fs_write_refused("/Flash/boot.cfg"));
    CHECK(fs_flash_set_exceptions("/flash/trx64u/boot.cfg", "/flash/trx64u/c64.cfg"));
    CHECK(fs_flash_exception("/flash/trx64u/c64.cfg"));
    CHECK(fs_flash_exception("/Flash/TRX64U/C64.CFG"));
    CHECK(!fs_write_refused("/flash/trx64u/c64.cfg"));
    // T104.1 5.5: the boot-attempt counter's file, one per profile - the second file of /flash/<core id>, and the host's boot menu file. Nothing else.
    CHECK(fs_flash_exception("/flash/trx64u/boot.cfg"));
    CHECK(fs_flash_exception("/Flash/boot.cfg"));
    CHECK(fs_flash_exception("/FLASH/TRX64U/BOOT.CFG"));
    CHECK(!fs_write_refused("/flash/trx64u/boot.cfg") && !fs_write_refused("/Flash/boot.cfg"));
    CHECK(fs_write_refused("/flash/trx64u/boot.cfg.bak"));
    CHECK(fs_write_refused("/flash/trx64u/boot.cfg/x"));
    CHECK(fs_write_refused("/flash/boot.cfg.bak"));
    CHECK(fs_write_refused("/flash/boot.cfg/x"));
    CHECK(fs_write_refused("/flash/trx64u/boot2.cfg"));
    CHECK(fs_write_refused("/flash/trx64u/c64.cfg.bak"));
    CHECK(fs_write_refused("/flash/trx64u/dummy800.cfg"));
    CHECK(fs_write_refused("/flash/trx64u"));
    CHECK(fs_write_refused("/flash/trx64u/"));
    CHECK(fs_write_refused("/flash/trx64u/x/../c64.cfg"));
    CHECK(fs_write_refused("/flash/roms/kernal.bin"));
    CHECK(fs_write_refused("/flash/c64.cfg"));
    CHECK(fs_write_refused("/flash"));
    CHECK(!fs_write_refused("/usb0/trx64u/c64.cfg"));             // other media are not the flash's business
    CHECK(!fs_write_refused("/sd/cores/dummy800.cfg"));
    // a registration replaces the last one; a path that is no "/flash/<dir>/<file>" is refused and registers nothing
    CHECK(fs_flash_set_exceptions("/flash/demo/boot.cfg", 0));
    CHECK(fs_write_refused("/flash/trx64u/c64.cfg") && fs_write_refused("/flash/trx64u/boot.cfg") && !fs_write_refused("/flash/demo/boot.cfg"));
    CHECK(!fs_flash_set_exceptions("/flash/boot.cfg", 0) && !fs_flash_exception("/flash/demo/boot.cfg"));    // no directory: registers nothing (the path is the host's fixed file, so its write is not refused)
    CHECK(!fs_flash_set_exceptions("/flash/x.cfg", 0) && fs_write_refused("/flash/x.cfg"));                   // no directory
    CHECK(!fs_flash_set_exceptions("/flash/a/b/c.cfg", 0) && fs_write_refused("/flash/a/b/c.cfg"));            // too deep
    CHECK(!fs_flash_set_exceptions("/flash/a/", 0) && !fs_flash_set_exceptions("/flash", 0) && !fs_flash_set_exceptions("/flash/", 0));
    CHECK(!fs_flash_set_exceptions("/usb0/a/b.cfg", 0) && !fs_flash_set_exceptions("/flashy/a/b.cfg", 0));
    CHECK(fs_flash_set_exceptions(0, ""));
    CHECK(fs_write_refused("/flash/demo/boot.cfg"));              // cleared
    CHECK(fs_flash_set_exceptions("/flash/trx64u/boot.cfg", "/flash/trx64u/c64.cfg"));

    // T228 R21: on the monolith profile the startup sets the flag and the flash drive is written as in TRX64-Ultimate's firmware; the medium panel and the
    // file actions ask the same function
    fs_flash_rw() = true;
    CHECK(!fs_write_refused("/flash/roms/kernal.bin") && !fs_write_refused("/Flash/carts/x.crt") && !fs_write_refused("/flash/trx64u/c64.cfg"));
    CHECK(!fs_write_refused("/flash"));
    CHECK(fs_path_is_flash("/flash/roms"));                         // still the flash: only the refusal is lifted
    fs_flash_rw() = false;
    CHECK(fs_write_refused("/flash/roms/kernal.bin"));

    const char *forbidden[] = { "write_page", "erase_sector", "write_config_page", "clear_config_page", "->write()", "get_flash", "Flash *",
                                "flash_write", "FlashWrite", 0 };
    int scanned = 0;
    for (const char *dir : { COREAPP_DIR "/src", COREAPP_DIR "/cores" }) {
        DIR *d = opendir(dir);
        CHECK(d != 0);
        while (d) {
            struct dirent *e = readdir(d);
            if (!e) break;
            std::string n = e->d_name;
            if (n.size() < 3 || (n.substr(n.size() - 3) != ".cc" && n.substr(n.size() - 2) != ".h")) continue;
            std::string text = slurp(std::string(dir) + "/" + n);
            scanned++;
            // T104.3: appsvc.cc holds the driver pointer to give an application the flash's bytes to READ; T228 R21:
            // dmasvc.cc too, for the Ultimate DMA service's READFLASH (page size, page count, one page: FW socket_dma.cc). The write calls stay forbidden in both
            // like everywhere, and so does every other use of the pointer in every other file.
            bool reader = n == "appsvc.cc" || n == "dmasvc.cc";
            for (int i = 0; forbidden[i]; i++) {
                if (reader && (!strcmp(forbidden[i], "get_flash") || !strcmp(forbidden[i], "Flash *"))) continue;
                // settings.h names "FlashLayer", the read-only host layer: "Flash *" would be a pointer to the driver class
                size_t at = text.find(forbidden[i]);
                if (at != std::string::npos) { printf("%s mentions %s\n", n.c_str(), forbidden[i]); CHECK(false); }
            }
        }
        if (d) closedir(d);
    }
    CHECK(scanned > 20);
    DONE("test_noflash");
}
