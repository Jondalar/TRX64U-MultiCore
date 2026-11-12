// T184 D3: the WiFi settings are set in the host (F8) and kept in its settings file, /Flash/trx/host.cfg, section "[WiFi settings]" (T163). The core
// application does not edit them: it reads the section once at its start into its own "WiFi settings" store (Use DHCP, Static IP / Netmask / Gateway /
// DNS) and never writes it back. Without that the core would run DHCP although the host has a static address.
#ifndef COREAPP_HOSTWIFI_H
#define COREAPP_HOSTWIFI_H

class ConfigStore;
class CoreFs;

#define HOSTWIFI_FILE "/Flash/trx/host.cfg"

// Applies the section "WiFi settings" of the text to the store; returns the number of values taken over (0: no section or nothing valid). Pure.
int hostwifi_apply(ConfigStore *wifi, const char *text, int len);

// Board: reads the file through fs, applies it to the store of the ConfigManager and effectuates it. Logs one line; an unreadable file leaves the store's defaults.
// Returns the number of values taken over, -1 when the file or the store is not there.
int hostwifi_load(CoreFs *fs);

#endif
