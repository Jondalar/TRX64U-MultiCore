// The C64U's hardware configuration from the core's settings (u64cfg.h). PROVENANCE: vendor/gideon/software/u64/u64_config.cc (Gideon Zweijtzer, GPL-3.0):
// U64Config::effectuate_settings, setMixer, setSpeakerMixer, U64SidSockets/U64UltiSids/U64SidAddressing::effectuate_settings, setFilter, setSidEmuParams,
// setCpuSpeed, setLedSelector, auto_mirror, SetResampleFilter, the palette programming; the register addresses are vendor/gideon/software/system/u64.h.
// What is different: the values come from this application's settings (menu.toml groups video, audio, sid, input, expert) instead of a ConfigStore, and
// the picture's HDMI mode and the system mode go through the host's video_apply() (app/video/videomode.cc), which keeps the pixel layer's frame.
#include "u64cfg.h"
#include "writeback.h"
#include "engines.h"
#include "ops_glue.h"
#include "hw.h"
#include "u64.h"
#include "iomap.h"
#include "color_timings.h"
#include "videomode.h"
#include "picture.h"
#include "sid_decide.h"
#include "i2c_drv.h"
extern "C" {
#include "resid8_filter.h"
#include "trx_io.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern "C" void SetVideoPll(t_video_mode mode, int ppm);
extern "C" void ResetHdmiPll(void);

#include "u64_resample.inc"

// ---- the settings this file reads: id, key (the menu.toml `key`), found once per Settings object -------------------------------------------------
#define KEYS(X)                                                                                                                                      \
    X(SYSMODE, "System Mode") X(SCANLINES, "HDMI Scan lines") X(ANALOG, "Analog Video Mode") X(DIGITAL, "Digital Video Mode")                          \
    X(PPM, "Adjust Color Clock") X(PALETTE, "Palette Definition")                                                                                    \
    X(MASTER, "Vol Master")                                                                                                                          \
    X(VOL0, "Vol UltiSid 1") X(VOL1, "Vol UltiSid 2") X(VOL2, "Vol Socket 1") X(VOL3, "Vol Socket 2") X(VOL4, "Vol Sampler L") X(VOL5, "Vol Sampler R")  \
    X(VOL6, "Vol Drive 1")                                                                                                                           \
    X(PAN0, "Pan UltiSID 1") X(PAN1, "Pan UltiSID 2") X(PAN2, "Pan Socket 1") X(PAN3, "Pan Socket 2") X(PAN4, "Pan Sampler L") X(PAN5, "Pan Sampler R") \
    X(PAN6, "Pan Drive 1")                                                                                                                           \
    X(SPK_EN, "Speaker Enable")                                                                                                                      \
    X(SPK0, "Spk Vol UltiSid 1")                                                                                                                     \
    X(S1_EN, "SID Socket 1") X(S2_EN, "SID Socket 2") X(S1_SHUNT, "SID Socket 1 1K Ohm Resistor") X(S2_SHUNT, "SID Socket 2 1K Ohm Resistor")          \
    X(S1_CAPS, "SID Socket 1 Capacitors") X(S2_CAPS, "SID Socket 2 Capacitors")                                                                      \
    X(S1_ADDR, "SID Socket 1 Address") X(S2_ADDR, "SID Socket 2 Address") X(STEREO, "Ext DualSID Range Split")                                       \
    X(U1_ADDR, "UltiSID 1 Address") X(U2_ADDR, "UltiSID 2 Address") X(USPLIT, "UltiSID Range Split") X(PADDLE, "Paddle Override")                    \
    X(MIRROR, "Auto Address Mirroring")                                                                                                              \
    X(U1_FILTER, "UltiSID 1 Filter Curve") X(U2_FILTER, "UltiSID 2 Filter Curve") X(U1_RES, "UltiSID 1 Filter Resonance")                            \
    X(U2_RES, "UltiSID 2 Filter Resonance") X(U1_WAVES, "UltiSID 1 Combined Waveforms") X(U2_WAVES, "UltiSID 2 Combined Waveforms")                  \
    X(U1_DIGI, "UltiSID 1 Digis Level") X(U2_DIGI, "UltiSID 2 Digis Level")                                                                          \
    X(U1_FON, "UltiSID 1 Filter") X(U2_FON, "UltiSID 2 Filter") X(U1_BASE, "UltiSID 1 6581 Base") X(U2_BASE, "UltiSID 2 6581 Base")                  \
    X(U1_OFFS, "UltiSID 1 6581 Offset") X(U2_OFFS, "UltiSID 2 6581 Offset")                                                                          \
    X(JOY, "Joystick Input") X(USERPORT, "UserPort Power Enable") X(TURBO, "Turbo Control") X(SPEED, "CPU Speed") X(BADLINE, "Badline Timing")        \
    X(SUPERCPU, "SuperCPU Detect (D0BC)") X(IECMODE, "Serial Bus Mode") X(PARCABLE, "SpeedDOS Parallel Cable") X(BURST, "Burst Mode Patch")          \
    X(LED0, "LED Select Top") X(LED1, "LED Select Bot")                                                                                              \
    X(MOUSEMODE, "Mouse Mode") X(MOUSESENS, "Mouse Sensitivity") X(MOUSEACC, "Mouse Acceleration") X(WHEELSENS, "Mouse Wheel Sensitivity")          \
    X(WHEELDIR, "Mouse Wheel Direction")

enum {
#define X(i, k) K_##i,
    KEYS(X)
#undef X
    K_N
};
static const char *const key_names[K_N] = {
#define X(i, k) k,
    KEYS(X)
#undef X
};

static const Settings *g_set;
static int16_t g_ix[K_N];

static void resolve(CoreCtx *c)
{
    if (g_set == c->settings) {
        return;
    }
    g_set = c->settings;
    for (int i = 0; i < K_N; i++) {
        g_ix[i] = (int16_t)(c->settings ? c->settings->find(key_names[i]) : -1);
        if (g_ix[i] < 0) {
            printf("[u64cfg] the menu has no setting \"%s\"; its default is used\n", key_names[i]);
        }
    }
}

// The index of the value of a setting (an enum's position), `def` when the menu has no such item. A range setting (mouse sensitivity) is value + base.
static int V(CoreCtx *c, int id, int def = 0)
{
    resolve(c);
    return g_ix[id] >= 0 ? c->settings->value(g_ix[id]) : def;
}

// ---- video ---------------------------------------------------------------------------------------------------------------------------------
static const uint8_t default_colors[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0xF7, 0xF7, 0xF7 }, { 0x8D, 0x2F, 0x34 }, { 0x6A, 0xD4, 0xCD }, { 0x98, 0x35, 0xA4 }, { 0x4C, 0xB4, 0x42 },
    { 0x2C, 0x29, 0xB1 }, { 0xEF, 0xEF, 0x5D }, { 0x98, 0x4E, 0x20 }, { 0x5B, 0x38, 0x00 }, { 0xD1, 0x67, 0x6D }, { 0x4A, 0x4A, 0x4A },
    { 0x7B, 0x7B, 0x7B }, { 0x9F, 0xEF, 0x93 }, { 0x6D, 0x6A, 0xEF }, { 0xB2, 0xB2, 0xB2 } };

