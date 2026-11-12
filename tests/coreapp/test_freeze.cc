// T93 S4, D7, D9: Freeze - stop, wait for PAUSED (at most 200 ms, then the menu opens anyway), the layer to the menu; and back:
// the layer to the core, no key typed into the machine, the machine released last.
#include <string>
#include <vector>
#include "check.h"
#include "freeze.h"

struct Ops : FreezeOps {
    uint32_t t = 0;
    int paused_after = 37;          // ms after stop(true) the PAUSED bit rises; -1 never
    int key_up_at = 0;              // the key that closed the menu is released at this time
    int stop_t = -1;
    std::vector<std::pair<uint32_t, std::string>> ev;
    std::vector<std::string> logs;
    bool running = false;
    void note(const char *s) { ev.push_back({t, s}); }
    void stop(bool on) { if (on) stop_t = (int)t; running = !on; note(on ? "stop" : "run"); }
    uint8_t status() { return (paused_after >= 0 && stop_t >= 0 && (int)t >= stop_t + paused_after && !running) ? 0x40 : 0x00; }
    void layer_to_menu() { note("menu"); }
    void layer_to_core() { note("core"); }
    bool wait_keys_idle(uint32_t timeout)
    {
        uint32_t t0 = t;
        while ((int)t < key_up_at) {
            if (t - t0 > timeout) return false;
            t++;
        }
        note("keys idle");
        return true;
    }
    uint32_t now_ms() { return t; }
    void delay_ms(uint32_t ms) { t += ms; }
    void log(const char *s) { logs.push_back(s); }
    int at(const char *what) const { for (auto &e : ev) if (e.second == what) return (int)e.first; return -1; }
    int idx(const char *what) const { for (size_t i = 0; i < ev.size(); i++) if (ev[i].second == what) return (int)i; return -1; }
};

int main()
{
    FreezeDesc d = { 0x0A8, 0x40, 200 };
    {   // the normal case
        Ops o;
        Freeze f(d, &o);
        CHECK(f.enter());
        CHECK(f.frozen());
        CHECK_EQ(o.at("stop"), 0);
        CHECK(o.at("menu") >= 37);                       // the menu gets the layer only once the machine is paused
        CHECK(o.at("menu") < 45);
        CHECK(o.logs.empty());
        CHECK(f.enter());                                // twice: no second stop
        int stops = 0;
        for (auto &e : o.ev) stops += e.second == "stop";
        CHECK_EQ(stops, 1);
        // leave: the key was released at t = 400
        o.key_up_at = (int)o.t + 300;
        int up = o.key_up_at;
        f.leave();
        CHECK(!f.frozen());
        CHECK(o.idx("core") < o.idx("run"));             // the picture first, the machine last
        CHECK(o.at("run") >= up + (int)f.debounce());     // not before the key is up and the debounce has passed (D7)
        CHECK(o.running);
        int before = (int)o.ev.size();
        f.leave();                                       // leaving twice does nothing
        CHECK_EQ((int)o.ev.size(), before);
    }
    {   // the core never says PAUSED: S4's 200 ms, a console line, the menu opens anyway
        Ops o; o.paused_after = -1;
        Freeze f(d, &o);
        CHECK(!f.enter());
        CHECK(f.frozen());
        CHECK(o.at("menu") >= 200 && o.at("menu") <= 205);
        CHECK_EQ(o.logs.size(), 1);
        CHECK(o.logs.size() && o.logs[0].find("no PAUSED after 200 ms") != std::string::npos);
    }
    {   // a key that never comes up: the machine runs after 2 s, and says so
        Ops o;
        Freeze f(d, &o);
        f.enter();
        o.key_up_at = 1000000;
        f.leave();
        CHECK(o.running);
        CHECK(o.t >= 2000);
        CHECK_EQ(o.logs.size(), 1);
    }
    {   // PAUSED already up at the first look: no waiting
        Ops o; o.paused_after = 0;
        Freeze f(d, &o);
        CHECK(f.enter());
        CHECK_EQ(o.at("menu"), 0);
    }
    DONE("test_freeze");
}
