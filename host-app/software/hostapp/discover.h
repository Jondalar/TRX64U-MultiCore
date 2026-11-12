// Which files the Add core browser lists (T101): *.cor bundles and *.cfw full firmware images (T115), any case. Nothing here searches:
// since 2026-10-25 the boot menu shows only what the person added (T101 S10), the browser lists the folder the person opened.
#ifndef HOST_DISCOVER_H
#define HOST_DISCOVER_H

bool discover_is_bundle(const char *name);

#endif
