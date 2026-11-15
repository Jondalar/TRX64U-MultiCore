// T196: UsbXinputDriver, beside UsbHidDriver (host-app/software/io/usb/usb_hid.cc) and registered through the same factory. Lives in coreapp/src, not in the
// USB copy: tools/hostapp_vendor.py copy replaces the copy's folders with vendor/gideon's files. The host app does not link this file.
#include <stdio.h>
#include <string.h>
#include "usb_base.h"
#include "usb_device.h"
#include "usb_xinput.h"
#include "xinput.h"
#include "FreeRTOS.h"
#include "task.h"

static const UsbPadSink *xi_sink = NULL;
static bool xi_cap_pad = false;

void usb_xinput_set_sink(const UsbPadSink *sink, bool cap_pad)
{
    xi_sink = sink;
    xi_cap_pad = cap_pad;
}

int pad_player_of(const void *key);

class UsbXinputDriver : public UsbDriver, public XiOut
{
    enum { PENDING = 4, PKT = 16 };
    UsbBase *host;
    UsbDevice *device;
    UsbInterface *interface;
    int kind;
    int irq_transaction;
    bool pad;
    struct t_pipe ipipe;
    struct t_pipe opipe;
    bool have_out;
    XiSession session;
    alignas(4) uint8_t irq_data[64];
    alignas(4) uint8_t obuf[PKT];
    alignas(4) uint8_t magic_reply[24];
    uint8_t pend[PENDING][PKT];
    uint8_t pend_len[PENDING];
    int pend_head, pend_tail;

public:
    static UsbDriver *test_driver(UsbInterface *intf);

    UsbXinputDriver(UsbInterface *intf, int k) : UsbDriver(intf), host(NULL), device(NULL), interface(NULL), kind(k), irq_transaction(0), pad(false),
                                                have_out(false), pend_head(0), pend_tail(0)
    {
        memset(&ipipe, 0, sizeof(ipipe));
        memset(&opipe, 0, sizeof(opipe));
        memset(pend_len, 0, sizeof(pend_len));
    }
    ~UsbXinputDriver() {}

