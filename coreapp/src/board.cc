#include "board.h"
#include "fsif.h"
#include <stdio.h>
#include <string.h>

BoardProfile board_detect(BoardProbe *p)
{
    BoardProfile b;
    memset(&b, 0, sizeof(b));
    uint8_t icap = p->icap_id();
    bool mc = icap == BOARD_ICAP_ID;
    b.kind = mc ? BOARD_MULTICORE : BOARD_MONOLITH;
    b.has_handover = p->handover_valid();              // T112 Q1: a large .cor start fills the page for the monolith profile too
    b.can_return_host = mc;
    b.flash_cfg = !mc;                                 // DC13: the parameter; the standalone installation has no host to keep its settings
    if (mc) {
        snprintf(b.boot_cfg, sizeof(b.boot_cfg), "%s", FS_FLASH_BOOT_MULTICORE);
    }
    b.rom_dir = mc ? 0 : BOARD_MONOLITH_ROM_DIR;

    uint32_t fpga = p->fpga_caps();
    uint8_t caps = p->trx_caps();
    if (fpga & BOARD_FPGA_DRIVE_1541_1) {
        b.probes |= BC_DRIVE_A;
    }
    if (p->iec_version() == BOARD_IEC_VERSION) {
        b.probes |= BC_IEC;
    }
    if (caps & BOARD_TRX_CAPS_CART) {
        b.probes |= BC_CARTRIDGE;
    }
    b.probes |= BC_REU;                                  // on every C64 core of this line (ADR-0053 refuses it only while a cartridge forbids it)
    if ((caps & (BOARD_TRX_CAPS_GATE | BOARD_TRX_CAPS_STATEBUS | BOARD_TRX_CAPS_ENGINE)) ==
        (BOARD_TRX_CAPS_GATE | BOARD_TRX_CAPS_STATEBUS | BOARD_TRX_CAPS_ENGINE)) {
        b.probes |= BC_RING;
    }
    if (fpga & BOARD_FPGA_COMMAND_INTF) {
        b.probes |= BC_UCI;
    }
    if (!mc) {
        b.probes |= BC_SID_SOCKETS;                      // the T37 registers are write only: the profile answers, not a read
    }

    if (mc) {
        snprintf(b.why, sizeof(b.why), "ICAP 0x%02X, %s", icap, b.has_handover ? "hand-over valid" : "no valid hand-over");
    } else if (icap == 0) {
        snprintf(b.why, sizeof(b.why), "ICAP absent, %s", b.has_handover ? "hand-over valid" : "no hand-over");
    } else {
        snprintf(b.why, sizeof(b.why), "ICAP byte 0x%02X is not 0x%02X, %s", icap, BOARD_ICAP_ID, b.has_handover ? "hand-over valid" : "no hand-over");
    }
    return b;
}

static bool name_ok(const char *s, size_t max)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > max) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char ch = s[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
            return false;
        }
    }
    return true;
}

bool board_set_core(BoardProfile *b, const char *core_id, const char *folder)
{
    if (b->kind != BOARD_MONOLITH) {
        return true;
    }
    b->cfg_dir[0] = 0;
    b->boot_cfg[0] = 0;
    fs_flash_set_exceptions(0, 0);
    if (!core_id || !core_id[0]) {
        return true;
    }
    if (!name_ok(core_id, BOARD_CORE_ID_MAX)) {
        return false;
    }
    char cfg[FS_FLASH_EXC_LEN] = "";
    bool fits = true;
    snprintf(b->cfg_dir, sizeof(b->cfg_dir), "/flash/%s", core_id);
    snprintf(b->boot_cfg, sizeof(b->boot_cfg), "/flash/%s/boot.cfg", core_id);
    if (b->flash_cfg && name_ok(folder, 63)) {
        int n = snprintf(cfg, sizeof(cfg), "%s/%s.cfg", b->cfg_dir, folder);
        if (n < 0 || n >= (int)sizeof(cfg)) {       // a truncated path would register another file than the one the settings are written to
            cfg[0] = 0;
            fits = false;
        }
    }
    return fs_flash_set_exceptions(b->boot_cfg, cfg) && fits;
}

const char *board_name(const BoardProfile &b)
{
    return b.kind == BOARD_MONOLITH ? "monolith" : "multicore";
}

static const struct { const char *name; uint32_t bit; } k_caps[] = {
    { "drive_a", BC_DRIVE_A }, { "iec", BC_IEC }, { "cartridge", BC_CARTRIDGE }, { "reu", BC_REU },
    { "ring", BC_RING }, { "uci", BC_UCI }, { "sid_sockets", BC_SID_SOCKETS },
};

int board_effective_caps(const BoardProfile &b, const char *const *in, int nin, const char **out, int outmax, char *struck, int struck_len)
{
    int n = 0;
    if (struck && struck_len > 0) {
        struck[0] = 0;
    }
    for (int i = 0; i < nin; i++) {
        bool keep = true;
        for (unsigned k = 0; k < sizeof(k_caps) / sizeof(k_caps[0]); k++) {
            if (!strcmp(in[i], k_caps[k].name)) {
                keep = (b.probes & k_caps[k].bit) != 0;
            }
        }
        if (keep) {
            if (n < outmax) {
                out[n++] = in[i];
            }
        } else if (struck && struck_len > 0) {
            size_t l = strlen(struck);
            snprintf(struck + l, struck_len - l, "%s%s", l ? " " : "", in[i]);
        }
    }
    return n;
}
