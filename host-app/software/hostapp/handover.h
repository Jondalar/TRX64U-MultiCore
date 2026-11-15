// The hand-over from the host app to a core application (T93 D11): one 4 KiB page in the DDR2 that the host app fills just
// before it jumps (core_jump) and the application reads first. Pure code: the host app's coreboot.cc and the blueprint
// (coreapp/) both include it, and tests/coreapp/ runs it natively. Nothing here knows a machine.
#ifndef HOST_HANDOVER_H
#define HOST_HANDOVER_H

#include <stdint.h>

// Outside the application's load range (0x30000..0xE80000), the staging areas (0x03000000..0x03C00000), the trampoline
// (0x03F00000) and every core window (the Amiga's is 0x1000000..0x1FFFFFF). T93 5.3 re-measured this against coreboot.h.
#define HANDOVER_ADDR     0x03EFF000u
#define HANDOVER_MAGIC    0x48585254u        // "TRXH", little endian
#define HANDOVER_VERSION  1u
#define HANDOVER_PATH_MAX 256

struct Handover {
    uint32_t magic;
    uint32_t version;
    uint32_t sum;                 // handover_sum() over everything below
    uint32_t video;               // the picture the host put up, video_pack(); 0 = it chose none (T105)
    char path[HANDOVER_PATH_MAX]; // the .cor path as the host opened it
    char core_name[48];           // the manifest's name
    char core_version[24];        // the manifest's version
    char host_build[16];          // the host key, 8 hex digits, or "" when the bitstream names none
    // ---- after the version-1 bytes (T115 7, 2026-10-26): handover_sum() and handover_valid() stop at id_sum, so a reader built before this
    // field accepts the block, and a block of an older host (which leaves what lies behind its size untouched) has no id. Read it with
    // handover_core_id() only.
    uint32_t id_sum;              // handover_id_sum(): the id's hash mixed with `sum`; 0 = no id
    char core_id[24];             // the core's id (core.toml id, the manifest's "id" of a large .cor), or ""
    // ---- after the id (T191, 2026-11-07): the host's wall-clock time at the jump, so a core application (which has no SNTP and no clock of its
    // own) can set its software clock. Outside handover_sum() and id_sum like the id: older readers stay valid, an older host's block has none.
    // Read it with handover_time() only.
    uint32_t time_sum;            // handover_time_sum(): date and time mixed with `sum`; 0 = no time
    uint32_t time_date;           // local calendar year << 16 | month << 8 | day
    uint32_t time_time;           // hour << 16 | minute << 8 | second
    // ---- after the time (T198 D7, 2026-11-10): "start with this config" - the name of a config of the core (<core folder>/configs/<name>.cfg, without the extension).
    // Outside handover_sum() and every earlier check word like the id and the time: older readers ignore it (plain start), a block of an older host has none.
    // Read it with handover_config() only.
    uint32_t config_sum;          // handover_config_sum(): the name mixed with `sum` and `id_sum`; 0 = no config
    char config[48];              // the config's name, "" = plain start (HANDOVER_CONFIG_MAX characters at most)
    // ---- after the config (T224 D2, 2026-11-15): "start with safe graphics" - the core application takes Picture Auto, Scale Integer, Smoothing Off for this start and writes none of
    // them. Outside handover_sum() and every earlier check word like the config: older readers ignore it, a block of an older host has none. Read it with handover_safe() only.
    uint32_t safe_sum;            // handover_safe_sum(): mixed with `sum` and `id_sum`; 0 = not asked
};
#define HANDOVER_CONFIG_MAX 32    // T198 D11: a config name is at most 32 characters; the field holds 47

uint32_t handover_sum(const Handover *h);

// T135 D3: the "menu requested" marker, two words at the end of the same 4 KiB page, outside struct Handover and outside its sum, so every
// reader of the struct stays valid. A core application writes it just before its IPROG back to the host ("Return to host menu"); the host
// reads and clears it at the start of its decision. DDR2 holds noise after a power-up: the second word is the complement of the first, and
// the page of a core start (handover_fill, cfw_handover_page) is written without it.
#define HANDOVER_MENU_OFFSET 0xFF8u
#define HANDOVER_MENU_MAGIC  0x554E454Du     // "MENU", little endian
void handover_menu_request(void *page);      // page = HANDOVER_ADDR (or a test buffer of 4096 bytes)
bool handover_menu_take(void *page);         // true when the marker was there; clears it either way

// Fills *h completely (the page is wiped first); strings are cut to their fields, never left unterminated.
void handover_fill(Handover *h, const char *path, const char *core_name, const char *core_version, uint32_t host_key,
                   uint32_t video = 0, const char *core_id = 0);

// The core's id the block carries, or "" when there is none or the bytes do not belong to this block (noise, or left behind by another
// start): never a wrong id, the name decides then. Call on a block handover_valid() accepted.
const char *handover_core_id(const Handover *h);

// T191: puts the host's local time (calendar year) into a filled block; handover_time() gives it back, false when the block carries none, the
// check does not hold (noise, a block of an older host) or the date is no plausible one (year 2020..2099).
void handover_set_time(Handover *h, int year, int mon, int day, int hour, int min, int sec);
bool handover_time(const Handover *h, int *year, int *mon, int *day, int *hour, int *min, int *sec);

// T198 D7: puts the config's name into a filled block (after handover_fill); a name that is empty or longer than HANDOVER_CONFIG_MAX leaves the block without one
// and returns false. handover_config() gives it back, "" when the block carries none, the check does not hold (noise, a block of an older host) or the bytes are not a
// terminated name: never a wrong name, the core then starts plain.
bool handover_set_config(Handover *h, const char *name);
const char *handover_config(const Handover *h);

// T224 D2: asks for the safe graphics start (handover_safe() gives it back; false for a block that carries none, whose check does not hold, or that was never asked).
void handover_set_safe(Handover *h, bool on);
bool handover_safe(const Handover *h);

// True when magic, version and sum agree and the path is terminated. DDR2 holds noise after a power-up and the previous
// core's block after a JTAG start: this tells noise from a block, not a fresh block from a stale one.
bool handover_valid(const Handover *h);

// "<dir>/<name>.cor" -> "<dir>/<name>.cfg" (T93 S6: the settings follow the bundle, never a fixed place). False when
// the path does not end in .cor (any case) or does not fit.
bool handover_cfg_path(const char *cor_path, char *out, int outlen);

#endif
