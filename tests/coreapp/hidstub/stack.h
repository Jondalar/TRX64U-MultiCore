// Native stand-in for Gideon's components/stack.h (T190).
#pragma once
#include <vector>
template<class T> class Stack {
    std::vector<T> v;
public:
    Stack(int, T) { }
    bool is_empty() { return v.empty(); }
    T pop() { T x = v.back(); v.pop_back(); return x; }
    void push(T x) { v.push_back(x); }
};
