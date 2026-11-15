// T196: XInput pads (Xbox 360 / X-mode wired, Xbox One / Series "GIP") for the pad service. Pure logic, no USB, no board: which interface the driver
// claims, how a pad is started, and how its native input report becomes the report of a SYNTHETIC HID descriptor (D1), which hidpad_parse reads like any
// other pad's. usb_xinput.cc is the board side (the USB driver); nothing in here knows a pipe.
//
// Public descriptions the byte values rest on (nothing was measured on a pad yet, see spec T196 section 5):
//   - the Linux kernel driver drivers/input/joystick/xpad.c (GPL-2.0, read for the layouts only; no code taken): the 360 report (type 0x00, length 0x14,
//     buttons in bytes 2-3, triggers 4-5, sticks 6-13, little endian), the 360 LED command (01 03 <pattern>), the GIP power-on packet
//     (05 20 <seq> 01 00), the GIP input message 0x20 (buttons bytes 4-5, triggers 6-9 as 10 bit, sticks 10-17) and the guide message 0x07;
//   - the community GIP protocol notes (header: command, options, sequence, length; options bit 0x10 = the sender wants an acknowledge, 0x20 =
//     internal/system message; the acknowledge message 0x01 carries the acknowledged command and options and the length of the acknowledged payload).
#ifndef COREAPP_XINPUT_H
#define COREAPP_XINPUT_H

#include <stdint.h>

enum { XI_NONE = 0, XI_360 = 1, XI_GIP = 2 };

// The interface triples the driver claims: 360 wired / X-mode pads, and GIP (Xbox One, Series).
#define XI_360_CLASS   0xFF
#define XI_360_SUB     0x5D
#define XI_360_PROTO   0x01
#define XI_GIP_CLASS   0xFF
#define XI_GIP_SUB     0x47
#define XI_GIP_PROTO   0xD0

// D3, D4: the driver claims only while the pad sink is set AND the running core has the capability `pad`. XI_NONE: leave the interface alone.
int xinput_claim(uint8_t cls, uint8_t sub, uint8_t proto, bool sink_set, bool cap_pad);

// ---- the synthetic HID report (what pad_attach / pad_report get) ----
// 5 bytes: buttons 1..16 (bit n = Button n+1), then the hat in the low nibble (0 N, 1 NE .. 7 NW, 15 none), then X and Y, 8 bit, 128 = centre.
// Button numbering = the generic numbering of pad.h (1 bottom, 2 right, 3 left, 4 top, 5 L, 6 R, 7 Select, 8 Start): A B X Y LB RB Back Start.
#define XI_REPORT_LEN 5
enum {
    XI_B_A = 0, XI_B_B, XI_B_X, XI_B_Y, XI_B_LB, XI_B_RB, XI_B_BACK, XI_B_START, XI_B_LS, XI_B_RS, XI_B_GUIDE, XI_B_LT, XI_B_RT
};
const uint8_t *xinput_descriptor(int *len);

// A trigger counts as a button from this value on (P4): 25 % of its range.
#define XI_TRIGGER_360  64      // 0..255
#define XI_TRIGGER_GIP  256     // 0..1023

// The 360 input report (20 bytes; shorter or of another type: false and *out untouched).
bool xinput_convert_360(const uint8_t *d, int n, uint8_t out[XI_REPORT_LEN]);

// The 360 pad's second start step: some third-party pads finish their init only after this vendor control read (GET, bmRequestType 0xC1, bRequest 1,
// wValue 0x0100, wIndex 0, wLength 20); the reply is read and ignored. The setup packet as the control pipe takes it.
extern const uint8_t xinput_360_magic[8];

// ---- GIP framing ----
#define GIP_CMD_ACK        0x01
#define GIP_CMD_POWER      0x05
#define GIP_CMD_VIRTUAL    0x07     // the guide button
#define GIP_CMD_INPUT      0x20
#define GIP_OPT_ACK        0x10
#define GIP_OPT_INTERNAL   0x20
#define GIP_INPUT_MIN      18       // header 4 + payload 14
extern const uint8_t xinput_gip_poweron[5];     // 05 20 00 01 00

// ---- T196 S7: what the diag core's xpad test measures (docs/specs/T196-xinput-pads.md section 2 S7) ----
// Production never reads it; the counters cost a few increments per packet. The driver fills the attach and OUT fields, the session the GIP fields, and
// xi_stats_report() the press/release edges of the decoded report.
struct XiStats {
    bool attached;                                  // a pad claimed by the driver
    int kind;                                       // XI_360 / XI_GIP
    uint16_t vid, pid;
    uint8_t ep_interval;                            // bInterval of the IN endpoint (ms at full speed)
    uint16_t ep_maxpkt;                             // wMaxPacketSize of the IN endpoint
    uint16_t programmed;                            // t_pipe.Interval as programmed (units of 125 us)
    uint32_t cb, cb_empty;                          // input callbacks, of them without data
    uint32_t cmd20, cmd07, cmd03, cmd_other;        // GIP packets per command (0x20 input, 0x07 guide, 0x03 keepalive)
    uint32_t ackbit20, ackbit_other;                // packets with the acknowledge bit (0x10), 0x20 and the rest
    uint32_t ack_queued, ack_dropped;               // acks the session wanted to write: taken by the queue / queue full
    uint32_t out_ok, out_fail, out_ms_max;          // OUT writes the driver sent (a short count is a failure), the longest one in ms
    uint32_t seq_gaps, seq_lost;                    // 0x20 messages whose sequence byte skipped, and how many messages that makes
    bool seq_have;
    uint8_t seq_last;
    uint32_t press[6], release[6];                  // up, down, left, right, A, B: edges of the decoded report
    uint8_t last_state;                             // bit n = direction/button n held in the last decoded report
};
extern XiStats xi_stats;
// The diag test's knobs (D9 of the spec); the defaults are what production does.
struct XiCfg {
    int poll_ms;                                    // the input pipe's interval; 20 = 50 Hz as the HID driver
    bool ack_input;                                 // acknowledge a 0x20 message that asks for it
};
extern XiCfg xi_cfg;
void xi_stats_reset();                              // the counters back to 0 (the attach fields stay)
void xi_stats_report(const uint8_t rep[XI_REPORT_LEN]);
// The counters as lines of text for the screen; the length written.
int xi_stats_text(char *out, int cap, uint32_t cb_per_s);

// What the session writes to the pad's OUT endpoint. true: taken.
class XiOut {
public:
    virtual bool write(const uint8_t *data, int len) = 0;
    virtual ~XiOut() {}
};

// One pad's protocol state. start() sends the start sequence once; data() takes every packet the IN endpoint delivered, answers what needs an answer
// through XiOut and returns true when `out` holds a synthetic report to hand to pad_report().
class XiSession {
public:
    XiSession();
    void start(int kind, XiOut *out, int player);
    bool data(const uint8_t *d, int n, uint8_t out[XI_REPORT_LEN]);
    int kind() const { return kind_; }
    int writes() const { return writes_; }
private:
    int kind_;
    XiOut *out_;
    bool guide_;
    bool have_;
    uint8_t last_[GIP_INPUT_MIN];
    int writes_;
    bool put(const uint8_t *p, int n);
};

#endif
