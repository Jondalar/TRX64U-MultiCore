// T157: the "Joystick ports" setting (Normal / Swapped) of every generic-app core that has one. For each core: the setting is there exactly once, a change
// is applied (the core's own register gets the swap bit, at once, through the setting's operation or its register bits), the .cfg keeps it
// ("Joystick ports=Swapped") and a new session reads it back. The cores that had the swap before T157 (apple2, atari800; their registers are checked in their own tests; the CPC's and the Next's are the units
// cpc_joyswap and zxnext_joyswap) are units of their apps as well (S4). zx (a choice per port) and c264 (its register is taken in reset) are named
// differently on purpose (T157 D1) and checked for their own key.
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "ops_glue.h"
#include "regset.h"
extern const CoreDesc apple2_desc;       // cores.h lists the shared app's cores; the Apple IIe is its own app (cores/apple2/app/lib)

struct Row { const CoreDesc *d; uint32_t reg; };

static int count_key(const CoreDesc &d, const char *key)
{
    int n = 0;
    for (int i = 0; i < d.nsettings; i++) n += strcmp(d.settings[i].key, key) == 0;
    return n;
}

// the last value written to `reg` (the full register number, as the core's header names it), -1 when none
static int last_write(FakeIo &io, uint32_t reg)
{
    int v = -1;
    for (auto &w : io.log) if (w.reg == reg) v = w.v;
    return v;
}

int main()
{
    // reg: the register that holds the swap bit (b0). apple2 (b6 of A2_CFG with the monitor items) and atari800 (flags) are checked in their own tests.
    // the cores that keep another form: their own key is there once (T157 D1)
    CHECK_EQ(count_key(apple2_desc, "Joystick ports"), 1);
    DONE("test_joyswap");
}
