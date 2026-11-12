// usb_phy.v with behavioural stand-ins for the Xilinx primitives: the meter
// (60 MHz against 100 MHz, short gate), the DIR edge count, the ULPI_RESET
// polarity bits, the data turnaround and the 24 MHz forwarding.
`timescale 1ns/1ps
module tb_usb_phy;
    reg clk50 = 0, cpu_clk = 0, ulpi = 0, cpu_rst = 1;
    always #10 clk50 = ~clk50;
    always #5 cpu_clk = ~cpu_clk;
    always #8.333 ulpi = ~ulpi;

    reg        dir = 0, nxt = 0, stp = 0, data_t = 0, rst_req = 0, hub_n = 1;
    reg  [7:0] data_o = 8'hA5, phy_drive = 8'h3C;
    reg  [1:0] ctl = 2'b00;
    wire [7:0] ULPI_DATA, data_i;
    wire       ULPI_STP, ULPI_RESET, ULPI_REFCLK, HUB_RESETn, HUB_CLOCK, ulpi_clk;
    wire [31:0] khz;
    wire [15:0] dir_edges;
    assign ULPI_DATA = dir ? phy_drive : 8'hzz;

    usb_phy #(.GATE(16000)) dut (
        .clk50(clk50), .pll_rst(1'b0), .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .ULPI_CLOCK(ulpi), .ULPI_NXT(nxt), .ULPI_DIR(dir), .ULPI_STP(ULPI_STP), .ULPI_DATA(ULPI_DATA),
        .ULPI_RESET(ULPI_RESET), .ULPI_REFCLK(ULPI_REFCLK), .HUB_RESETn(HUB_RESETn), .HUB_CLOCK(HUB_CLOCK),
        .ulpi_clk(ulpi_clk), .ulpi_nxt(), .ulpi_dir(), .ulpi_data_i(data_i),
        .ulpi_stp(stp), .ulpi_data_o(data_o), .ulpi_data_t(data_t),
        .ulpi_reset_req(rst_req), .hub_reset_n(hub_n),
        .ctl(ctl), .khz(khz), .dir_edges(dir_edges)
    );

    integer err = 0, i, refedges = 0;
    always @(posedge ULPI_REFCLK) refedges = refedges + 1;
    task check(input cond, input [8*40-1:0] what); begin
        if (!cond) begin err = err + 1; $display("FAIL: %0s", what); end
    end endtask
    initial begin
        #200;
        check(ULPI_RESET === 1'b0, "ULPI_RESET low in reset");
        cpu_rst = 0; #100;
        check(ULPI_RESET === 1'b1, "ULPI_RESET high after reset");
        ctl = 2'b01; #100;
        check(ULPI_RESET === 1'b0, "ctl 1 inverts");
        ctl = 2'b10; rst_req = 1; #100;
        check(ULPI_RESET === 1'b0, "ctl 2 passes the app's reset");
        rst_req = 0; #100;
        check(ULPI_RESET === 1'b1, "ctl 2, app reset off");
        hub_n = 0; #50; check(HUB_RESETn === 1'b0, "hub reset"); hub_n = 1;
        // turnaround: link drives while data_t, PHY while DIR
        data_t = 1; #1; check(ULPI_DATA === 8'hA5, "link drives");
        data_t = 0; #1; check(ULPI_DATA === 8'hzz, "link released");
        @(posedge ulpi); #2 dir = 1; #1 check(data_i === 8'h3C, "PHY data in");
        @(posedge ulpi); #2 dir = 0;
        for (i = 0; i < 4; i = i + 1) begin
            repeat (3) @(posedge ulpi); #2 dir = 1; @(posedge ulpi); #2 dir = 0;
        end
        #400000;    // two gates
        $display("khz = %0d (expect 600 = 60 MHz), dir_edges = %0d, REFCLK edges %0d in 400 us",
                 khz, dir_edges, refedges);
        check(khz >= 599 && khz <= 601, "meter");
        check(dir_edges == 5, "DIR edges");
        check(refedges > 9500 && refedges < 9700, "24 MHz forwarded");
        if (err) $display("FAIL (%0d)", err); else $display("PASS");
        $finish;
    end
endmodule

// stand-ins for the primitives
module BUFG(input I, output O); assign O = I; endmodule
module IOBUF(inout IO, input I, input T, output O); assign IO = T ? 1'bz : I; assign O = IO; endmodule
module ODDR #(parameter DDR_CLK_EDGE = "SAME_EDGE") (output Q, input C, CE, D1, D2, R, S);
    assign Q = C ? D1 : D2;
endmodule
module PLLE2_BASE #(parameter real CLKIN1_PERIOD = 0.0, parameter integer DIVCLK_DIVIDE = 1,
                    parameter integer CLKFBOUT_MULT = 1, parameter integer CLKOUT0_DIVIDE = 1) (
    input CLKIN1, CLKFBIN, RST, PWRDWN,
    output CLKFBOUT, LOCKED, output reg CLKOUT0 = 0, output CLKOUT1, CLKOUT2, CLKOUT3, CLKOUT4, CLKOUT5);
    real half;
    initial begin
        half = CLKIN1_PERIOD * DIVCLK_DIVIDE * CLKOUT0_DIVIDE / CLKFBOUT_MULT / 2.0;
        forever #(half) CLKOUT0 = ~CLKOUT0;
    end
    assign CLKFBOUT = CLKFBIN;
    assign LOCKED = 1'b1;
endmodule
