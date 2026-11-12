// T111 S2: nothing is written before the deliberate confirmation, on every other path. The confirm state machine (fmtconfirm.cc: keys and a
// clock in, a verdict out) drives the real format (fmt_exfat over a counting device): only the exact name, RETURN, then a second RETURN
// between 1 s and 10 s later lead to a first write. Every other path ends with the write counter at 0.
//   run.sh builds this against fmtconfirm.cc as it is (must pass) and against -DFMT_NO_CONFIRM and sed mutants (each must go red).
#include "fmtbench.h"
#include "fmtconfirm.h"
#include "fmt.h"

static bool op_detach(void *) { return true; }
static bool op_attach(void *, const FmtGeom *, bool, char *, int) { return true; }

struct Run {
    SparseDisk disk;
    FmtConfirm fc;
    uint32_t now;
    int started;
    Run() : disk(128 * MiB_S), now(1000), started(0) { fmtc_open(&fc, "Usb0", now); }
    // one event of the machine: the first GO runs the real format
    void act(FmtcEvent ev)
    {
        if (ev == FMTC_GO && !started) {
            started = 1;
            static uint8_t work[65536];
            FmtOps ops = { 0, op_detach, op_attach };
            char why[160];
            fmt_exfat(&disk, &ops, work, sizeof(work), 0, 0, why, sizeof(why));
        }
    }
    void key(FmtcKey k, int ch = 0, uint32_t dt = 100) { now += dt; act(fmtc_key(&fc, k, ch, now)); }
    void type(const char *s) { for (; *s; s++) key(FMTK_CHAR, *s, 80); }
    void tick(uint32_t dt, bool present = true) { now += dt; act(fmtc_tick(&fc, now, present)); }
};

int main()
{
    // open and leave
    { Run r; r.key(FMTK_ESCAPE); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_CANCELLED); }
    // open and do nothing: the 60 s idle time-out
    { Run r; for (int i = 0; i < 70; i++) r.tick(1000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_CANCELLED); }
    // a medium is selected, the person looks, types nothing, presses RETURN
    { Run r; r.key(FMTK_RETURN); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING && r.fc.msg); r.key(FMTK_RETURN, 0, 2000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING); }
    // the wrong name, RETURN
    { Run r; r.type("Usb1"); r.key(FMTK_RETURN); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING && r.fc.msg);
      r.key(FMTK_RETURN, 0, 2000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING); }
    // the right name minus one character, and plus one
    { Run r; r.type("Usb"); r.key(FMTK_RETURN); r.key(FMTK_RETURN, 0, 2000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING); }
    { Run r; r.type("Usb00"); r.key(FMTK_RETURN); r.key(FMTK_RETURN, 0, 2000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING); }
    // a name typed, corrected with BACKSPACE, RETURN twice: the corrected text is what counts
    { Run r; r.type("Usb0x"); r.key(FMTK_BACKSPACE); r.key(FMTK_RETURN); CHECK(r.fc.state == FMTC_ARMED && r.disk.writes == 0); r.key(FMTK_ESCAPE); CHECK(r.fc.state == FMTC_CANCELLED && r.disk.writes == 0); }
    { Run r; r.type("Usb0"); r.key(FMTK_BACKSPACE); r.key(FMTK_RETURN); CHECK(r.fc.state == FMTC_TYPING); r.key(FMTK_RETURN, 0, 2000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_TYPING); }
    // armed, then: another key / ESC / the time-out / the medium pulled -> cancelled, then a RETURN does nothing
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); CHECK(r.fc.state == FMTC_ARMED); r.key(FMTK_CHAR, 'x'); CHECK(r.fc.state == FMTC_CANCELLED);
      r.key(FMTK_RETURN, 0, 1500); CHECK(r.disk.writes == 0); }
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); r.key(FMTK_ESCAPE); CHECK(r.fc.state == FMTC_CANCELLED); r.key(FMTK_RETURN, 0, 1500); CHECK(r.disk.writes == 0); }
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); for (int i = 0; i < 11; i++) r.tick(1000); CHECK(r.fc.state == FMTC_CANCELLED); r.key(FMTK_RETURN, 0, 500); CHECK(r.disk.writes == 0); }
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); r.now += 11000; r.key(FMTK_RETURN, 0, 0); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_CANCELLED); }    // no tick came, the RETURN itself is late
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); r.tick(300, false); CHECK(r.fc.state == FMTC_CANCELLED); r.key(FMTK_RETURN, 0, 1500); CHECK(r.disk.writes == 0); }
    { Run r; r.type("Usb"); r.tick(300, false); CHECK(r.fc.state == FMTC_CANCELLED && r.disk.writes == 0); }                                      // pulled while typing
    // the key repeat of a held RETURN is not a second RETURN: armed at t, then a RETURN every 33 ms for 3 s
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); CHECK(r.fc.state == FMTC_ARMED); for (int i = 0; i < 90; i++) r.key(FMTK_RETURN, 0, 33);
      CHECK(r.disk.writes == 0 && r.fc.state == FMTC_ARMED); }
    // the same with the first repeat after 660 ms (the X11 default) and then the rate of 25 per second
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); r.key(FMTK_RETURN, 0, 660); for (int i = 0; i < 80; i++) r.key(FMTK_RETURN, 0, 40);
      CHECK(r.disk.writes == 0 && r.fc.state == FMTC_ARMED); }
    // the second RETURN too soon after the first is ignored, and a later one still works
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); r.key(FMTK_RETURN, 0, 500); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_ARMED);
      r.key(FMTK_RETURN, 0, 1500); CHECK(r.fc.state == FMTC_STARTED && r.disk.writes > 0); }
    // the whole act, in the other case: only this leads to a write
    { Run r; r.type("usb0"); r.key(FMTK_RETURN); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_ARMED); r.tick(2000); CHECK(r.disk.writes == 0 && r.fc.state == FMTC_ARMED);
      r.key(FMTK_RETURN, 0, 1000); CHECK(r.fc.state == FMTC_STARTED && r.disk.writes > 0); }
    // a started act cannot be started again, and a closed one does not react
    { Run r; r.type("Usb0"); r.key(FMTK_RETURN); r.key(FMTK_RETURN, 0, 1500); uint64_t w = r.disk.writes; r.key(FMTK_RETURN, 0, 1500); r.key(FMTK_RETURN, 0, 1500); CHECK(r.disk.writes == w && w > 0); }
    // input is bounded
    { Run r; for (int i = 0; i < 60; i++) r.key(FMTK_CHAR, 'a'); CHECK(r.fc.n < FMTC_NAME_MAX); r.key(FMTK_RETURN); CHECK(r.disk.writes == 0); }

    printf("test_fmtconfirm: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
