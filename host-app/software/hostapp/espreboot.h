// T152 D11: the power cycle of the board, asked of the ESP32 control module. The same request as the core application's `u64.power_cycle`
// (coreapp/src/u64cfg.cc, hw.cc hw_esp32_rpc): rpc_header_t { command, thread, sequence } over the WiFi UART's SLIP link, command 0x13 MACHINE_REBOOT, no
// answer (the power goes). Before it the module is asked who it is (CMD_IDENTIFY 0x02): a module that does not answer is reported, not assumed.
// The board side is espreboot.cc; the render bench links its own.
#ifndef HOST_ESPREBOOT_H
#define HOST_ESPREBOOT_H

#define ESP_CMD_IDENTIFY       0x02
#define ESP_CMD_MACHINE_REBOOT 0x13

// true = the request was sent (the board is about to lose its power); false = it was not, *why says why (static text)
bool esp_power_cycle(const char **why);

#endif
