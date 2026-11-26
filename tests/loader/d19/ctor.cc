// A global object whose constructor calls a function: the compiler puts the call into .init_array.
// No SDK header: this fixture has to compile where no newlib headers are installed.
extern "C" int outside_value(void) { return 7; }

struct Cache {
    int v;
    Cache() : v(outside_value()) {}
};
static Cache cache;

extern "C" int lib_register(void) { return cache.v; }
