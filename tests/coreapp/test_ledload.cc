// T159 S1, S2: case LED 1 as the machine's drive LED in the A1200 (coreapp/src/ledload.cc, the setting "LED Load Indicator" applied by the
// operation led.load). The application only selects the gateware source once; the trace of the selector writes is FakeIo::led_log.
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "ops_glue.h"

int main()
{
    // ---- S1: On writes the selector once: LED 1 = drive A activity (4), LED 2 = On (0); no blinking, however long the machine runs
    {
        FakeFs fs;
        fs.put("/usb0/a.hdf", std::vector<uint8_t>(512 * 64, 7));
        AmigaRig r(&fs, &amiga1200_desc);
        int si = r.st->find("LED Load Indicator");
        CHECK(si >= 0);
        CHECK_EQ(r.st->value(si), 0);                                    // default On
        CHECK_EQ(r.io.led_log.size(), 0);                                // nothing before the operation runs
        ops_set(&r.c, "led.load", 0);                                    // what regset_apply does for the default at the start
        CHECK_EQ(r.io.led_log.size(), 1);
        CHECK(r.io.led_log.size() == 1 && r.io.led_log[0].v == 0x04);
        ops_set(&r.c, "led.load", 0);                                    // applied again: nothing new
        char msg[160];
        CHECK_EQ(sec_mount(&r.c, 0, "/usb0/a.hdf", msg, sizeof(msg)), 0);
        for (int i = 0; i < 300; i++) {                                  // data moving for 3 s
            if (i % 10 == 0) r.io.post(0, false, i / 10);
            engines_poll(&r.c);
            r.io.delay_ms(10);
        }
        CHECK_EQ(r.io.led_log.size(), 1);                                // the application never touches it again
    }

    // ---- S2: Off leaves the LED alone
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga1200_desc);
        int si = r.st->find("LED Load Indicator");
        r.st->set(si, 1);
        ops_set(&r.c, "led.load", 1);
        for (int i = 0; i < 100; i++) { engines_poll(&r.c); r.io.delay_ms(10); }
        CHECK_EQ(r.io.led_log.size(), 0);
    }

    // ---- switching Off after On takes the selector back to On / On once (a latched selector cannot be unset), then nothing
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga1200_desc);
        ops_set(&r.c, "led.load", 0);
        ops_set(&r.c, "led.load", 1);
        CHECK_EQ(r.io.led_log.size(), 2);
        CHECK(r.io.led_log.size() == 2 && r.io.led_log[1].v == 0x00);
        ops_set(&r.c, "led.load", 1);
        CHECK_EQ(r.io.led_log.size(), 2);
        ops_set(&r.c, "led.load", 0);                                    // and On again selects the drive source again
        CHECK(r.io.led_log.size() == 3 && r.io.led_log[2].v == 0x04);
    }

    // ---- the A500 is the A1200 1:1 (T159 D-all, Alex 2026-11-02): the same setting, default On, the same single selector write 0x04
    {
        FakeFs fs;
        AmigaRig r(&fs, &amiga_desc);
        int si = r.st->find("LED Load Indicator");
        CHECK(si >= 0);
        CHECK_EQ(r.st->value(si), 0);
        ops_set(&r.c, "led.load", 0);
        CHECK(r.io.led_log.size() == 1 && r.io.led_log[0].v == 0x04);
        for (int i = 0; i < 100; i++) { engines_poll(&r.c); r.io.delay_ms(10); }
        CHECK_EQ(r.io.led_log.size(), 1);
    }
    DONE("ledload");
}