// Gideon U64Config::rgb_to_yuv (the matrix in 0.14 format, Y scaled so that white is 0xB6)
static void rgb_to_yuv(const uint8_t rgb[3], uint8_t yuv[3])
{
    const int Y[3] = { 3496, 6864, 1333 };
    const int U[3] = { -2408, -4735, 7143 };
    const int V[3] = { 10076, -8438, -1638 };
    int y = Y[0] * rgb[0] + Y[1] * rgb[1] + Y[2] * rgb[2];
    int u = U[0] * rgb[0] + U[1] * rgb[1] + U[2] * rgb[2];
    int v = V[0] * rgb[0] + V[1] * rgb[1] + V[2] * rgb[2];
    y >>= 14;
    u >>= 14;
    v >>= 14;
    if (u > 127) { v *= 127; v /= u; u = 127; }
    if (u < -127) { v *= -127; v /= u; u = -127; }
    if (v > 127) { u *= 127; u /= v; v = 127; }
    if (v < -127) { u *= -127; u /= v; v = -127; }
    yuv[0] = (uint8_t)y;
    yuv[1] = (uint8_t)u;
    yuv[2] = (uint8_t)v;
}

static uint8_t g_palette[16][3];
static bool g_palette_valid;
static char g_palette_name[64];

static void program_palette(const uint8_t rgb[16][3])
{
    memcpy(g_palette, rgb, sizeof(g_palette));
    g_palette_valid = true;
    volatile uint8_t *rgbr = (volatile uint8_t *)C64_PALETTE;
    volatile uint8_t *yuvr = (volatile uint8_t *)(C64_PALETTE + 0x400);
    volatile uint8_t *hdmi = (volatile uint8_t *)U64II_HDMI_PALETTE;
    for (int i = 0; i < 16; i++) {
        uint8_t yuv[3];
        rgb_to_yuv(rgb[i], yuv);
        for (int k = 0; k < 3; k++) {
            rgbr[4 * i + k] = rgb[i][k];
            hdmi[4 * i + k] = rgb[i][k];
            yuvr[4 * i + k] = yuv[k];
        }
    }
}

// A .vpl (Gideon FileTypePalette::parseVplFile): lines of three hex numbers r g b, '#' starts a comment; 16 of them.
static bool parse_vpl(const char *text, int n, uint8_t rgb[16][3])
{
    int idx = 0, p = 0;
    while (p < n && idx < 16) {
        char line[80];
        int l = 0;
        while (p < n && text[p] != '\n') {
            if (text[p] != '\r' && l < (int)sizeof(line) - 1) {
                line[l++] = text[p];
            }
            p++;
        }
        p++;
        line[l] = 0;
        char *hash = strchr(line, '#');
        if (hash) {
            *hash = 0;
        }
        unsigned r, g, b;
        if (!line[strspn(line, " \t")]) {
            continue;
        }
        if (sscanf(line, "%x %x %x", &r, &g, &b) != 3) {
            return false;
        }
        rgb[idx][0] = (uint8_t)r;
        rgb[idx][1] = (uint8_t)g;
        rgb[idx][2] = (uint8_t)b;
        idx++;
    }
    return idx == 16;
}

static const char *palette_dir(void) { return "/flash/data"; }

// 0 on success; the file by name from the data folder of the flash, or a full path.
static int load_palette(CoreCtx *c, const char *name, char *msg, int msglen)
{
    uint8_t rgb[16][3];
    char path[200];
    if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name);
    } else {
        snprintf(path, sizeof(path), "%s/%s", palette_dir(), name);
    }
    static char buf[8200];
    uint64_t size = 0;
    bool dir = false;
    if (!c->fs->stat(path, &size, &dir) || dir || size > 8192 || size < 8) {
        snprintf(msg, msglen, "%s: no palette file (8 bytes to 8 KB)", path);
        return 1;
    }
    int got = c->fs->load(path, buf, sizeof(buf));
    if (got < 0 || !parse_vpl(buf, got, rgb)) {
        snprintf(msg, msglen, "%s: not a palette (16 lines of three hex numbers)", path);
        return 1;
    }
    program_palette(rgb);
    snprintf(g_palette_name, sizeof(g_palette_name), "%s", name);
    return 0;
}

