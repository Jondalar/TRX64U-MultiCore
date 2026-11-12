// Private to uicore: shared by uic_svc.cc and uic_net.cc. Not part of the public surface.
#ifndef UICORE_PRIV_H
#define UICORE_PRIV_H

#include "uic.h"

void uic_sha256(const uint8_t *data, size_t n, uint8_t out[32]);
bool uic_ct_equal(const char *a, const char *b);   // constant time in the length of the longer, 32 characters

void uic_lock(UicService *s);
void uic_unlock(UicService *s);
void uic_ev_store(UicService *s, uint32_t rev, const char *text);   // into the ring (UI task)
uint32_t uic_next_rev(UicService *s);
void uic_apply(UicService *s, const UicIntent &in, UicReply *r);   // the one dispatcher (UI task)

#endif