    // The session's writes only queue: they may come from the input callback, which must not wait for the USB engine. flush() sends them from install() and poll().
    bool write(const uint8_t *data, int len)
    {
        if (!have_out || len > PKT || ((pend_head + 1) % PENDING) == pend_tail) return false;
        memcpy(pend[pend_head], data, len);
        pend_len[pend_head] = (uint8_t)len;
        pend_head = (pend_head + 1) % PENDING;
        return true;
    }
    void flush()
    {
        while (pend_tail != pend_head) {
            int n = pend_len[pend_tail];
            memcpy(obuf, pend[pend_tail], n);
            pend_tail = (pend_tail + 1) % PENDING;
            // a pad that never takes the OUT packet must not hold poll() for long: short timeout, the rest of the queue is dropped on a failure
            uint32_t t0 = (uint32_t)xTaskGetTickCount();
            int sent = host->bulk_out(&opipe, obuf, n, 200);                // the bytes that went out: a short count is a failure (0 on a NAK / timeout)
            uint32_t ms = ((uint32_t)xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
            if (ms > xi_stats.out_ms_max) xi_stats.out_ms_max = ms;
            if (sent == n) xi_stats.out_ok++; else xi_stats.out_fail++;
            if (sent < 0) {
                printf("[xinput] write of %d bytes failed, %d queued packets dropped\n", n, (pend_head - pend_tail + PENDING) % PENDING);
                pend_tail = pend_head;
            }
        }
    }

    void install(UsbInterface *intf)
    {
        UsbDevice *dev = intf->getParentDevice();
        printf("Installing XInput '%s %s'\n", dev->manufacturer, dev->product);
        host = dev->host;
        interface = intf;
        device = dev;
        dev->set_configuration(dev->get_device_config()->config_value);
        dev->set_interface(intf->getInterfaceDescriptor()->interface_number, intf->getInterfaceDescriptor()->alternate_setting);

        struct t_endpoint_descriptor *iin = intf->find_endpoint(0x83);      // interrupt IN
        struct t_endpoint_descriptor *iout = intf->find_endpoint(0x03);     // interrupt OUT
        if (!iin) {
            printf("[xinput] no interrupt IN endpoint\n");
            return;
        }
        int dlen = 0;
        const uint8_t *desc = xinput_descriptor(&dlen);
        pad = xi_sink && xi_sink->attach(this, dev->product[0] ? dev->product : "Xbox controller", dev->vendorID, dev->productID, desc, dlen);
        if (!pad) return;

        host->initialize_pipe(&ipipe, device, iin);
        int poll = xi_cfg.poll_ms;                                           // 20 ms = 50 Hz, as the HID driver; the diag core's xpad test changes it (T196 S7)
        if (poll < 1 || poll > 255) poll = 20;
        ipipe.Interval = (uint16_t)(poll * 8);                               // units of 125 us
        uint16_t mp = iin->max_packet_size;
        if (mp == 0 || mp > sizeof(irq_data)) mp = sizeof(irq_data);
        ipipe.Length = mp;
        ipipe.MaxTrans = mp;
        ipipe.buffer = irq_data;
        irq_transaction = host->allocate_input_pipe(&ipipe, callback, this);
        if (irq_transaction <= 0) {
            printf("[xinput] no input pipe\n");
            xi_sink->detach(this);
            pad = false;
            return;
        }
        xi_stats.attached = true;
        xi_stats.kind = kind;
        xi_stats.vid = dev->vendorID;
        xi_stats.pid = dev->productID;
        xi_stats.ep_interval = iin->interval;
        xi_stats.ep_maxpkt = iin->max_packet_size;
        xi_stats.programmed = ipipe.Interval;
        if (iout) {
            host->initialize_pipe(&opipe, device, iout);
            have_out = true;
        } else if (kind == XI_GIP) {
            printf("[xinput] GIP pad without an interrupt OUT endpoint: no power-on packet, the pad may stay silent\n");
        }
        session.start(kind, this, pad_player_of(this));
        flush();
        if (kind == XI_360) {
            uint8_t setup[8];
            memcpy(setup, xinput_360_magic, 8);
            setup[4] = intf->getInterfaceDescriptor()->interface_number;       // wIndex = the interface (0 on the pads known)
            host->control_exchange(&dev->control_pipe, setup, 8, magic_reply, 20);     // the reply is ignored; a pad that stalls it is no worse off
        }
        host->resume_input_pipe(irq_transaction);
    }

    void disable(void)
    {
        if (pad) {
            pad = false;
            xi_stats.attached = false;
            xi_sink->detach(this);
        }
        if (host && irq_transaction > 0) {
            host->free_input_pipe(irq_transaction);
            irq_transaction = 0;
        }
    }

    void poll(void)
    {
        if (pad && have_out) flush();
    }

    static void callback(void *object) { ((UsbXinputDriver *)object)->interrupt_handler(); }

    void interrupt_handler()
    {
        int n = host->getReceivedLength(irq_transaction);
        uint8_t rep[XI_REPORT_LEN];
        xi_stats.cb++;
        if (n <= 0) xi_stats.cb_empty++;
        if (n > 0 && pad && session.data(irq_data, n, rep)) {
            xi_stats_report(rep);
            xi_sink->report(this, rep, XI_REPORT_LEN);
        }
        host->resume_input_pipe(irq_transaction);
    }
};

UsbDriver *UsbXinputDriver::test_driver(UsbInterface *intf)
{
    const struct t_interface_descriptor *d = intf->getInterfaceDescriptor();
    int k = xinput_claim(d->interface_class, d->sub_class, d->protocol, xi_sink != NULL, xi_cap_pad);
    if (k == XI_NONE) return NULL;
    printf("** USB XInput device found (%s)!\n", k == XI_GIP ? "GIP" : "360");
    return new UsbXinputDriver(intf, k);
}

FactoryRegistrator<UsbInterface *, UsbDriver *> xinput_tester(UsbInterface::getUsbDriverFactory(), UsbXinputDriver::test_driver);
