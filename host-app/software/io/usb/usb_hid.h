#ifndef USB_HID_H
#define USB_HID_H

#include "usb_base.h"
#include "usb_device.h"
#include "hid_decoder.h"
#include "FreeRTOS.h"
#include "timers.h"

struct t_usb_hid_status_snapshot
{
    char mouse_name[33];
    char mouse_mode[12];
    char keyboard_name[33];
    char keyboard_mode[12];
};

void usb_hid_get_status_snapshot(t_usb_hid_status_snapshot& snapshot);

// T104.1 5.6: the mouse library's hook. One call per mouse report, with the motion after sensitivity and acceleration and the buttons in
// HID order (1 left, 2 right, 4 middle). A sink that returns true has consumed the report: the driver then does no port-1 output, no cursor
// keys and no menu navigation for it. Null (the default, and the host app): the driver handles the report itself, as before.
typedef bool (*usb_hid_mouse_sink_t)(int dx, int dy, uint8_t buttons);
void usb_hid_set_mouse_sink(usb_hid_mouse_sink_t sink);
// T141: the keyboard's hook. One call per combined keyboard report (modifiers, reserved, six usages), on the USB task, before the driver's own
// handling, which goes on as before (menus, the C64 matrix). Null (the default, and the host app): nothing changes.
typedef void (*usb_hid_keyboard_sink_t)(const uint8_t report[8]);
void usb_hid_set_keyboard_sink(usb_hid_keyboard_sink_t sink);
// T156: the gamepad library's hook. An interface whose report descriptor is not a keyboard or a mouse is offered to the sink: attach() returns true
// when its descriptor holds a gamepad, and the driver then reads the interface's reports and hands every one to report() (the bytes as the
// endpoint delivered them), until the interface goes away: detach(). `key` is the interface's identity. Null (the default, and the host app):
// such an interface stays unread, as before.
struct UsbPadSink {
    bool (*attach)(const void *key, const char *name, uint16_t vid, uint16_t pid, const uint8_t *desc, int len);
    void (*report)(const void *key, const uint8_t *data, int len);
    void (*detach)(const void *key);
};
void usb_hid_set_pad_sink(const UsbPadSink *sink);
// The driver's 1351 and port-1 output, for a sink that drives them (the C64 backend of the library).
void usb_hid_set_mouse1_position(int16_t mouse_x, int16_t mouse_y);
void usb_hid_set_joy1_output(uint8_t active_low_mask);

class UsbHidDriver : public UsbDriver
{
	int  irq_transaction;
    struct t_pipe ipipe;

    uint8_t irq_data[64];

    UsbBase   *host;
    UsbDevice *device;
    UsbInterface *interface;
    int  irq_in;
    bool keyboard;
    bool mouse;
    bool pad;                   // T156: the interface is a gamepad of the pad sink
    bool descriptor_keyboard;
    bool descriptor_mouse;
    bool rest_source;
    static int16_t mouse_x, mouse_y;
    uint8_t mouse_joy;
    int native_wheel_delta_queue[8];
    uint8_t native_wheel_queue_head;
    uint8_t native_wheel_queue_tail;
    uint8_t native_wheel_base_joy;
    uint8_t native_wheel_output_active;
    TimerHandle_t wheel_pulse_timer;
    uint8_t keyboard_data[8];
    HidItemList report_items;
    t_item_location rep_button1;
    t_item_location rep_button2;
    t_item_location rep_button3;
    t_item_location rep_mouse_x;
    t_item_location rep_mouse_y;
    t_item_location rep_wheel_v;
    t_item_location rep_wheel_h;
    bool has_button1;
    bool has_button2;
    bool has_button3;
    bool has_wheel_v;
    bool has_wheel_h;
    bool previous_left_button_pressed;
    bool mouse_registered;
    bool keyboard_registered;
    bool keyboard_idle_accepted;
    void unregisterKeyboardIdle();
    bool menu_left_button_consumed;
    bool menu_right_button_consumed;
    int menu_wheel_h_latch;
    int menu_wheel_v_latch;
    int menu_wheel_v_mode;
    int menu_wheel_v_burst_accumulator;
    int menu_wheel_v_burst_direction;
    uint32_t menu_wheel_v_last_tick;
    int wheel_axis_h_remainder;
    int wheel_axis_v_remainder;
    int wheel_key_h_remainder;
    int wheel_key_v_remainder;
    int wheel_step_accumulator;
    int wheel_pulse_phase;
    uint8_t wheel_pulse_mask;
    uint32_t wheel_pulse_next_tick;
    int wheel_pulse_burst_direction;
    uint8_t wheel_pulse_burst_count;
    int pointer_sensitivity_setting;
    int cursor_motion_x_remainder;      // motion not yet worth a cursor key
    int cursor_motion_y_remainder;
    int pointer_sensitivity_remainder_x;
    int pointer_sensitivity_remainder_y;
    int adaptive_accel_ema_x16;
    int adaptive_accel_scale_factor;
    static void S_wheel_pulse_timer(TimerHandle_t a);
    void service_native_wheel_timer(void);
    void set_joy1_output(uint8_t active_low_mask);
    bool process_mouse_report(const uint8_t *data, int data_len);

public:
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbHidDriver(UsbInterface *intf);
	~UsbHidDriver();

    bool has_active_report_keyboard(void) const;
    bool has_active_report_mouse(void) const;
    void relinquish_boot_function(bool release_keyboard, bool release_mouse);

	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    void interrupt_handler();

    // A wheel mouse with no USB device behind it, whose reports go through the
    // same handling as a USB mouse's; NULL if it cannot be built. Its buttons and
    // wheel pulses are their own port 1 source. Call only from one task.
    static UsbHidDriver *restMouse(void);
    static UsbHidDriver *restMouseIfBuilt(void);    // NULL until restMouse() built it
    bool restMouseAttached(void) const;
    void restMouseAttach(void);
    void restMouseDetach(void);
    void restMouseReport(uint8_t buttons, int dx, int dy, int wheel, int pan);
    uint8_t restMouseButtons(void) const;   // HID order: left 1, right 2, middle 4
};

#endif
