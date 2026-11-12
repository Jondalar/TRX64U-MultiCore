// C64U keyboard: the keyboard's own controller (Bling board, behind J8)
// sends key events on BLING_KEYB, a UART line at 250 kbaud, 8N1, idle high.
// Worked out with the edge recorder in top.v (tools/bling_uart.py):
//   0x00-0x3F  key down, code = column * 8 + row of a matrix that is the
//              C64's (column = CIA 1 port A bit, row = port B bit) with
//              columns 0 and 7 swapped and rows 3 and 7 swapped: A = 0x0A
//              as on the C64, but RETURN = 0x01 (C64: column 7, row 1)
//   0x80-0xBF  the same key up
//   0x55       all keys up (comes instead of the last key-up code)
//   0x40/0xC0  RESTORE down/up
//   0x41       SHIFT LOCK pressed: the key does not latch, every press sends
//              0x41 and then 0x55, so the lock state is toggled here
//              (Gideon's bling_board.cc reads it as FLAGS bit 1 on each 0x41)
// The table came from pressing every key in turn (2026-10-01). Other codes
// from 0x42 up are counted in `other` and kept in `last`, so the
// multi-function switch and the like can be found later.
//
// keys[col*8 + row] = 1 while the key is down; SHIFT LOCK holds left SHIFT
// (column 1, row 7), as on the C64, unless lock_off. Every byte also goes
// out on last/rx_tgl for the app (bling_io).
module bling_keyboard #(
    parameter integer CLK_HZ = 31527800,
    parameter integer CLK_HZ_60 = 32745000,     // T40: clk in the 60 Hz System Modes
    parameter integer BAUD   = 250000
) (
    input  wire        clk,
    input  wire        v60,             // clk is CLK_HZ_60 (quasi-static)
    input  wire        rx,              // BLING_KEYB, already synchronised
    output wire [63:0] keys,
    output reg         restore = 1'b0,  // RESTORE held
    output reg   [7:0] last = 8'h00,    // last byte received
    output reg   [7:0] other = 8'h00,   // codes 0x42-0x7F seen
    output reg         rx_tgl = 1'b0,   // toggles with each byte taken into last
    output wire        locked,          // SHIFT LOCK is down
    input  wire        lock_off         // SHIFT LOCK holds no SHIFT (the app's menu scan)
);
    localparam integer BIT50  = CLK_HZ / BAUD;
    localparam integer BIT60  = CLK_HZ_60 / BAUD;
    (* ASYNC_REG = "TRUE" *) reg [1:0] v60_s = 2'b00;
    always @(posedge clk) v60_s <= {v60_s[0], v60};
    wire [8:0] BIT  = v60_s[1] ? BIT60[8:0] : BIT50[8:0];
    wire [8:0] HALF = {1'b0, BIT[8:1]};

    reg [63:0] down = 64'd0;
    reg        lock = 1'b0;
    assign keys = down | ({63'd0, lock & !lock_off} << 15);
    assign locked = lock;

    // the keyboard's code -> C64 matrix bit: swap columns 0/7 and rows 3/7
    function [5:0] c64_bit(input [5:0] code);
        reg [2:0] c, r;
        begin
            c = code[5:3];
            r = code[2:0];
            c = (c == 3'd0) ? 3'd7 : (c == 3'd7) ? 3'd0 : c;
            r = (r == 3'd3) ? 3'd7 : (r == 3'd7) ? 3'd3 : r;
            c64_bit = {c, r};
        end
    endfunction

    reg  [8:0] cnt = 9'd0;
    reg  [3:0] n = 4'd0;                // 0 idle, 1-8 data bits, 9 stop bit
    reg  [7:0] sh = 8'h00;
    always @(posedge clk) begin
        if (n == 4'd0) begin
            if (!rx) begin              // start bit: sample in the middle of each bit
                cnt <= BIT + HALF;
                n   <= 4'd1;
            end
        end else if (cnt != 9'd0)
            cnt <= cnt - 9'd1;
        else begin
            cnt <= BIT;
            if (n != 4'd9) begin
                sh <= {rx, sh[7:1]};    // LSB first
                n  <= n + 4'd1;
            end else begin
                n <= 4'd0;
                if (rx) begin           // good stop bit: take the byte
                    last   <= sh;
                    rx_tgl <= !rx_tgl;
                    if (sh == 8'h55) begin
                        down    <= 64'd0;
                        restore <= 1'b0;
                    end else if (sh[6:0] == 7'h40)
                        restore <= !sh[7];
                    else if (sh == 8'h41)
                        lock <= !lock;
                    else if (sh == 8'hC1)
                        ;
                    else if (!sh[6])
                        down[c64_bit(sh[5:0])] <= !sh[7];
                    else
                        other <= other + 8'd1;
                end
            end
        end
    end
endmodule
