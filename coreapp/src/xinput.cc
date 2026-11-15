#include "xinput.h"
#include <string.h>
#include <stdio.h>

const uint8_t xinput_gip_poweron[5] = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0x00, 0x01, 0x00 };

const uint8_t xinput_360_magic[8] = { 0xC1, 0x01, 0x00, 0x01, 0x00, 0x00, 0x14, 0x00 };

XiStats xi_stats;
XiCfg xi_cfg = { 20, true };

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

bool XiSession::put(const uint8_t *p, int n)
{
    if (out_ && out_->write(p, n)) {
        writes_++;
        return true;
    }
    return false;
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
    XiStats &st = xi_stats;
    if (opt & GIP_OPT_ACK) {
        if (cmd == GIP_CMD_INPUT) st.ackbit20++; else st.ackbit_other++;
    }
    if (cmd == GIP_CMD_INPUT) {
        st.cmd20++;
        if (st.seq_have) {
            uint8_t gap = (uint8_t)(seq - st.seq_last - 1);
            if (gap && !(st.seq_last == 255 && seq == 1)) {         // 255 -> 1 may be the pad skipping 0 on a wrap, not a loss
                st.seq_gaps++;
                st.seq_lost += gap;
            }
        }
        st.seq_have = true;
        st.seq_last = seq;
    } else if (cmd == GIP_CMD_VIRTUAL) {
        st.cmd07++;
    } else if (cmd == 0x03) {
        st.cmd03++;
    } else {
        st.cmd_other++;
    }
    if ((opt & GIP_OPT_ACK) && cmd != GIP_CMD_ACK && (cmd != GIP_CMD_INPUT || xi_cfg.ack_input)) {
        const uint8_t ack[13] = { GIP_CMD_ACK, GIP_OPT_INTERNAL, seq, 0x09, 0x00, cmd, (uint8_t)(opt & ~GIP_OPT_ACK), plen, 0x00, 0x00, 0x00, 0x00, 0x00 };
        if (put(ack, sizeof(ack))) st.ack_queued++; else st.ack_dropped++;
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

void xi_stats_reset()
{
    XiStats keep = xi_stats;
    memset(&xi_stats, 0, sizeof(xi_stats));
    xi_stats.attached = keep.attached;
    xi_stats.kind = keep.kind;
    xi_stats.vid = keep.vid;
    xi_stats.pid = keep.pid;
    xi_stats.ep_interval = keep.ep_interval;
    xi_stats.ep_maxpkt = keep.ep_maxpkt;
    xi_stats.programmed = keep.programmed;
}

void xi_stats_report(const uint8_t rep[XI_REPORT_LEN])
{
    uint8_t hat = rep[2] & 0x0F;
    uint8_t now = 0;
    if (hat == 7 || hat == 0 || hat == 1) now |= 1;                 // up
    if (hat == 3 || hat == 4 || hat == 5) now |= 2;                 // down
    if (hat == 5 || hat == 6 || hat == 7) now |= 4;                 // left
    if (hat == 1 || hat == 2 || hat == 3) now |= 8;                 // right
    if (rep[0] & (1 << XI_B_A)) now |= 16;
    if (rep[0] & (1 << XI_B_B)) now |= 32;
    uint8_t ch = now ^ xi_stats.last_state;
    for (int i = 0; i < 6; i++) {
        if (ch & (1 << i)) {
            if (now & (1 << i)) xi_stats.press[i]++; else xi_stats.release[i]++;
        }
    }
    xi_stats.last_state = now;
}

int xi_stats_text(char *out, int cap, uint32_t cb_per_s)
{
    const XiStats &s = xi_stats;
    int n = 0;
#define ADD(...) do { if (n < cap) n += snprintf(out + n, cap - n, __VA_ARGS__); } while (0)
    if (!s.attached) {
        ADD("No XInput pad claimed. Plug the pad in (or replug it).\n");
    } else {
        ADD("Pad %04X:%04X %s  IN bInterval %u ms, max packet %u\n", s.vid, s.pid, s.kind == XI_GIP ? "GIP" : "360", (unsigned)s.ep_interval,
            (unsigned)s.ep_maxpkt);
        ADD("Programmed %u x 125 us = %u ms   wanted by the test: %d ms\n", (unsigned)s.programmed, (unsigned)(s.programmed / 8), xi_cfg.poll_ms);
    }
    ADD("Ack of input: %s\n", xi_cfg.ack_input ? "on" : "off");
    ADD("Callbacks %u (%u/s, empty %u)\n", (unsigned)s.cb, (unsigned)cb_per_s, (unsigned)s.cb_empty);
    ADD("0x20 input %u   0x07 guide %u   0x03 keepalive %u   other %u\n", (unsigned)s.cmd20, (unsigned)s.cmd07, (unsigned)s.cmd03, (unsigned)s.cmd_other);
    ADD("Ack bit set: on 0x20 %u, on others %u\n", (unsigned)s.ackbit20, (unsigned)s.ackbit_other);
    ADD("Acks queued %u, queue full %u\n", (unsigned)s.ack_queued, (unsigned)s.ack_dropped);
    ADD("OUT writes ok %u, failed %u, slowest %u ms\n", (unsigned)s.out_ok, (unsigned)s.out_fail, (unsigned)s.out_ms_max);
    ADD("Sequence gaps in 0x20: %u (%u messages lost)\n", (unsigned)s.seq_gaps, (unsigned)s.seq_lost);
    static const char *const nm[6] = { "Up", "Down", "Left", "Right", "A", "B" };
    ADD("Edges press/release:\n");
    for (int i = 0; i < 6; i++) {
        ADD("%s %u/%u%s", nm[i], (unsigned)s.press[i], (unsigned)s.release[i], i < 5 ? "   " : "\n");
    }
#undef ADD
    return n < cap ? n : cap - 1;
}
