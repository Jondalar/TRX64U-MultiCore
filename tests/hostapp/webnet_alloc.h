// Force-included into webnet.cc / webui.cc by test_webnet: the allocator of netalloc.h (NET_ALLOC) is the test's, so "no memory" can be made to happen.
#ifndef TEST_WEBNET_ALLOC_H
#define TEST_WEBNET_ALLOC_H
#include <stddef.h>
extern "C" void *webnet_test_alloc(size_t n);
#endif
