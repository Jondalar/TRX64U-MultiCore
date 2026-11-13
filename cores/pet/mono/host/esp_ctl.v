// T152 S8 (taken from TRX64-Ultimate t26-esp-ram, designs/c64_basic/esp_ctl.v, T26.1): the two ESP32 control pins, WIFI_BOOT (L13) and WIFI_DIS (M13).
//
// Gideon's uart_dma has the bits (flowctrl 4 = boot, 5 = enable, the app's
// ModuleCtrl); this block puts them on the pins, behind a gate. Until the
// gate (ARM) is set by a write, both pins are high-Z (oe = 0; top.v: pin = oe ? level : z), which is exactly what
// the bitstream did before (pins unconstrained, tool default pull-down, the
// ESP32 runs the board and is never reset). The uart_dma reset value is
// boot = 1, enable = 0 and must never reach the pins by itself.
//
// Pin = uart_dma bit XOR the polarity bit. The derived polarity (no polarity
// bit set): WIFI_BOOT high = ESP32 GPIO0 low = ROM loader at the next reset,
// WIFI_DIS high = ESP32 held in reset (the app's mode values
// OFF 3 / BOOT 1 / RUN 0). Both pins low = normal run. If the
// board turns out to be the other way round, INV_BOOT / INV_DIS flip them.
//
// Registers (the app: 0x10060910 + offset; rdata is valid the clock after re):
//   +0 R/W  b0 ARM (reset 0), b1 INV_BOOT (reset 0), b2 INV_DIS (reset 0)
//   +1 R    b0 level driven on WIFI_BOOT, b1 on WIFI_DIS, b2 pins are driven (ARM)
//   +2 R    0xE5, +3 R version (1)
module esp_ctl (
    input  wire       clk,
    input  wire       rst,
    input  wire       we,
    input  wire       re,
    input  wire [3:0] addr,
    input  wire [7:0] wdata,
    output reg  [7:0] rdata = 8'h00,
    input  wire       boot_i,                 // uart_dma boot  (flowctrl bit 4)
    input  wire       enable_i,               // uart_dma enable (flowctrl bit 5)
    output wire       boot_o,                 // the pin levels, and the output enable (ARM):
    output wire       dis_o,                  // top.v makes the tri-state (z while not armed)
    output wire       oe
);
    reg arm = 1'b0, inv_boot = 1'b0, inv_dis = 1'b0;

    wire boot_v = boot_i ^ inv_boot;
    wire dis_v  = enable_i ^ inv_dis;

    assign boot_o = boot_v;
    assign dis_o  = dis_v;
    assign oe     = arm;

    always @(posedge clk) begin
        if (rst) begin
            arm <= 1'b0; inv_boot <= 1'b0; inv_dis <= 1'b0;
        end else if (we && addr == 4'd0) begin
            arm <= wdata[0]; inv_boot <= wdata[1]; inv_dis <= wdata[2];
        end
        if (re) begin
            case (addr)
            4'd0:    rdata <= {5'd0, inv_dis, inv_boot, arm};
            4'd1:    rdata <= {5'd0, arm, dis_v, boot_v};
            4'd2:    rdata <= 8'hE5;
            4'd3:    rdata <= 8'h01;
            default: rdata <= 8'h00;
            endcase
        end
    end
endmodule
