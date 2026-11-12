#pragma once
#include <string.h>
// The bench's interfaces: slot 0 is the cable with an address (as before); a scenario rewrites the slots (T184 chip check).
struct NetStubIf { bool present, up, wireless; char ip[24]; };
struct NetworkInterface {
    int idx = 0;
    static NetStubIf *slots() {
        static NetStubIf s[2] = { { true, true, false, "192.0.2.189" }, { false, false, false, "" } };
        return s;
    }
    static NetworkInterface *getInterface(int i) {
        static NetworkInterface n[2] = { { 0 }, { 1 } };
        return i >= 0 && i < 2 && slots()[i].present ? &n[i] : 0;
    }
    bool is_link_up() { return slots()[idx].up; }
    bool is_wireless() { return slots()[idx].wireless; }
    void getIpAddrString(char *b, int) { strcpy(b, slots()[idx].ip); }
};