// Gideon U64Config::SetResampleFilter
static void set_resample_filter(t_video_mode m)
{
    const t_video_color_timing *ct = color_timings[(int)m];
    int mode = (ct->audio_div == 77) ? 0 : 1;
    const uint32_t *pf = (mode == 0) ? (const uint32_t *)c_filter2_coef_pal_alt : (const uint32_t *)c_filter2_coef_ntsc;
    int size = (mode == 0) ? 675 : 512;
    U64_RESAMPLE_RESET = 1;
    U64_RESAMPLE_FLUSH = 1;
    for (int i = 0; i < size; i++) {
        U64_RESAMPLE_DATA = *(pf++);
    }
    U64_RESAMPLE_LABOR = (mode == 0) ? 45 : 169;
    U64_RESAMPLE_FLUSH = 0;
}

static int g_sys = -1, g_ppm = 0;                 // the host brought the picture up with no offset

static int fx_video(CoreCtx *c, const OpArgs &, OpOut &o)
{
    int sys = V(c, K_SYSMODE);
    C64_SCANLINES = (uint8_t)V(c, K_SCANLINES, 1);
    int dig = V(c, K_DIGITAL);
    U64_HDMI_ENABLE = dig == 2 ? 0 : 1;               // Auto: the screen is HDMI unless the user says DVI (the EDID tells nothing a DVI monitor does not also say)
    uint8_t format = V(c, K_ANALOG) ? VIDEO_FMT_RGB_OUTPUT : 0;
    const t_video_color_timing *ct = color_timings[sys];
    C64_PHASE_INCR = ct->phase_inc;
    int ppm = V(c, K_PPM, 100) - 100;                 // the range -100 .. 100 ppm is stored as the index 0 .. 200
    C64_VIDEOFORMAT = (uint8_t)(ct->mode_bits | format);
    if (sys != g_sys) {
        printf("[u64cfg] system mode %s\n", video_sys_name(sys));
        picture_set_system(c, sys);                   // the picture layer takes the new family: its mode and the Picture row are picked again (FW u64_config.cc:1471-1508)
        g_ppm = 0;
        g_sys = sys;
        set_resample_filter((t_video_mode)sys);
    }
    if (ppm != g_ppm) {
        SetVideoPll((t_video_mode)sys, ppm);          // the colour clock's offset goes with the PLL setting (Gideon's setPllOffset is empty)
        ResetHdmiPll();
        g_ppm = ppm;
    }
    // the palette: the file named in the setting, else the default colours
    resolve(c);
    const char *pal = g_ix[K_PALETTE] >= 0 ? c->settings->text(g_ix[K_PALETTE]) : "";
    while (*pal == ' ') {
        pal++;
    }
    if (*pal) {
        if (strcmp(pal, g_palette_name)) {
            if (load_palette(c, pal, o.msg, o.msglen)) {
                program_palette(default_colors);
                g_palette_name[0] = 0;
                return OPE_FAILED;
            }
        }
    } else if (!g_palette_valid || g_palette_name[0]) {
        program_palette(default_colors);
        g_palette_name[0] = 0;
    }
    return OPE_OK;
}

