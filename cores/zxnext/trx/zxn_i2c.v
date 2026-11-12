// T116 Q10: the Next's real-time clock, a DS1307 at I2C address 0x68, answered from the app's clock. The machine drives SCL and SDA open drain
// (o_I2C_SCL_n / o_I2C_SDA_n, 0 = pulled low) and reads the bus back (i_I2C_*_n); this slave pulls SDA low for its acknowledge and its data bits.
// Registers 0-6 are the time in BCD (seconds, minutes, hours 24 h, weekday 1-7, day, month, year 00-99), written by the app through the
// register block (zxn_regs: the app's service writes them about once a second); register 7 (control) and 8-63 (56 bytes of RAM) are the
// chip's own and writable by the machine. A write to registers 0-6 from the machine is accepted and ignored (the app's clock is the clock).
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zxn_i2c (
    input  wire        clk,
    input  wire        rst,
    input  wire        scl_n,             // the bus: 1 = high
    input  wire        sda_n,
    output reg         sda_pull = 1'b0,   // 1: the slave pulls SDA low
    input  wire [55:0] time_bcd           // register 0 in bits 7:0 ... register 6 in bits 55:48
);
    localparam [2:0] S_IDLE = 3'd0, S_ADDR = 3'd1, S_PTR = 3'd2, S_WDATA = 3'd3, S_RDATA = 3'd4;
    reg [2:0] st = S_IDLE;
    reg       scl_q = 1'b1, sda_q = 1'b1;
    reg [3:0] cnt = 4'd0;                 // rising edges of SCL since the start of the byte
    reg [7:0] sh = 8'd0;
    reg [5:0] ptr = 6'd0;
    reg [7:0] ctl = 8'd0;
    reg [7:0] ram [0:55];
    reg [7:0] rbyte = 8'hFF;
    wire      scl_rise = scl_n & ~scl_q;
    wire      scl_fall = ~scl_n & scl_q;
    wire      start = scl_n & scl_q & sda_q & ~sda_n;
    wire      stop  = scl_n & scl_q & ~sda_q & sda_n;
    function [7:0] regread(input [5:0] p, input [55:0] t, input [7:0] c, input [7:0] r);
        regread = (p < 6'd7) ? t[8 * p[2:0] +: 8] : (p == 6'd7) ? c : r;
    endfunction
    wire [7:0] rd_ram = ram[(ptr >= 6'd8) ? (ptr - 6'd8) : 6'd0];
    wire [7:0] rd_cur = regread(ptr, time_bcd, ctl, rd_ram);
    wire [5:0] ptr_n = ptr + 6'd1;
    wire [7:0] rd_next = regread(ptr_n, time_bcd, ctl, ram[(ptr_n >= 6'd8) ? (ptr_n - 6'd8) : 6'd0]);
    integer i;
    initial for (i = 0; i < 56; i = i + 1) ram[i] = 8'd0;
    always @(posedge clk) begin
        scl_q <= scl_n; sda_q <= sda_n;
        if (rst) begin st <= S_IDLE; sda_pull <= 1'b0; cnt <= 4'd0; end
        else begin
            if (stop) begin st <= S_IDLE; sda_pull <= 1'b0; end
            else if (start) begin st <= S_ADDR; cnt <= 4'd0; sda_pull <= 1'b0; end
            else if (scl_rise) begin
                if (cnt < 4'd8) sh <= {sh[6:0], sda_n};
                cnt <= cnt + 4'd1;
                if (cnt == 4'd8 && st == S_RDATA) begin           // the master's acknowledge: a high bit ends the read
                    if (sda_n) st <= S_IDLE;
                    else begin ptr <= ptr_n; rbyte <= rd_next; end
                end
            end else if (scl_fall && st != S_IDLE) begin
                if (cnt == 4'd8) begin                              // eight bits are in: the ninth clock is the acknowledge
                    case (st)
                    S_ADDR: if (sh[7:1] == 7'h68) begin
                        sda_pull <= 1'b1;
                        st <= sh[0] ? S_RDATA : S_PTR;
                        if (sh[0]) rbyte <= rd_cur;
                    end else st <= S_IDLE;
                    S_PTR: begin ptr <= sh[5:0]; sda_pull <= 1'b1; st <= S_WDATA; end
                    S_WDATA: begin
                        sda_pull <= 1'b1;
                        if (ptr == 6'd7) ctl <= sh;
                        else if (ptr >= 6'd8) ram[ptr - 6'd8] <= sh;
                        ptr <= ptr_n;
                    end
                    default: sda_pull <= 1'b0;
                    endcase
                end else if (cnt == 4'd9) begin                     // the acknowledge clock is over
                    cnt <= 4'd0;
                    sda_pull <= (st == S_RDATA) ? ~rbyte[7] : 1'b0;
                end else if (st == S_RDATA) begin
                    sda_pull <= ~rbyte[3'd7 - cnt[2:0]];
                end else sda_pull <= 1'b0;
            end
        end
    end
endmodule
