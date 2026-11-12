// Allocation for what a network task makes per request or per connection (T192 M6). On the board `operator new` never returns null (memory_wrap.cc panics when the heap
// is empty), `malloc` does: so these use malloc + placement new, and the 503 / close paths for "no memory" are real. A test sets NET_ALLOC to an allocator that can fail
// (as for nettransport.cc). Free with net_delete, never with `delete` (malloc's block has its own header on the board).
#ifndef COREAPP_NETALLOC_H
#define COREAPP_NETALLOC_H

#include <stdlib.h>
#include <new>

#ifndef NET_ALLOC
#define NET_ALLOC malloc
#endif

template <class T> T *net_new()
{
    void *p = NET_ALLOC(sizeof(T));
    return p ? new (p) T : 0;
}

template <class T> void net_delete(T *t)
{
    if (t) {
        t->~T();
        free(t);
    }
}

#endif
