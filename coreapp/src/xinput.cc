#include "xinput.h"
#include <string.h>

const uint8_t xinput_gip_poweron[5] = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0x00, 0x01, 0x00 };

const uint8_t xinput_360_magic[8] = { 0xC1, 0x01, 0x00, 0x01, 0x00, 0x00, 0x14, 0x00 };

namespace {

// 16 buttons, a hat (4 bits, 0..7, 15 outside = released), 4 pad bits, X and Y (8 bit, 0..255): the shape of hidpad.cc's test pads.
const uint8_t synth_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0x65, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0 };

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// up/down/left/right to the hat value; opposite directions cancel
uint8_t hat_of(bool u, bool d, bool l, bool r)
{
    if (u && d) u = d = false;
    if (l && r) l = r = false;
    if (u) return l ? 7 : r ? 1 : 0;
    if (d) return l ? 5 : r ? 3 : 4;
    return l ? 6 : r ? 2 : 15;
}

void fill(uint8_t out[XI_REPORT_LEN], uint16_t buttons, uint8_t hat, int16_t lx, int16_t ly)
{
    out[0] = (uint8_t)buttons;
    out[1] = (uint8_t)(buttons >> 8);
    out[2] = hat & 0x0F;
    out[3] = (uint8_t)((lx >> 8) + 128);                 // -32768..32767 to 0..255
    out[4] = (uint8_t)(127 - (ly >> 8));                 // XInput up is positive, HID Y grows downward
}

#define BIT(n) ((uint16_t)1 << (n))

// message 0x20: buttons in bytes 4-5, triggers 6-9 (10 bit), left stick 10-13; the bit order is not the 360's
void gip_convert(const uint8_t *m, bool guide, uint8_t out[XI_REPORT_LEN])
{
    uint8_t b0 = m[4], b1 = m[5];
    uint16_t b = 0;
    if (b1 & 0x10) b |= BIT(XI_B_LB);
    if (b1 & 0x20) b |= BIT(XI_B_RB);
    if (b1 & 0x40) b |= BIT(XI_B_LS);
    if (b1 & 0x80) b |= BIT(XI_B_RS);
    if (b0 & 0x04) b |= BIT(XI_B_START);
    if (b0 & 0x08) b |= BIT(XI_B_BACK);
    if (b0 & 0x10) b |= BIT(XI_B_A);
    if (b0 & 0x20) b |= BIT(XI_B_B);
    if (b0 & 0x40) b |= BIT(XI_B_X);
    if (b0 & 0x80) b |= BIT(XI_B_Y);
    if (guide) b |= BIT(XI_B_GUIDE);
    if (le16(m + 6) >= XI_TRIGGER_GIP) b |= BIT(XI_B_LT);
    if (le16(m + 8) >= XI_TRIGGER_GIP) b |= BIT(XI_B_RT);
    fill(out, b, hat_of(b1 & 1, b1 & 2, b1 & 4, b1 & 8), (int16_t)le16(m + 10), (int16_t)le16(m + 12));
}

}

int xinput_claim(uint8_t cls, uint8_t sub, uint8_t proto, bool sink_set, bool cap_pad)
{
    if (!sink_set || !cap_pad) return XI_NONE;
    if (cls == XI_360_CLASS && sub == XI_360_SUB && proto == XI_360_PROTO) return XI_360;
    if (cls == XI_GIP_CLASS && sub == XI_GIP_SUB && proto == XI_GIP_PROTO) return XI_GIP;
    return XI_NONE;
}

const uint8_t *xinput_descriptor(int *len)
{
    if (len) *len = (int)sizeof(synth_desc);
    return synth_desc;
}

