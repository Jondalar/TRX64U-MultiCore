// The serial (IEC) bus between the C128 core (clk), the app's drive A (cpu_clk) and the board's IEC port (T170). Copy of cores/c64u/iec_bus_c64u.v
// with the fourth line, SRQ (the fast serial clock), and taps for the bus trace (c128_iectrace.v). Every party sees the wired AND of all
// pulls ('0' = line pulled low). Each source is registered in its own clock, then synchronized into the other one, and only then combined, so a
// level change in one clock can never glitch into the other.
// Board port (C64U, Gideon's XDC; host/pins.xdc): IEC_*_O = 1 pulls the line low, IEC_*_I = 1 while the line is low. The outputs carry only our own
// pulls, so the port never latches itself low. Drive A (a 1541 or a 1581) has no SRQ: only the core and the port take part in that line.
module iec_bus_c128 #(
    parameter PORT = 1                  // 0: board port ignored and released
) (
    input  wire clk,                    // C128
    input  wire c64_atn_o, c64_clk_o, c64_data_o, c64_srq_o,
    output wire c64_atn_i, c64_clk_i, c64_data_i, c64_srq_i,

    input  wire cpu_clk,                // drive
    input  wire drv_clk_o, drv_data_o,
    output wire drv_atn_i, drv_clk_i, drv_data_i,

    input  wire port_off,                                       // level: ignore and release the port
    input  wire port_atn_i, port_clk_i, port_data_i, port_srq_i,  // board pins, 1 = line low
    output reg  port_atn_o, port_clk_o, port_data_o, port_srq_o,

    // taps for the trace (clk): levels, 1 = high, {SRQ, DATA, CLK, ATN}; the bus as the core hears it, and the board port as it reaches the core
    output wire [3:0] tap_bus,
    output wire [3:0] tap_port
);
    // C128 side {ATN, CLK, DATA, SRQ}
    reg [3:0] c64_r = 4'b1111;
    always @(posedge clk) c64_r <= {c64_atn_o, c64_clk_o, c64_data_o, c64_srq_o};

    // drive side
    reg [1:0] drv_r = 2'b11;
    always @(posedge cpu_clk) drv_r <= {drv_clk_o, drv_data_o};

    // board side, as levels (1 = high), {ATN, CLK, DATA, SRQ}
    wire       port_on = PORT && !port_off;
    wire [3:0] port_lv = port_on ? ~{port_atn_i, port_clk_i, port_data_i, port_srq_i} : 4'b1111;

    // to the core: {drive CLK, drive DATA, port ATN, port CLK, port DATA, port SRQ}
    (* ASYNC_REG = "TRUE" *) reg [5:0] to_c64_s1 = 6'h3F, to_c64_s2 = 6'h3F;
    always @(posedge clk) begin
        to_c64_s1 <= {drv_r, port_lv};
        to_c64_s2 <= to_c64_s1;
    end
    assign c64_atn_i  = c64_r[3] & to_c64_s2[3];
    assign c64_clk_i  = c64_r[2] & to_c64_s2[5] & to_c64_s2[2];
    assign c64_data_i = c64_r[1] & to_c64_s2[4] & to_c64_s2[1];
    assign c64_srq_i  = c64_r[0] & to_c64_s2[0];
    assign tap_bus    = {c64_srq_i, c64_data_i, c64_clk_i, c64_atn_i};
    assign tap_port   = {to_c64_s2[0], to_c64_s2[1], to_c64_s2[2], to_c64_s2[3]};

    // to the drive and the port: {core ATN, CLK, DATA, SRQ, port ATN, CLK, DATA, SRQ}
    (* ASYNC_REG = "TRUE" *) reg [7:0] to_drv_s1 = 8'hFF, to_drv_s2 = 8'hFF;
    always @(posedge cpu_clk) begin
        to_drv_s1 <= {c64_r, port_lv};
        to_drv_s2 <= to_drv_s1;
    end
    assign drv_atn_i  = to_drv_s2[7] & to_drv_s2[3];
    assign drv_clk_i  = to_drv_s2[6] & drv_r[1] & to_drv_s2[2];
    assign drv_data_i = to_drv_s2[5] & drv_r[0] & to_drv_s2[1];

    always @(posedge cpu_clk) begin
        port_atn_o  <= port_on && !to_drv_s2[7];
        port_clk_o  <= port_on && !(to_drv_s2[6] & drv_r[1]);
        port_data_o <= port_on && !(to_drv_s2[5] & drv_r[0]);
        port_srq_o  <= port_on && !to_drv_s2[4];
    end
endmodule
