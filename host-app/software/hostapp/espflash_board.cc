// T152 S8: the board side of espflash.h. The ROM loader is spoken to over the WiFi UART that esp32.cc owns (Gideon's Esp32 object, T184): the module's boot
// pins are the host gateware's esp_ctl (0x10060910, host/esp_ctl.v: +0 b0 ARM, +2 reads 0xE5), driven by the UART's flow control bits (DmaUART::ModuleCtrl) once armed.
// The sequence of calls is esp32.cc Download() / EnableRunMode() with the same waits; Esp32::Download() and Flash() themselves stay out of this build (FLASH_EXTRA_PROTECTION).
#include "espflash.h"
#include "esp32.h"
#include "dma_uart.h"
#include "wifi_cmd.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define ESP_CTL_ARM ((volatile uint8_t *)0x10060910)
#define ESP_CTL_ID  ((volatile uint8_t *)0x10060912)

namespace {
struct BoardLink : EspFlashLink {
    bool pins_ok;
    BoardLink() : pins_ok(false) { }
    void arm(bool on)
    {
        if (on) {
            pins_ok = *ESP_CTL_ID == 0xE5;                       // a host gateware without the boot control reads 0 there: the loader cannot be entered
            if (pins_ok) {
                *ESP_CTL_ARM = 1;
            }
        } else if (pins_ok) {
            *ESP_CTL_ARM = 0;
            pins_ok = false;
        }
    }
    bool enter_loader()
    {
        if (!pins_ok) {
            return false;
        }
        DmaUART *u = esp32.uart;
        esp32.StopApp();
        u->EnableSlip(false);
        u->EnableLoopback(false);
        u->FlowControl(false);
        u->txDebug = false;
        u->ModuleCtrl(ESP_MODE_OFF);
        u->ClearRxBuffer();
        vTaskDelay(150);
        u->SetBaudRate(115200);
        u->EnableLoopback(false);
        u->FlowControl(false);
        u->ClearRxBuffer();
        u->EnableSlip(false);
        u->EnableIRQ(true);
        u->ModuleCtrl(ESP_MODE_BOOT);
        vTaskDelay(100);                                         // the boot message of the ROM
        static uint8_t msg[512];
        memset(msg, 0, sizeof(msg));
        u->EnableSlip(true);
        u->EnableSlip(false);
        bool found = false;
        if (u->Read(msg, sizeof(msg)) > 0) {
            for (int i = 0; i < 100 && !found; i++) {
                found = !strncmp((const char *)msg + i, "DOWNLOAD", 8);
            }
        }
        u->EnableSlip(true);                                     // from here on packets
        return found;
    }
    void send_packet(const uint8_t *p, uint32_t n) { esp32.uart->SendSlipPacket(p, (int)n); }
    int recv_packet(uint8_t *p, uint32_t cap, int timeout_ms)
    {
        command_buf_t *buf;
        if (esp32.uart->ReceivePacket(&buf, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return 0;
        }
        uint32_t n = buf->size < cap ? buf->size : cap;
        memcpy(p, buf->data, n);
        esp32.uart->FreeBuffer(buf);
        return (int)n;
    }
    void set_baud(uint32_t bps) { esp32.uart->SetBaudRate((int)bps); }
    void leave_loader()
    {
        esp32.EnableRunMode();
        esp32.StartApp();
    }
    bool identify(uint16_t *major, uint16_t *minor)
    {
        char name[34];
        for (int i = 0; i < 20; i++) {                           // the new application needs a few seconds to come up
            if (wifi_detect(major, minor, name, sizeof(name)) == pdTRUE) {
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        return false;
    }
};
}

EspFlashLink *esp_flash_board_link(void)
{
    static BoardLink link;
    return &link;
}
