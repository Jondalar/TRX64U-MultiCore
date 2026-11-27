// A global object with a destructor that does something visible: the compiler registers it with __cxa_atexit, which no library of this build defines.
// No SDK header: this fixture has to compile where no newlib headers are installed.
volatile int sink;

struct Guard {
    int v;
    Guard() : v(1) {}
    ~Guard() { sink = 0; }
};
static Guard guard;

extern "C" int lib_register(void) { return guard.v; }