static int op_palette_apply(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    return load_palette(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// "Set as Default" (a .vpl in the Library): the palette is applied and its name is the setting, so every start takes it
static int op_palette_default(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (load_palette(c, a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    OpIn in[2] = { in_str("key", "Palette Definition"), in_str("value", a.s(0)) };
    OpResult r;
    ops_call(c, "settings.set", in, 2, &r);
    snprintf(o.msg, o.msglen, "Palette %s is the default", a.s(0));
    return OPE_OK;
}

static const char *state_palette(CoreCtx *)
{
    return g_palette_name[0] ? g_palette_name : "Default colours";
}

// ---- audio ---------------------------------------------------------------------------------------------------------------------------------
static const uint8_t volume_ctrl[] = {
    0x00, 0x01, 0x02, 0x04, 0x06, 0x08, 0x10, 0x12, 0x14, 0x17, 0x1a, 0x1d, 0x20, 0x24, 0x28, 0x2d,
    0x33, 0x39, 0x40, 0x48, 0x51, 0x5b, 0x66, 0x72, 0x80, 0x90, 0xa1, 0xb5, 0xcb, 0xe4, 0xff };
static const uint16_t pan_ctrl[] = { 0, 40, 79, 116, 150, 181, 207, 228, 243, 253, 256 };

// source x master x pan in one step (Gideon mixer_side): a side panned away or OFF stays 0, anything else is at least 1
static uint8_t mixer_side(uint8_t source_raw, uint8_t master_raw, uint16_t pan)
{
    if (!source_raw || !master_raw || !pan) {
        return 0;
    }
    uint32_t v = ((uint32_t)source_raw * master_raw * pan + (1u << 14)) >> 15;
    if (v > 0xFF) {
        v = 0xFF;
    }
    return v ? (uint8_t)v : 1;
}

// the channels Gideon's mixer has that this board has no source for (drive 2, tape read, tape write): his defaults
static const uint8_t spare_vol[3] = { 11, 4, 4 };
static const uint8_t spare_pan[3] = { 7, 5, 5 };

static void write_mixer(CoreCtx *c)
{
    volatile uint8_t *mixer = (volatile uint8_t *)U64_AUDIO_MIXER;
    uint8_t master = volume_ctrl[V(c, K_MASTER, 24)];
    for (int i = 0; i < 10; i++) {
        int vol = i < 7 ? V(c, K_VOL0 + i, i == 6 ? 11 : 24) : spare_vol[i - 7];
        int pan = i < 7 ? V(c, K_PAN0 + i, 5) : spare_pan[i - 7];
        uint8_t src = volume_ctrl[vol];
        *(mixer++) = mixer_side(src, master, pan_ctrl[10 - pan]);      // right
        *(mixer++) = mixer_side(src, master, pan_ctrl[pan]);           // left
    }
}

// FW u64_config.cc:1445-1447 (U2PIO_SPEAKER_EN b0) and setSpeakerMixer :1749-1765. The bitstream decodes ONE gain for the whole mono mix
// (0x10100540/41/44/45), so the one speaker volume goes to all ten pairs; the per-source volumes of the FW are not decoded and stay out.
static void write_speaker(CoreCtx *c)
{
    volatile uint8_t *mixer = (volatile uint8_t *)U64_SPEAKER_MIXER;
    bool on = V(c, K_SPK_EN, 1) != 0;
    U2PIO_SPEAKER_EN = on ? 1 : 0;
    uint8_t vol = on ? volume_ctrl[V(c, K_SPK0, 0)] : 0;
    for (int i = 0; i < 20; i++) {
        *(mixer++) = vol;
    }
}

static int fx_audio(CoreCtx *c, const OpArgs &, OpOut &)
{
    write_mixer(c);
    write_speaker(c);
    return OPE_OK;
}

void u64cfg_mute_sids(void)
{
    volatile uint8_t *mixer = (volatile uint8_t *)U64_AUDIO_MIXER;
    for (int i = 0; i < 8; i++) {
        mixer[i] = 0;
    }
    mixer = (volatile uint8_t *)U64_SPEAKER_MIXER;
    for (int i = 0; i < 8; i++) {
        mixer[i] = 0;
    }
}

void u64cfg_restore_mixer(CoreCtx *c)
{
    write_mixer(c);
    write_speaker(c);
}

// ---- SID sockets, addressing and the UltiSIDs ----------------------------------------------------------------------------------------------
static const uint8_t sid_offsets[] = {
    0x01, 0x40, 0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C, 0x5E,
    0x60, 0x62, 0x64, 0x66, 0x68, 0x6A, 0x6C, 0x6E, 0x70, 0x72, 0x74, 0x76, 0x78, 0x7A, 0x7C, 0x7E,
    0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE, 0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE };
static const uint8_t stereo_bits[] = { 0x00, 0x02, 0x04, 0x08, 0x10, 0x20 };
static const uint8_t split_bits[] = { 0x00, 0x02, 0x04, 0x08, 0x10, 0x06, 0x12, 0x18 };

// the SID found in each socket by the last detection (u64cfg_detect): 0 none, 1 6581, 2 8580, 3 ...
static int g_sid_type[2];

// Gideon U64Config::auto_mirror: A5..A9 are don't care where all the decodes in D400-D7FF agree, so the space fills with mirrors without new overlaps
static void auto_mirror(uint8_t *base, uint8_t *mask, uint8_t *split, int count)
{
    for (int i = 0; i < 4; i++) {
        base[i] &= ~split[i];
        mask[i] &= ~split[i];
    }
    for (int a = 5; a <= 9; a++) {
        bool same = true, set = false;
        int bit = 0, temp;
        for (int i = 0; i < count; i++) {
            if ((base[i] < 0x40) || (base[i] >= 0x80)) {
                continue;
            }
            if (!set) {
                bit = (((base[i] >> (a - 4)) & 1) << 1) | ((mask[i] >> (a - 4)) & 1);
                set = true;
            } else {
                temp = (((base[i] >> (a - 4)) & 1) << 1) | ((mask[i] >> (a - 4)) & 1);
                if (temp != bit) {
                    same = false;
                }
            }
        }
        if (same) {
            for (int i = 0; i < count; i++) {
                if ((base[i] < 0x40) || (base[i] >= 0x80)) {
                    continue;
                }
                mask[i] &= ~(1 << (a - 4));
                base[i] &= ~(1 << (a - 4));
            }
        }
    }
}

// the resulting decodes, for the address map view (cfg state "u64.sid_map")
static uint8_t g_base[4], g_mask[4], g_split[4];

static void write_sid_addressing(CoreCtx *c)
{
    uint8_t base[4], mask[4], split[4];
    base[0] = sid_offsets[V(c, K_S1_ADDR, 1)];
    base[1] = sid_offsets[V(c, K_S2_ADDR, 1)];
    base[2] = sid_offsets[V(c, K_U1_ADDR, 1)];
    base[3] = sid_offsets[V(c, K_U2_ADDR, 1)];
    mask[0] = mask[1] = mask[2] = mask[3] = 0xFE;
    split[0] = split[1] = stereo_bits[V(c, K_STEREO)];
    split[2] = split[3] = split_bits[V(c, K_USPLIT)];
    for (int i = 0; i < 4; i++) {                                   // fix_splits
        base[i] &= ~split[i];
        mask[i] &= ~split[i];
    }
    if (V(c, K_MIRROR, 1)) {
        auto_mirror(base, mask, split, 4);
    }
    C64_SID1_BASE = base[0];
    C64_SID2_BASE = base[1];
    C64_EMUSID1_BASE = base[2];
    C64_EMUSID2_BASE = base[3];
    C64_SID1_MASK = mask[0];
    C64_SID2_MASK = mask[1];
    C64_EMUSID1_MASK = mask[2];
    C64_EMUSID2_MASK = mask[3];
    C64_STEREO_ADDRSEL = (uint8_t)V(c, K_STEREO);
    C64_EMUSID_SPLIT = (uint8_t)V(c, K_USPLIT);
    memcpy(g_base, base, 4);
    memcpy(g_mask, mask, 4);
    memcpy(g_split, split, 4);
}

// ---- the SID in a socket (FW u64_config.cc U64SidSockets::detect, detectFPGASID, detectRemakes, S_SidDetector, DetectSidImpl) ---------------------
// Run once at the start, as FW runs it in U64Config's constructor, on the machine let out of reset and stopped, over the DMA window. Types as sid_type_names:
// 1 6581, 2 8580, 3 FPGASID, 4 SwinSID, 5 ARMSID, 6 ARM2SID, 9 PDsid, 10 SIDKick, 11 SIDKick Pico. Not ported: the Dukestah adapter probe (type 8): FW follows it with
// the FPGASID device's effectuate_settings (u64_config.cc:773-774, sid_device_fpgasid.cc), which this application does not have; the probe alone would leave the chip in the
// state of its writes.
#define SDMA(a) (*(volatile uint8_t *)(C64_MEMORY_BASE + (a)))

static void detect_addresses(CoreCtx *c)
{
    C64_SID1_BASE = 0x40;                                             // FW S_SetupDetectionAddresses: socket 1 at $D400, socket 2 at $D500, the UltiSIDs out of the way
    C64_SID2_BASE = 0x50;
    C64_SID1_MASK = 0xF0;
    C64_SID2_MASK = 0xF0;
    C64_EMUSID1_BASE = 0x60;
    C64_EMUSID2_BASE = 0x60;
    C64_EMUSID1_MASK = 0xFE;
    C64_EMUSID2_MASK = 0xFE;
    C64_SID1_EN = 1;
    C64_SID2_EN = 1;
    C64_STEREO_ADDRSEL = 0;
    c->io->delay_ms(1);
}

static int detect_fpgasid(volatile uint8_t *b)
{
    b[25] = 0xEE;
    b[26] = 0xAB;
    uint8_t id1 = b[0], id2 = b[1];
    b[25] = 0x00;
    b[26] = 0x00;
    return (id1 == 0x1D && id2 == 0xF5) ? 3 : 0;
}

static int detect_remakes(CoreCtx *c, volatile uint8_t *b)
{
    CoreIo *io = c->io;
    b[29] = 'S'; io->delay_us(10);
    b[30] = 'I'; io->delay_us(10);
    b[31] = 'D'; io->delay_us(10);
    io->delay_ms(10);
    uint8_t id1 = b[27];
    io->delay_us(10);
    uint8_t id2 = b[28];
    io->delay_us(10);
    if (id1 == 'S' && id2 == 'W') {
        b[29] = 0;
        return 4;
    }
    if (id1 == 'N' && id2 == 'O') {
        b[31] = 'I'; io->delay_us(10);
        b[30] = 'I'; io->delay_us(10);
        io->delay_ms(10);
        uint8_t l = b[27];
        io->delay_us(10);
        b[29] = 0;
        io->delay_us(10);
        return (l == 'L' || l == 'R') ? 6 : 5;
    }
    b[29] = 'P';
    (void)SDMA(2);
    b[30] = 'D';
    (void)SDMA(2);
    if (b[30] == 'S') {
        return 9;
    }
    uint8_t v[32];
    b[0x1f] = 0xff;
    for (int i = 0; i < 32; i++) {
        b[0x1e] = (uint8_t)(224 + i);
        v[i] = b[0x1d];
    }
    b[0x1f] = 0xfd;
    static const uint8_t pico[] = { 0x53, 0x4b, 0x10, 0x09, 0x03, 0x0f };
    static const uint8_t teen[] = { 0x53, 0x49, 0x44, 0x4b, 0x09, 0x03, 0x0b };
    if (!memcmp(v, pico, sizeof(pico))) {
        return 11;
    }
    if (!memcmp(v, teen, sizeof(teen))) {
        return 10;
    }
    return 0;
}

// OSC3 of both sockets: voltage-independent 6581 / 8580 test. buf[0..15] socket 1 first read, 16..31 second, 32.. socket 2 (DetectSidImpl)
static bool detect_osc3(uint8_t *buf)
{
    uint32_t timeout = 1000000;
    while (SDMA(0xD012) != 0xFF) {
        if (!timeout--) {
            return false;
        }
    }
    for (int x = 0; x < 16; x++) {
        for (int s = 0; s < 2; s++) {
            uint16_t o = (uint16_t)(0xD400 + 0x100 * s);
            SDMA(o + 0x12) = 0x48;
            SDMA(o + 0x0F) = 0x48;
            SDMA(o + 0x12) = 0x24;
            for (int i = 0; i < 3; i++) { SDMA(o + 0x1F) = 0; }
            buf[32 * s + x] = SDMA(o + 0x1B);
            for (int i = 0; i < 4; i++) { SDMA(o + 0x1F) = 0; }
            buf[32 * s + 16 + x] = SDMA(o + 0x1B);
        }
    }
    return true;
}

static void detect_sids(CoreCtx *c, int type[2])
{
    c->io->ram_memonly(false);                                        // the SIDs are on the bus, not in the RAM behind them
    type[0] = type[1] = 0;
    for (int s = 0; s < 2; s++) {
        detect_addresses(c);
        type[s] = detect_fpgasid((volatile uint8_t *)(C64_MEMORY_BASE + 0xD400 + 0x100 * s));
    }
    if (!type[0] || !type[1]) {
        c->io->delay_ms(100);
        for (int s = 0; s < 2; s++) {
            detect_addresses(c);
            if (!type[s]) {
                type[s] = detect_remakes(c, (volatile uint8_t *)(C64_MEMORY_BASE + 0xD400 + 0x100 * s));
            }
        }
    }
    if (!type[0] || !type[1]) {
        detect_addresses(c);
        uint8_t buf[64];
        for (int attempt = 0; attempt < 3; attempt++) {
            int r[2] = { 0, 0 };
            if (!detect_osc3(buf)) {
                break;
            }
            bool ok = true;
            for (int s = 0; s < 2; s++) {
                r[s] = sid_osc3_type(buf + 32 * s);
                ok &= r[s] != 0 || type[s] != 0;                      // FW: a socket that answered nothing sane gives the next attempt
            }
            for (int s = 0; s < 2; s++) {
                if (r[s] && !type[s]) {
                    type[s] = r[s];
                }
            }
            if (ok) {
                break;
            }
        }
    }
    printf("[u64cfg] SID detect: socket 1 = %d, socket 2 = %d\n", type[0], type[1]);
}

#define STOPREG(o) (*(volatile uint8_t *)(C64_CARTREGS_BASE + (o)))
static const char *const type_keys[2] = { "Pad SID Detected Socket 1", "Pad SID Detected Socket 2" };   // the settings keep only "Pad " extras (Settings::extra_set)
static bool g_detected;

// FW U64SidSockets::detect and its tail: the machine is let out of reset and stopped, the chips are looked at, the type is stored, and a changed type
// sets the socket's enable, caps and shunt (sid_type_change). The machine goes back into reset: boot_start's init holds it until the release. The
// supply of a 6581 is only ever raised for a type 1 (write_sid_sockets) that the read-outs agreed on, and a new one switches its socket off. If the
// machine does not stop in time nothing is written to a chip and every type stays 0 (FW would have used the stored type; a swapped chip must never
// keep a 6581's supply). The type is stored after FW's CFG_SID1_TYPE (the settings file), and compared with the one of the last start.
static void sid_types_detect(CoreCtx *c)
{
    if (g_detected || c->started || c->frozen || !c->settings) {
        return;
    }
    g_detected = true;
    CoreIo *io = c->io;
    io->mode_reset(false);                                            // C64_MODE_UNRESET
    uint32_t t0 = io->now_ms();
    while ((STOPREG(3) & 0x10) && io->now_ms() - t0 < 500) {        // while c64_reset_detect()
    }
    STOPREG(2) = 0x02;                                                // C64_STOP_MODE = STOP_COND_FORCE
    STOPREG(1) = 0x01;                                                // C64_STOP
    t0 = io->now_ms();
    while (!(STOPREG(1) & 0x02) && io->now_ms() - t0 < 500) {       // C64_HAS_STOPPED
    }
    int t[2] = { 0, 0 };
    bool stopped = (STOPREG(1) & 0x02) != 0;
    if (stopped) {
        detect_sids(c, t);
    } else {
        printf("[u64cfg] SID detect: the machine did not stop, no chip was touched\n");
    }
    io->stop(false);
    io->mode_reset(true);
    if (!stopped) {
        return;
    }
    char note[80] = "";
    for (int s = 0; s < 2; s++) {
        g_sid_type[s] = t[s];
        char v[8];
        int old = c->settings->extra_get(type_keys[s], v, sizeof(v)) ? atoi(v) : 0;
        if (t[s] == old) {
            continue;
        }
        snprintf(v, sizeof(v), "%d", t[s]);
        c->settings->extra_set(type_keys[s], v);
        SidChange ch = sid_type_change(t[s]);
        resolve(c);
        int ix[3] = { g_ix[s ? K_S2_EN : K_S1_EN], g_ix[s ? K_S2_CAPS : K_S1_CAPS], g_ix[s ? K_S2_SHUNT : K_S1_SHUNT] };
        int val[3] = { ch.enable, ch.caps, ch.shunt };
        for (int k = 0; k < 3; k++) {
            if (val[k] >= 0 && ix[k] >= 0) {
                c->settings->set(ix[k], val[k]);                      // the start, before the executor: one task, no menu yet; a write of the settings file at most
            }
        }
        snprintf(note, sizeof(note), "SID changed. Please review settings");
    }
    if (note[0] && !c->notice_pending) {
        snprintf(c->notice, sizeof(c->notice), "%s", note);
        c->notice_pending = true;
    }
}

static void write_sid_sockets(CoreCtx *c)
{
    sid_types_detect(c);
    uint8_t ctrl = 0;
    for (int s = 0; s < 2; s++) {
        int typ = g_sid_type[s];
        uint8_t shu = (uint8_t)V(c, s ? K_S2_SHUNT : K_S1_SHUNT);
        uint8_t cap = (uint8_t)(1 - V(c, s ? K_S2_CAPS : K_S1_CAPS));
        uint8_t reg = 0;
        if (V(c, s ? K_S2_EN : K_S1_EN)) {
            reg = typ == 1 ? 3 : 2;
        }
        uint8_t value = (uint8_t)(reg | (shu << 2) | (cap << 3));
        ctrl |= (uint8_t)(s ? value << 4 : value);
    }
    if (i2c) {
        i2c->i2c_lock("SID Configuration");
        i2c->set_channel(I2C_CHANNEL_1V8);
        i2c->i2c_write_byte(0x40, 0x01, ctrl);
        i2c->i2c_write_byte(0x40, 0x03, 0x00);
        i2c->i2c_unlock();
    }
    C64_SID1_EN = V(c, K_S1_EN) ? 1 : 0;
    C64_SID2_EN = V(c, K_S2_EN) ? 1 : 0;
}

// FW u64_config.cc setResid8 :577-594: the plan of resid8_filter.h (a copy of the FW header) -> trx_io 0x040-0x04C and the UltiSID RES / DIGI registers
static void write_ultisid(CoreCtx *c)
{
    for (int u = 0; u < 2; u++) {
        Resid8Plan p = resid8_plan(V(c, u ? K_U2_FILTER : K_U1_FILTER), V(c, u ? K_U2_RES : K_U1_RES), V(c, u ? K_U2_DIGI : K_U1_DIGI),
                                   V(c, u ? K_U2_BASE : K_U1_BASE, 245), V(c, u ? K_U2_OFFS : K_U1_OFFS, -780), V(c, u ? K_U2_FON : K_U1_FON) == 0);
        if (u == 0) {
            C64_EMUSID1_RES = p.res_reg;
            C64_EMUSID1_DIGI = p.digi_reg;
        } else {
            C64_EMUSID2_RES = p.res_reg;
            C64_EMUSID2_DIGI = p.digi_reg;
        }
        trx_io_resid8_filter(u, p.src, p.auto_bits, p.base, p.offset);
        trx_io_resid8_bypass(u, p.bypass != 0);
    }
    C64_EMUSID1_WAVES = (uint8_t)V(c, K_U1_WAVES);
    C64_EMUSID2_WAVES = (uint8_t)V(c, K_U2_WAVES);
}

static int fx_sid(CoreCtx *c, const OpArgs &, OpOut &)
{
    write_sid_sockets(c);
    write_sid_addressing(c);
    write_ultisid(c);
    C64_PADDLE_EN = (uint8_t)V(c, K_PADDLE, 1);
    return OPE_OK;
}

// "$D400 mirrored on ..." style lines of the decodes for the viewer: one row per decode with A9 .. A5 as Gideon's show_mapping prints them
static const char *state_sid_map_n(int i)
{
    static char line[4][64];
    static const char *const names[4] = { "Socket 1", "Socket 2", "UltiSID 1", "UltiSID 2" };
    int p = snprintf(line[i], sizeof(line[i]), "%s  ", names[i]);
    for (int a = 9 - 4; a >= 5 - 4; a--) {
        int m = (g_mask[i] >> a) & 1, b = (g_base[i] >> a) & 1;
        p += snprintf(line[i] + p, sizeof(line[i]) - p, "%s", m ? (b ? "1 " : "0 ") : (b ? "#! " : "- "));
    }
    snprintf(line[i] + p, sizeof(line[i]) - p, " $%02X00+$%02X", 0xD0 + (g_base[i] >> 4) * 1, (g_base[i] & 0x1F) * 2);
    return line[i];
}
static const char *state_sid_map0(CoreCtx *) { return state_sid_map_n(0); }
static const char *state_sid_map1(CoreCtx *) { return state_sid_map_n(1); }
static const char *state_sid_map2(CoreCtx *) { return state_sid_map_n(2); }
static const char *state_sid_map3(CoreCtx *) { return state_sid_map_n(3); }

static const char *const sid_type_names[] = { "None", "6581", "8580", "FPGASID", "SwinSID Ultimate", "ARMSID", "ARM2SID", "SidFx", "FPGASID Dukestah",
                                              "PDsid", "SIDKick (Teensy)", "SIDKick Pico" };
const char *u64cfg_sid_type_name(int type) { return type >= 0 && type < 12 ? sid_type_names[type] : "?"; }
static const char *state_sid1(CoreCtx *) { return u64cfg_sid_type_name(g_sid_type[0]); }
static const char *state_sid2(CoreCtx *) { return u64cfg_sid_type_name(g_sid_type[1]); }

// ---- the rest: joystick, user port, turbo, serial bus, LEDs ---------------------------------------------------------------------------------
static const uint8_t speedregs_regvalues[] = { 0x00, 0x00, 0x01, 0x05 };
static const uint8_t c_iec_connectors[] = { 0x70, 0x60, 0x30, 0x50 };      // 4: ext, 5: ult, 6: cia
static const uint8_t wasd_settings[] = { 0x00, 0x00, 0x01, 0x01, 0x03, 0x03 };
extern uint8_t keyb_joy_select;
extern uint8_t wasd_to_joy;

static int fx_misc(CoreCtx *c, const OpArgs &, OpOut &)
{
    C64_PADDLE_SWAP = (uint8_t)(V(c, K_JOY) & 1);
    int swap = V(c, K_JOY);
    U64II_KEYB_JOY = keyb_joy_select = (uint8_t)(swap & 1);
    MATRIX_WASD_TO_JOY = wasd_to_joy = wasd_settings[swap];
    uint8_t en = V(c, K_USERPORT, 1) ? 3 : 0;
    U64_USERPORT_EN = en;
    U64_PWM_DUTY = en ? 0xD8 : 0x00;
    // turbo (setCpuSpeed)
    uint8_t prefReg, turboRegs;
    int tc = V(c, K_TURBO);
    if (tc == 0) {
        prefReg = 0x80;
        turboRegs = 0;
    } else {
        prefReg = (uint8_t)(V(c, K_SPEED) | (V(c, K_BADLINE, 1) << 7));
        turboRegs = (uint8_t)(speedregs_regvalues[tc] + (V(c, K_SUPERCPU) ? 0x02 : 0x00));
    }
    C64_TURBOREGS_EN = turboRegs;
    C64_SPEED_PREFER = prefReg;
    C64_SPEED_UPDATE = 1;
    // serial bus, burst mode, the parallel cable
    U64_INT_CONNECTORS = (uint8_t)(V(c, K_PARCABLE) | (V(c, K_BURST) << 1) | c_iec_connectors[V(c, K_IECMODE)]);
    // LEDs
    U64_CASELED_SELECT = (uint8_t)((V(c, K_LED1, 4) << 4) | V(c, K_LED0, 0));
    return OPE_OK;
}

// the mouse and wheel items are read by usb_hid.cc through this (weak there): the value of Gideon's item numbers CFG_MOUSE_* (usb_hid_config.h)
extern "C" int u64_get_usb_hid_config_value(int key, int default_value);
static CoreCtx *g_ctx;
extern "C" int u64_get_usb_hid_config_value(int key, int default_value)
{
    int v;
    if (mouse_sink_scaling(key, &v)) {                           // T190: a core with a mouse sink and its own settings (Amiga): mouse.sens / mouse.accel
        return v;
    }
    if (!g_ctx || !g_ctx->settings) {
        return default_value;
    }
    switch (key) {
    case 0x55: return V(g_ctx, K_MOUSEMODE, 1);                  // CFG_MOUSE_MODE
    case 0x56: return V(g_ctx, K_WHEELSENS, 7) + 1;              // CFG_SCROLL_FACTOR 1..16
    case 0x57: return V(g_ctx, K_WHEELDIR);                      // CFG_WHEEL_DIRECTION
    case 0x58: return 0; // CFG_MENU_MOUSE_NAV: never. Our menus are keys only (T104.1, Alex 2026-10-29: "menumouse raus"); the driver queues no menu key from the mouse
    case 0x5D: return V(g_ctx, K_MOUSESENS, 7) + 1;              // CFG_MOUSE_SENSITIVITY 1..16
    case 0x5E: return V(g_ctx, K_MOUSEACC);                      // CFG_MOUSE_ACCELERATION
    }
    return default_value;
}

static int fx_input(CoreCtx *c, const OpArgs &, OpOut &)
{
    g_ctx = c;                                                   // the values are read when the report comes in
    return OPE_OK;
}

// ---- power -----------------------------------------------------------------------------------------------------------------------------------
// The U64-II has no power register (U64_POWER_REG is the first U64's): power goes through the ESP32 control module (Gideon c64_subsys.cc MENU_C64_POWEROFF,
// wifi_machine_off() / wifi_machine_reboot(): rpc_calls.h CMD_MACHINE_OFF 0x0F, CMD_MACHINE_REBOOT 0x13). The module answers nothing, the machine is gone.
#define ESP_CMD_MACHINE_OFF    0x0F
#define ESP_CMD_MACHINE_REBOOT 0x13
static int op_power_off(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (media_writeback(c, WB_WHY_POWER, o.msg, o.msglen)) {         // T154, T180 D4: the media first; one that cannot be written back keeps the machine on
        return OPE_FAILED;
    }
    int rc = hw_esp32_rpc(ESP_CMD_MACHINE_OFF, 0, 0, 0, 0, 0);
    if (rc < 0) {
        snprintf(o.msg, o.msglen, "The ESP32 control module is not there (no link)");
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Power off sent to the control module");
    return OPE_OK;
}
static int op_power_cycle(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (media_writeback(c, WB_WHY_POWER, o.msg, o.msglen)) {         // T154, T180 D4: the media first; one that cannot be written back keeps the machine on
        return OPE_FAILED;
    }
    int rc = hw_esp32_rpc(ESP_CMD_MACHINE_REBOOT, 0, 0, 0, 0, 0);
    if (rc < 0) {
        snprintf(o.msg, o.msglen, "The ESP32 control module is not there (no link)");
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Power cycle sent to the control module");
    return OPE_OK;
}

// ---- registration ------------------------------------------------------------------------------------------------------------------------------
static const ArgSpec value_arg[] = { ARG_INT("value", -2000, 2000) };
static const ArgSpec name_arg[] = { ARG_PATH("name") };

void u64cfg_register(OpRegistry &r)
{
    r.contributor("u64cfg");
    r.add_op("u64.fx_video", value_arg, 1, fx_video);
    r.add_op("u64.fx_audio", value_arg, 1, fx_audio);
    r.add_op("u64.fx_sid", value_arg, 1, fx_sid);
    r.add_op("u64.fx_misc", value_arg, 1, fx_misc);
    r.add_op("u64.fx_input", value_arg, 1, fx_input);
    r.add_op("u64.apply_palette", name_arg, 1, op_palette_apply);
    r.add_op("u64.set_palette", name_arg, 1, op_palette_default);
    r.add_op("u64.power_off", 0, 0, op_power_off);
    r.add_op("u64.power_cycle", 0, 0, op_power_cycle);
    r.add_state("u64.palette", state_palette);
    r.add_state("u64.sid1_type", state_sid1);
    r.add_state("u64.sid2_type", state_sid2);
    r.add_state("u64.sid_map0", state_sid_map0);
    r.add_state("u64.sid_map1", state_sid_map1);
    r.add_state("u64.sid_map2", state_sid_map2);
    r.add_state("u64.sid_map3", state_sid_map3);
}
