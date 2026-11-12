// T143 D7: the host's controllers into the Neo Geo's: both C64 joystick ports (up, down, left, right, fire = A) and the C64 keyboard (the buttons a one-button
// stick has not) into the two 16 bit joystick words of neogeo.sv (MiSTer layout: bit 0 right, 1 left, 2 down, 3 up, 4 A, 5 B, 6 C, 7 D, 8 Start, 9 Select, 10 Coin).
//   player 1: port 1 (a stick: A is the fire button);  B = SPACE, C = Z, D = X, Start = F1, Select = F3, Coin = F5
//   player 2: port 2;                                   B = RIGHT SHIFT, C = /, D = ",", Start = F7, Select = "=", Coin = "+"
// Written for this project (TREX), no upstream code; the idea of nes_pad.v.
// T156: the application's USB pads (`app1`/`app2`, the registers PAD1 / PAD2 of neo_regs, cpu_clk) are one more source, OR-ed in; their bit n is the machine's word bit n.
`timescale 1ns / 1ps
module neo_pad (
    input  wire        clk,                 // the machine clock (48 MHz)
    input  wire [4:0]  joy1,                // the host's joystick 1: up, down, left, right, fire (1 = pressed), cpu_clk
    input  wire [4:0]  joy2,
    input  wire [63:0] keys,                // the C64 matrix, keys[col*8 + row] = 1 while that key is down
    input  wire        swap,                // the ports exchanged
    input  wire [15:0] app1,                // T156: the application's USB pads, the machine's joystick word, cpu_clk (quasi static, two flip-flops)
    input  wire [15:0] app2,
    output wire [15:0] pad0,
    output wire [15:0] pad1
);
    // the keys as indices into the matrix (host-app/software/io/c64/keyboard_c64.cc keymap_normal; the same table nes_pad.v and zxn_keys.v read)
    localparam integer K_F7 = 3, K_F1 = 4, K_F3 = 5, K_F5 = 6, K_Z = 12, K_X = 23, K_PLUS = 40, K_COMMA = 47, K_EQ = 53, K_RSHIFT = 52, K_SLASH = 55, K_SPACE = 60;
    (* ASYNC_REG = "TRUE" *) reg [4:0] j1_s1 = 5'd0, j1_s2 = 5'd0, j2_s1 = 5'd0, j2_s2 = 5'd0;
    (* ASYNC_REG = "TRUE" *) reg [15:0] k_s1 = 16'd0, k_s2 = 16'd0;
    (* ASYNC_REG = "TRUE" *) reg [15:0] a1_s1 = 16'd0, a1_s2 = 16'd0, a2_s1 = 16'd0, a2_s2 = 16'd0;
    wire [15:0] k_raw = {keys[K_PLUS], keys[K_EQ], keys[K_F7], keys[K_COMMA], keys[K_SLASH], keys[K_RSHIFT],
                         keys[K_F5], keys[K_F3], keys[K_F1], keys[K_X], keys[K_Z], keys[K_SPACE], 4'd0};
    always @(posedge clk) begin
        j1_s1 <= swap ? joy2 : joy1;  j1_s2 <= j1_s1;
        j2_s1 <= swap ? joy1 : joy2;  j2_s2 <= j2_s1;
        k_s1  <= k_raw;               k_s2  <= k_s1;
        a1_s1 <= app1;                a1_s2 <= a1_s1;
        a2_s1 <= app2;                a2_s2 <= a2_s1;
    end
    wire p1_b = k_s2[4], p1_c = k_s2[5], p1_d = k_s2[6], p1_sta = k_s2[7], p1_sel = k_s2[8], p1_coin = k_s2[9];
    wire p2_b = k_s2[10], p2_c = k_s2[11], p2_d = k_s2[12], p2_sta = k_s2[13], p2_sel = k_s2[14], p2_coin = k_s2[15];
    // joy: [0] up, [1] down, [2] left, [3] right, [4] fire
    wire [15:0] b0 = {5'd0, p1_coin, p1_sel, p1_sta, p1_d, p1_c, p1_b, j1_s2[4], j1_s2[0], j1_s2[1], j1_s2[2], j1_s2[3]};
    wire [15:0] b1 = {5'd0, p2_coin, p2_sel, p2_sta, p2_d, p2_c, p2_b, j2_s2[4], j2_s2[0], j2_s2[1], j2_s2[2], j2_s2[3]};
    assign pad0 = b0 | {5'd0, a1_s2[10:0]};
    assign pad1 = b1 | {5'd0, a2_s2[10:0]};
endmodule