bool xinput_convert_360(const uint8_t *d, int n, uint8_t out[XI_REPORT_LEN])
{
    if (n < 14 || d[0] != 0x00 || d[1] < 14) return false;
    uint8_t b0 = d[2], b1 = d[3];
    uint16_t b = 0;
    if (b1 & 0x01) b |= BIT(XI_B_LB);
    if (b1 & 0x02) b |= BIT(XI_B_RB);
    if (b1 & 0x04) b |= BIT(XI_B_GUIDE);
    if (b1 & 0x10) b |= BIT(XI_B_A);
    if (b1 & 0x20) b |= BIT(XI_B_B);
    if (b1 & 0x40) b |= BIT(XI_B_X);
    if (b1 & 0x80) b |= BIT(XI_B_Y);
    if (b0 & 0x10) b |= BIT(XI_B_START);
    if (b0 & 0x20) b |= BIT(XI_B_BACK);
    if (b0 & 0x40) b |= BIT(XI_B_LS);
    if (b0 & 0x80) b |= BIT(XI_B_RS);
    if (d[4] >= XI_TRIGGER_360) b |= BIT(XI_B_LT);
    if (d[5] >= XI_TRIGGER_360) b |= BIT(XI_B_RT);
    fill(out, b, hat_of(b0 & 1, b0 & 2, b0 & 4, b0 & 8), (int16_t)le16(d + 6), (int16_t)le16(d + 8));
    return true;
}

XiSession::XiSession() : kind_(XI_NONE), out_(0), guide_(false), have_(false), writes_(0)
{
    memset(last_, 0, sizeof(last_));
}

void XiSession::put(const uint8_t *p, int n)
{
    if (out_ && out_->write(p, n)) writes_++;
}

void XiSession::start(int kind, XiOut *out, int player)
{
    kind_ = kind;
    out_ = out;
    guide_ = false;
    have_ = false;
    if (kind == XI_GIP) {
        // what Linux xpad.c sends every Xbox One / Series pad (xboxone_init_packets, vendor 0x0000): power on, the guide LED on, authentication done;
        // byte 2 counts up per packet (xpad: odata_serial)
        uint8_t pkt[7];
        memcpy(pkt, xinput_gip_poweron, sizeof(xinput_gip_poweron));
        pkt[2] = 0x00;
        put(pkt, sizeof(xinput_gip_poweron));
        const uint8_t led[7] = { 0x0A, GIP_OPT_INTERNAL, 0x01, 0x03, 0x00, 0x01, 0x14 };
        put(led, sizeof(led));
        const uint8_t auth[6] = { 0x06, GIP_OPT_INTERNAL, 0x02, 0x02, 0x01, 0x00 };
        put(auth, sizeof(auth));
    } else if (kind == XI_360) {
        const uint8_t led[3] = { 0x01, 0x03, (uint8_t)(0x06 + (player & 3)) };      // the player's LED of the ring, steady
        put(led, sizeof(led));
    }
}

bool XiSession::data(const uint8_t *d, int n, uint8_t out[XI_REPORT_LEN])
{
    if (kind_ == XI_360) return xinput_convert_360(d, n, out);
    if (kind_ != XI_GIP || n < 4) return false;
    uint8_t cmd = d[0], opt = d[1], seq = d[2], plen = d[3];
    if (plen & 0x80) return false;                                                  // a two byte length: no message we read
    if ((opt & GIP_OPT_ACK) && cmd != GIP_CMD_ACK) {
        const uint8_t ack[13] = { GIP_CMD_ACK, GIP_OPT_INTERNAL, seq, 0x09, 0x00, cmd, (uint8_t)(opt & ~GIP_OPT_ACK), plen, 0x00, 0x00, 0x00, 0x00, 0x00 };
        put(ack, sizeof(ack));
    }
    if (cmd == GIP_CMD_VIRTUAL && n >= 5 && plen >= 1) {
        guide_ = d[4] & 1;
        if (!have_) return false;
        gip_convert(last_, guide_, out);
        return true;
    }
    if (cmd == GIP_CMD_INPUT && n >= GIP_INPUT_MIN && plen >= 14) {
        memcpy(last_, d, GIP_INPUT_MIN);
        have_ = true;
        gip_convert(last_, guide_, out);
        return true;
    }
    return false;
}
