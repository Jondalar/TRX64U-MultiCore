#include "hw_i2c_drv.h"
#include <stdio.h>

#define _wait_busy() while(this->i2c_regs->status & I2C_STATUS_BUSY) { __asm__("nop"); }
#define wait_busy(st) do { st = this->i2c_regs->status; } while(st & I2C_STATUS_BUSY)

void Hw_I2C_Driver :: set_channel(int channel)
{
    this->channel = channel;
}

void Hw_I2C_Driver :: i2c_start()
{
    i2c_regs->channel = this->channel;
    _wait_busy();
}

void Hw_I2C_Driver :: i2c_restart()
{
    i2c_regs->repeated_start = 1;                
    _wait_busy();
}


void Hw_I2C_Driver :: i2c_stop()
{
    i2c_regs->stop = 1;
    _wait_busy();
}

int Hw_I2C_Driver :: i2c_send_byte(const uint8_t byte)
{
    i2c_regs->data_out = byte;
    uint8_t st;
    wait_busy(st);
    return (st & I2C_STATUS_ERROR) ? -1 : 0;
}

uint8_t Hw_I2C_Driver :: i2c_receive_byte(int ack)
{
    if(ack) {
        i2c_regs->receive_ack = 1;
    } else {
        i2c_regs->receive = 1;
    }
    _wait_busy();
    return i2c_regs->data_out;
}

// No sleep after scan_enable = 0 (T242 D4): host/app_i2c.v keeps the master busy until the keyboard scanner has stopped between two
// transactions, so the first command waits in hardware. The firmware's vTaskDelay(2) here cost every i2c_lock 5-10 ms.
bool Hw_I2C_Driver :: enable_scan(bool enable, bool automatic)
{
    if (automatic) {
        if (scanning) { // was it manually enabled?
            i2c_regs->scan_enable = enable ? 1 : 0;
        }
    } else { // manual call to this function
        i2c_regs->scan_enable = enable ? 1 : 0;
        scanning = enable;
    }
    return true;
}
