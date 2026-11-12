// T152 S8, from TRX64-Ultimate t26-esp-ram (T26.1) with the ESP32 reset/boot sequence added: esp_ctl.v. The pins are z after reset whatever uart_dma says (its
// reset value is boot = 1, enable = 0), the register arms them, boot/enable
// reach the pins as the app's modes OFF 3 / BOOT 1 / RUN 0 demand, the
// polarity bits flip them, a reset disarms again, and the status/ID bytes read.
`timescale 1ns/1ps
module tb_esp_ctl;
    reg clk = 0, rst = 1, we = 0, re = 0;
    reg [3:0] addr = 0;
    reg [7:0] wdata = 0;
    wire [7:0] rdata;
    reg boot_i = 1, enable_i = 0;          // uart_dma reset values
    wire boot_o, dis_o, oe;
    wire wifi_boot = oe ? boot_o : 1'bz;   // as top.v makes the pins
    wire wifi_dis  = oe ? dis_o  : 1'bz;
    esp_ctl dut (.clk(clk), .rst(rst), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata),
                 .boot_i(boot_i), .enable_i(enable_i), .boot_o(boot_o), .dis_o(dis_o), .oe(oe));
    always #5 clk = ~clk;
    integer errors = 0;
    task check(input [255:0] what, input cond);
        if (!cond) begin errors = errors + 1; $display("FAIL %0s", what); end
    endtask
    task wr(input [3:0] a, input [7:0] d);
        begin @(negedge clk); addr = a; wdata = d; we = 1; @(negedge clk); we = 0; end
    endtask
    task rd(input [3:0] a);
        begin @(negedge clk); addr = a; re = 1; @(negedge clk); re = 0; @(negedge clk); end
    endtask
    // z shows as the pull-down's 0 on the net; read the driver itself too
    task mode(input b, input e);
        begin boot_i = b; enable_i = e; #1; end
    endtask

    // The ESP32 as the pins see it: a floating pin reads 0 (pull-down). DIS high holds EN low (reset); when DIS falls
    // the chip samples GPIO0 = BOOT: high pulls GPIO0 low (ROM loader), low = normal run.
    reg [1:0] esp_state = 2'd0;            // 0 = run, 1 = held in reset, 2 = ROM loader
    integer undriven_edges = 0;
    wire pin_dis  = (wifi_dis  === 1'bz) ? 1'b0 : wifi_dis;
    wire pin_boot = (wifi_boot === 1'bz) ? 1'b0 : wifi_boot;
    always @(posedge pin_dis) esp_state = 2'd1;
    always @(negedge pin_dis) esp_state = pin_boot ? 2'd2 : 2'd0;
    // a pin must never move while it is not driven: count edges seen with oe low
    always @(wifi_boot or wifi_dis) if (!oe && (wifi_boot !== 1'bz || wifi_dis !== 1'bz)) undriven_edges = undriven_edges + 1;
    initial begin
        repeat (3) @(negedge clk);
        rst = 0;
        // after reset: not driven, in uart_dma's reset state (boot 1) and in every mode
        #1;
        check("boot pin undriven", wifi_boot === 1'bz);
        check("dis pin undriven", wifi_dis === 1'bz);
        mode(1, 1); check("OFF not driven (boot)", wifi_boot === 1'bz); check("OFF not driven (dis)", wifi_dis === 1'bz);
        mode(0, 0); check("RUN not driven", wifi_boot === 1'bz && wifi_dis === 1'bz);
        rd(0); check("reg0 reset", rdata == 8'h00);
        rd(2); check("id", rdata == 8'hE5);
        rd(3); check("version", rdata == 8'h01);
        // arm: pins follow boot/enable, derived polarity
        wr(0, 8'h01);
        mode(0, 0); check("RUN: boot low", wifi_boot === 1'b0); check("RUN: dis low", wifi_dis === 1'b0);
        mode(1, 1); check("OFF: boot high", wifi_boot === 1'b1); check("OFF: dis high", wifi_dis === 1'b1);
        mode(1, 0); check("BOOT: boot high", wifi_boot === 1'b1); check("BOOT: dis low", wifi_dis === 1'b0);
        rd(1); check("status BOOT", rdata == 8'b00000101);
        mode(1, 1); rd(1); check("status OFF", rdata == 8'b00000111);
        // polarity bits
        wr(0, 8'h03); mode(0, 0);
        check("INV_BOOT RUN: boot high", wifi_boot === 1'b1); check("INV_BOOT RUN: dis low", wifi_dis === 1'b0);
        wr(0, 8'h05);
        check("INV_DIS RUN: boot low", wifi_boot === 1'b0); check("INV_DIS RUN: dis high", wifi_dis === 1'b1);
        rd(0); check("reg0 readback", rdata == 8'h05);
        // disarm gives the pins back
        wr(0, 8'h00);
        check("disarmed: z", wifi_boot === 1'bz && wifi_dis === 1'bz);
        // a write to another register does nothing
        wr(1, 8'hFF); check("reg1 read only", wifi_boot === 1'bz);
        // reset disarms
        wr(0, 8'h07); #1; check("armed again", wifi_boot !== 1'bz);
        rst = 1; @(negedge clk); @(negedge clk); rst = 0; #1;
        check("reset: z", wifi_boot === 1'bz && wifi_dis === 1'bz);
        rd(0); check("reset: reg0", rdata == 8'h00);
        // reset/boot-mode sequence of the app (RUN 0 -> OFF 3 -> BOOT 1 -> OFF 3 -> RUN 0), armed
        wr(0, 8'h01);
        mode(0, 0); check("seq: running", esp_state == 2'd0);
        mode(1, 1); check("seq: OFF holds reset", esp_state == 2'd1);
        // OFF -> BOOT: BOOT must still be high when DIS falls; the chip samples it there
        mode(1, 0); check("seq: BOOT enters ROM loader", esp_state == 2'd2);
        check("seq: BOOT high after release", wifi_boot === 1'b1 && wifi_dis === 1'b0);
        mode(1, 1); check("seq: OFF again resets", esp_state == 2'd1);
        mode(0, 1); check("seq: boot low in reset", wifi_boot === 1'b0 && esp_state == 2'd1);
        mode(0, 0); check("seq: RUN boots the app", esp_state == 2'd0);
        // a reset of the host in the ROM-loader state lets go of both pins at once
        mode(1, 1); mode(1, 0); check("seq: loader again", esp_state == 2'd2);
        @(negedge clk); rst = 1; @(negedge clk); #1;
        check("seq: reset frees pins in 1 clk", wifi_boot === 1'bz && wifi_dis === 1'bz);
        rst = 0; @(negedge clk);
        check("seq: no edge while undriven", undriven_edges == 0);
        if (errors == 0) $display("PASS tb_esp_ctl errors=0"); else $display("FAIL tb_esp_ctl errors=%0d", errors);
        $finish;
    end
endmodule
