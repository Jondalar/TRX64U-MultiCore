// T142 D7: the SNES's two controller ports, driven by the C64's own hardware (the NES core's choice, T95 5.18: the joystick ports give what a one-button
// stick has, the keyboard the rest). The mapping is a proposal for Alex (T142 Q5), a table in this file only:
//
//   port 1: up/down/left/right and B from c64_joy1 (fire), A = SPACE, Y = Z, X = X, L = A, R = S, Select = F3, Start = F1
//   port 2: up/down/left/right and B from c64_joy2 (fire), A = RIGHT SHIFT, Y = comma, X = full stop, L = K, R = L, Select = F7, Start = F5
//
// The joystick and key sources need neither the host nor the application: `c64_joy1/2` and `c64_keys` are contract lines (contract/layer2_ports.txt).
// T156: the application's USB pads (`app1`/`app2`, the registers PAD1 / PAD2 of snes_regs) are one more source, OR-ed with these (`c1`/`c2`); their bit
// n is the serial bit n (B first).
// The serial protocol is the hardware's (SNES_MiSTer rtl/ioport.sv): the S-CPU's latch (`strobe`, JOY_STRB) loads the 16 bit register while it is
// high, the rising edge of the port's clock shifts it on, the data line is the register's top bit, and the first bit is B. The register holds
// the buttons active low in the order B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R, then four ones; after the sixteenth bit the data
// line goes LOW (a pressed button is a low line, the CPU register inverts it: $4016 bit 0 reads 1), as on a real controller whose shift register takes a
// grounded input: ioport.sv shifts zeros in. Donkey Kong Country relies on it: after the automatic read it reads $4016 / $4017 once by hand and treats a
// 0 in bit 0 as "no pad plugged in" (it clears its button state; T142 spec 5, 2026-10-31). The second data line (IOBit / multitap) is high: no multitap.
`timescale 1ns / 1ps
module snes_pad (
    input  wire        clk,                   // cpu_clk, the clock the machine runs on
    input  wire        rst,                   // the machine's reset
    input  wire [4:0]  joy1,                  // the host's joystick 1: up, down, left, right, fire (1 = pressed)
    input  wire [4:0]  joy2,
    input  wire [63:0] keys,                  // the C64 matrix, keys[col*8 + row] = 1 while that key is down
    input  wire [15:0] app1,                  // T156: the application's USB pad, bit n = serial bit n (B Y Select Start Up Down Left Right A X L R), cpu_clk
    input  wire [15:0] app2,
    input  wire        strobe,                // JOY_STRB
    input  wire        clk1,                  // JOY1_CLK
    input  wire        clk2,                  // JOY2_CLK
    output wire [1:0]  di1,                   // JOY1_DI
    output wire [1:0]  di2
);
    // The keys, as indices into the matrix (col*8 + row of the C64 keyboard: host-app/software/io/c64/keyboard_c64.cc, keymap_normal).
    localparam integer K_F7 = 3, K_F1 = 4, K_F3 = 5, K_F5 = 6;                    // column 0
    localparam integer K_A = 1*8 + 2, K_Z = 1*8 + 4, K_S = 1*8 + 5;               // column 1
    localparam integer K_X = 2*8 + 7;                                              // column 2
    localparam integer K_K = 4*8 + 5;                                              // column 4
    localparam integer K_STOP = 5*8 + 4, K_L = 5*8 + 2, K_COMMA = 5*8 + 7;        // column 5 (full stop, L, comma)
    localparam integer K_RSHIFT = 6*8 + 4, K_SPACE = 7*8 + 4;                     // columns 6 and 7

    // sys_clk to cpu_clk. Buttons only: two flip-flops are enough, and a bit that arrives a clock late is not visible to a machine that
    // reads its ports once a frame.
    (* ASYNC_REG = "TRUE" *) reg [4:0] j1_s1 = 5'd0, j1_s2 = 5'd0, j2_s1 = 5'd0, j2_s2 = 5'd0;
    (* ASYNC_REG = "TRUE" *) reg [15:0] k_s1 = 16'd0, k_s2 = 16'd0;
    wire [15:0] k_raw = {keys[K_SPACE], keys[K_Z], keys[K_X], keys[K_A], keys[K_S], keys[K_F3], keys[K_F1],
                         keys[K_RSHIFT], keys[K_COMMA], keys[K_STOP], keys[K_K], keys[K_L], keys[K_F7], keys[K_F5], 2'b00};
    always @(posedge clk) begin
        j1_s1 <= joy1;  j1_s2 <= j1_s1;
        j2_s1 <= joy2;  j2_s2 <= j2_s1;
        k_s1  <= k_raw; k_s2  <= k_s1;
    end
    wire k_space = k_s2[15], k_z = k_s2[14], k_x = k_s2[13], k_a = k_s2[12], k_s = k_s2[11], k_f3 = k_s2[10], k_f1 = k_s2[9];
    wire k_rshift = k_s2[8], k_comma = k_s2[7], k_stop = k_s2[6], k_k = k_s2[5], k_l = k_s2[4], k_f7 = k_s2[3], k_f5 = k_s2[2];

    // joy: up, down, left, right, fire as c64_joy bits [4:0]? The contract's order is up, down, left, right, fire from bit 0 (nes_pad.v uses j[3:0] as
    // up..right in this order for the NES byte {right, left, down, up}), so bit 0 = up, 1 = down, 2 = left, 3 = right, 4 = fire.
    wire [11:0] b1 = {j1_s2[4], k_z, k_f3, k_f1, j1_s2[0], j1_s2[1], j1_s2[2], j1_s2[3], k_space, k_x, k_a, k_s};     // B Y Select Start Up Down Left Right A X L R
    wire [11:0] b2 = {j2_s2[4], k_comma, k_f7, k_f5, j2_s2[0], j2_s2[1], j2_s2[2], j2_s2[3], k_rshift, k_stop, k_k, k_l};
    // T156: the USB pads join by OR. The registers are written by the application in cpu_clk, the clock of this module: no crossing. The word is in
    // the serial order (bit 0 = B), b1 is {B ... R} with B at bit 11: reversed.
    wire [11:0] a1 = {app1[0], app1[1], app1[2], app1[3], app1[4], app1[5], app1[6], app1[7], app1[8], app1[9], app1[10], app1[11]};
    wire [11:0] a2 = {app2[0], app2[1], app2[2], app2[3], app2[4], app2[5], app2[6], app2[7], app2[8], app2[9], app2[10], app2[11]};
    wire [11:0] c1 = b1 | a1, c2 = b2 | a2;
    wire [15:0] l1 = ~{c1, 4'b0000};
    wire [15:0] l2 = ~{c2, 4'b0000};
    localparam FILL = 1'b0;

    reg [15:0] sh1 = 16'hFFFF, sh2 = 16'hFFFF;
    reg        c1_q = 1'b0, c2_q = 1'b0;
    always @(posedge clk) begin
        if (rst) begin
            sh1 <= 16'hFFFF; sh2 <= 16'hFFFF; c1_q <= 1'b0; c2_q <= 1'b0;
        end else begin
            if (strobe) begin
                sh1 <= l1; sh2 <= l2;
            end else begin
                if (!c1_q && clk1) sh1 <= {sh1[14:0], FILL};
                if (!c2_q && clk2) sh2 <= {sh2[14:0], FILL};
            end
            c1_q <= clk1; c2_q <= clk2;
        end
    end
    assign di1 = {1'b1, sh1[15]};
    assign di2 = {1'b1, sh2[15]};
endmodule
