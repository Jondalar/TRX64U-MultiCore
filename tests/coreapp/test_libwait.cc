// T104.1 5.5: the start waits for a stick root only when a stick is known to be coming. Without one a core start must not be
// LIB_MOUNT_WAIT_MS slower (the first version waited 4 s at every start). The file system is scripted against a clock that only
// advances in delay_ms, so the wait is measured in the model's milliseconds and costs no real time.
#include <string>
#include <vector>
#include "check.h"
#include "libscan.h"

struct Env : LibMountEnv {
    uint32_t t = 0;
    bool storage = false;
    int delays = 0;
    bool usb_storage_attached() override { return storage; }
    uint32_t now_ms() override { return t; }
    void delay_ms(uint32_t ms) override { t += ms; delays++; }
};

// "/" lists the devices installed from `installed_at`, "/usb1" lists from `mounted_at` (UINT32_MAX: never); both against env.t
struct ScriptFs : CoreFs {
    Env *env;
    uint32_t installed_at, mounted_at;
    ScriptFs(Env *e, uint32_t inst, uint32_t mnt) : env(e), installed_at(inst), mounted_at(mnt) { }
    int list(const char *dir, FsEntry *out, int max) override
    {
        if (!strcmp(dir, "/")) {
            int n = 0;
            if (n < max) { memset(&out[n], 0, sizeof(FsEntry)); strcpy(out[n].name, "flash"); out[n].dir = true; n++; }
            if (env->t >= installed_at && n < max) { memset(&out[n], 0, sizeof(FsEntry)); strcpy(out[n].name, "USB1"); out[n].dir = true; n++; }
            return n;
        }
        if (!strcasecmp(dir, "/usb1") && env->t >= mounted_at) {
            return 0;
        }
        return -1;
    }
    bool stat(const char *, uint64_t *, bool *) override { return false; }
    int load(const char *, void *, uint32_t) override { return -1; }
    bool save(const char *, const void *, uint32_t) override { return false; }
    bool remove(const char *) override { return false; }
    CoreFile *open(const char *, bool) override { return 0; }
    void close(CoreFile *) override { }
};

#define NEVER 0xFFFFFFFFu

int main()
{
    LibWaitResult w;
    // no stick, no USB storage: no wait, no delay at all
    {
        Env e; ScriptFs fs(&e, NEVER, NEVER);
        lib_wait_for_stick(&fs, &e, false, &w);
        CHECK(!w.waited); CHECK(!w.present); CHECK_EQ(e.t, 0); CHECK_EQ(e.delays, 0);
        CHECK(strstr(w.why, "no USB mass-storage device attached"));
    }
    // the ROMs come from /flash and the USB stack has no stick installed: no wait, even with a device enumerating
    {
        Env e; e.storage = true; ScriptFs fs(&e, NEVER, NEVER);
        lib_wait_for_stick(&fs, &e, true, &w);
        CHECK(!w.waited); CHECK_EQ(e.t, 0); CHECK(strstr(w.why, "ROMs come from /flash"));
    }
    // a root lists already: present, no wait
    {
        Env e; ScriptFs fs(&e, 0, 0);
        lib_wait_for_stick(&fs, &e, false, &w);
        CHECK(w.present); CHECK(!w.waited); CHECK_EQ(e.t, 0);
    }
    // a device is enumerating, installs at 700 ms, mounts at 1900 ms: waits exactly until the root lists
    {
        Env e; e.storage = true; ScriptFs fs(&e, 700, 1900);
        lib_wait_for_stick(&fs, &e, false, &w);
        CHECK(w.waited); CHECK(w.present); CHECK_EQ(w.ms, 1900); CHECK(strstr(w.why, "stick present after 1900 ms"));
    }
    // the device is installed, the medium never comes (a card reader without a card): the wait ends at the limit
    {
        Env e; ScriptFs fs(&e, 0, NEVER);
        lib_wait_for_stick(&fs, &e, true, &w);           // installed beats the flash rule: a stick that is known to be coming is waited for
        CHECK(w.waited); CHECK(!w.present); CHECK_EQ(w.ms, LIB_MOUNT_WAIT_MS); CHECK(strstr(w.why, "not seen after 4000 ms"));
    }
    // a device enumerates and never installs a root (a stick the stack cannot use): the same limit
    {
        Env e; e.storage = true; ScriptFs fs(&e, NEVER, NEVER);
        lib_wait_for_stick(&fs, &e, false, &w);
        CHECK(w.waited); CHECK(!w.present); CHECK_EQ(w.ms, LIB_MOUNT_WAIT_MS);
    }
    DONE("test_libwait");
}
