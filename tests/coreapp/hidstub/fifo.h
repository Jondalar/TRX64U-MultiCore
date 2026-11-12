// Native stand-in for Gideon's components/fifo.h: only what hid_decoder.h calls (T190: the test builds the real HidMouseInterpreter).
#pragma once
#include <deque>
#include <stdint.h>
template<class T> class Fifo {
    std::deque<T> q;
public:
    Fifo(int, T) { }
    bool is_empty() { return q.empty(); }
    T pop() { T v = q.front(); q.pop_front(); return v; }
    void push(T v) { q.push_back(v); }
    T head() { return q.front(); }
};
