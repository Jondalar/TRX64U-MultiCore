// Native stand-in for Gideon's components/indexed_list.h (T190).
#pragma once
#include <vector>
template<class T> class IndexedList {
    std::vector<T> v;
public:
    IndexedList(int, T) { }
    int get_elements() { return (int)v.size(); }
    T &operator[](int i) { return v[i]; }
    void append(T x) { v.push_back(x); }
};
