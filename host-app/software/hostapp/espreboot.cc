// T152 D11: the board side of espreboot.h. T184: the link is Gideon's now (esp32.cc owns the one DmaUART on the module's UART, wifi.cc runs its command
// task); the request goes through wifi_cmd.cc like every other one: CMD_IDENTIFY first, then CMD_MACHINE_REBOOT. This file opens no UART of its own any more.
#include "espreboot.h"
#include "wifi_cmd.h"
#include "FreeRTOS.h"
#include "task.h"

// wifi_machine_reboot() waits for an answer that never comes when the power goes (the transmit macro waits without a limit), so it has a task of its own.
static void reboot_task(void *)
{
    wifi_machine_reboot();
    vTaskDelete(NULL);
}

bool esp_power_cycle(const char **why)
{
    uint16_t major, minor;
    char name[34];
    if (wifi_detect(&major, &minor, name, sizeof(name)) != pdTRUE) {
        *why = "the ESP32 control module did not answer";
        return false;
    }
    if (xTaskCreate(reboot_task, "esp reboot", 2 * configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        *why = "the power cycle could not be sent to the control module";
        return false;
    }
    return true;
}
