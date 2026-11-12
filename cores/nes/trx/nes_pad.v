// T95 5.18: the controller ports. The NES's two ports are driven by the C64's own hardware, decided with Alex on 2026-10-15:
// the joystick ports give the four directions and A, the keyboard gives the three buttons a one-button stick has not
// ("map the C64 joystick + the keyboard sensibly ... F1 = Start, F3 = Select", translated).
//
//   port 1: up/down/left/right and A from c64_joy1, B = space, Select = F3, Start = F1
//   port 2: up/down/left/right and A from c64_joy2, B = right shift, Select = F7, Start = F5
//
// The joystick and key sources need neither the host nor the application: `c64_joy1/2` and `c64_keys` are contract lines
// (contract/layer2_ports.txt), so the keys reach the core in the clock they are pressed, not a service tick later. The host ORs the
// physical lines with the ones software may drive (host/app_input.v). T156: the application's USB pads (`app1`/`app2`, the registers
// PAD1 / PAD2 of nes_regs, written on every report and cleared when the pad leaves) are one more source, OR-ed in the same place
// (`q1`/`q2`) in the register's own bit order, which is the controller's: A B Select Start Up Down Left Right from bit 0.
//
// The serial protocol is the hardware's: `strobe` (nes.v `joypad_out[0]`, the $4016 write) latches the buttons while it is high,
// and the machine's read of $4016 / $4017 (`rdclk`, nes.v `joypad_clock`) shifts the next bit out on its falling edge. Bit 0
// goes out first and the order is A, B, Select, Start, Up, Down, Left, Right. The register is 24 bits with ones above the eight
// buttons, like NES_MiSTer's NES.sv: on an NES (not a Famicom) the ninth and later reads of a standard controller return 1.
`timescale 1ns / 1ps
module nes_pad (
    input  wire        clk,                   // cpu_clk, the clock the machine runs on
    input  wire        rst,                   // the machine's reset
    input  wire [4:0]  joy1,                  // the host's joystick 1: up, down, left, right, fire (1 = pressed)
    input  wire [4:0]  joy2,
    input  wire        joy_swap,              // T157: 1 = the host's joystick 2 drives controller port 1 and joystick 1 port 2 (cpu_clk, quasi static)
    input  wire [63:0] keys,                  // the C64 matrix, keys[col*8 + row] = 1 while that key is down
    input  wire [7:0]  app1,                  // T156: the application's USB pad, the serial order (0 A, 1 B, 2 Select, 3 Start, 4 Up, 5 Down, 6 Left, 7 Right), cpu_clk
    input  wire [7:0]  app2,
    input  wire        strobe,                // nes.v joypad_out[0]
    input  wire [1:0]  rdclk,                 // nes.v joypad_clock: high while the machine reads the port
    output wire [4:0]  data1,                 // nes.v joypad1_data
    output wire [4:0]  data2
);
    // The keys, as indices into the matrix. Group 0 is DEL, RETURN, CRSR right, F7, F1, F3, F5, CRSR down; group 6 bit 4 is the
    // right shift; group 7 bit 4 is the space bar (host-app/software/io/c64/keyboard_c64.cc, keymap_normal).
    localparam integer K_F7 = 3, K_F1 = 4, K_F3 = 5, K_F5 = 6, K_RSHIFT = 52, K_SPACE = 60;

    // sys_clk to cpu_clk. Buttons only: two flip-flops are enough, and a bit that arrives a clock late is not visible to a
    // machine that reads its ports once a frame.
    (* ASYNC_REG = "TRUE" *) reg [4:0] j1_s1 = 5'd0, j1_s2 = 5'd0, j2_s1 = 5'd0, j2_s2 = 5'd0;
    (* ASYNC_REG = "TRUE" *) reg [5:0] k_s1 = 6'd0, k_s2 = 6'd0;
    wire [5:0] k_raw = {keys[K_SPACE], keys[K_F3], keys[K_F1], keys[K_RSHIFT], keys[K_F7], keys[K_F5]};
    always @(posedge clk) begin
        j1_s1 <= joy_swap ? joy2 : joy1;  j1_s2 <= j1_s1;
        j2_s1 <= joy_swap ? joy1 : joy2;  j2_s2 <= j2_s1;
        k_s1  <= k_raw; k_s2  <= k_s1;
    end
    wire k_space = k_s2[5], k_f3 = k_s2[4], k_f1 = k_s2[3], k_rshift = k_s2[2], k_f7 = k_s2[1], k_f5 = k_s2[0];

    wire p1_a = j1_s2[4], p1_b = k_space,  p1_sel = k_f3, p1_sta = k_f1;
    wire p2_a = j2_s2[4], p2_b = k_rshift, p2_sel = k_f7, p2_sta = k_f5;
    wire [7:0] p1 = {j1_s2[3], j1_s2[2], j1_s2[1], j1_s2[0], p1_sta, p1_sel, p1_b, p1_a};
    wire [7:0] p2 = {j2_s2[3], j2_s2[2], j2_s2[1], j2_s2[0], p2_sta, p2_sel, p2_b, p2_a};
    // T156: the USB pads join the sources by OR. The registers are written by the application in cpu_clk, the clock of this module: no crossing.
    wire [7:0] q1 = p1 | app1, q2 = p2 | app2;

    reg [23:0] sh1 = 24'd0, sh2 = 24'd0;
    reg [1:0]  rdclk_q = 2'd0;
    always @(posedge clk) begin
        if (rst) begin
            sh1 <= 24'd0;
            sh2 <= 24'd0;
            rdclk_q <= 2'd0;
        end else begin
            if (strobe) begin
                sh1 <= {16'hFFFF, q1};
                sh2 <= {16'hFFFF, q2};
            end
            // A read while the strobe is high keeps the first bit: the shift wins the clock, as in NES.sv
            if (!rdclk[0] && rdclk_q[0]) sh1 <= {1'b0, sh1[23:1]};
            if (!rdclk[1] && rdclk_q[1]) sh2 <= {1'b0, sh2[23:1]};
            rdclk_q <= rdclk;
        end
    end

    // The port's other four lines are the expansion ones (microphone, Zapper, Power Pad): no device, so 0.
    assign data1 = {4'b0000, sh1[0]};
    assign data2 = {4'b0000, sh2[0]};
endmodule
