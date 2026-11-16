// T85: the C64 Ultimate's keyboard matrix, joysticks and the application's mouse deltas as the Minimig wants
// them (the events the MCU's hid.v made: kbd_mouse_level / type / data). Written for this project.
//
// keys[col * 8 + row] is the C64 matrix (CIA 1 port A = column, port B = row), 1 = down. The Amiga keys that
// the C64 has not got are not on the matrix (Q6: C64 matrix only); the application sends them as raw key events (T141, rk_*; the A1200 has its own copy, cores/amiga1200/rtl/amiga_input.v); LEFT ARROW is Escape (Alex). Table: KEYMAP below.
// A key event is: level toggles, type 2, data = {release, raw code}. Events are spaced (a key every ~1.2 ms; the
// keyboard of an Amiga cannot send faster, the CIA holds one code), mouse events (type 0 = x, 1 = y) follow
// each other 64 ticks apart. All outputs change on ce28 ticks only.
// Joystick ports: the C64's port 2 is the Amiga's joystick port (port 2), the C64's port 1 the mouse port.
// T141 (the A500's file; the A1200 has its own copy): rk_data / rk_tgl / rk_room, a queue of 16 raw key events
// ({release, code}) that the application fills (a USB keyboard); an event leaves when nothing else is due, with the same spacing as a matrix key.
// Ctrl + Commodore + RESTORE (= Ctrl + left Amiga + right Amiga) pulses reset_kbd, the Amiga's reset.
`timescale 1ns / 1ps
module amiga_input (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        ce28,
    input  wire [63:0] keys,
    input  wire        restore,
    input  wire [4:0]  joy1,
    input  wire [4:0]  joy2,
    input  wire [10:0] pad_word,                    // T225: the USB pad's state for game port 2 {play, reverse, forward, yellow, green, blue/fire2, red/fire, right, left, down, up} (cpu_clk, quasi static)
    input  wire [1:0]  pad_mode,                    // T225: 0 port 2 as before (the host's joystick port), 1 one-button joystick, 2 two-button joystick, 3 CD32 pad; (cpu_clk, quasi static)
    input  wire        joy_swap,                    // T157: 1 = the host's port 2 is the Amiga's port 1 and the host's port 1 its port 2
    // the application's mouse (cpu_clk values, quasi static; the toggle says a new pair is there)
    input  wire [7:0]  mouse_dx,
    input  wire [7:0]  mouse_dy,
    input  wire [2:0]  mouse_btn,
    input  wire        mouse_tgl,
    // T141: raw Amiga key codes from the application (a USB keyboard): {release, code}, quasi static, the toggle says a new byte is there
    input  wire [7:0]  rk_data,
    input  wire        rk_tgl,
    output wire        rk_room,                     // 1 = the queue holds 8 events or fewer (a single bit, safe to read from another clock)
    output reg         kbd_mouse_level = 1'b0,
    output reg  [1:0]  kbd_mouse_type = 2'd0,
    output reg  [7:0]  kbd_mouse_data = 8'd0,
    output wire [2:0]  mouse_buttons,
    output wire [10:0] joystick0,
    output wire        cd32pad2_en,
    output wire [7:0]  joystick1,
    output reg         reset_kbd = 1'b0
);
    // Amiga raw codes, index = col * 8 + row; 7F = no key
    function [6:0] keymap(input [5:0] i);
        case (i)
        // col 0: DEL RETURN CRSR-RT F7 F1 F3 F5 CRSR-DN
        6'd0: keymap = 7'h41; 6'd1: keymap = 7'h44; 6'd2: keymap = 7'h4E; 6'd3: keymap = 7'h56;
        6'd4: keymap = 7'h50; 6'd5: keymap = 7'h52; 6'd6: keymap = 7'h54; 6'd7: keymap = 7'h4D;
        // col 1: 3 W A 4 Z S E LSHIFT
        6'd8: keymap = 7'h03; 6'd9: keymap = 7'h11; 6'd10: keymap = 7'h20; 6'd11: keymap = 7'h04;
        6'd12: keymap = 7'h31; 6'd13: keymap = 7'h21; 6'd14: keymap = 7'h12; 6'd15: keymap = 7'h60;
        // col 2: 5 R D 6 C F T X
        6'd16: keymap = 7'h05; 6'd17: keymap = 7'h13; 6'd18: keymap = 7'h22; 6'd19: keymap = 7'h06;
        6'd20: keymap = 7'h33; 6'd21: keymap = 7'h23; 6'd22: keymap = 7'h14; 6'd23: keymap = 7'h32;
        // col 3: 7 Y G 8 B H U V
        6'd24: keymap = 7'h07; 6'd25: keymap = 7'h15; 6'd26: keymap = 7'h24; 6'd27: keymap = 7'h08;
        6'd28: keymap = 7'h35; 6'd29: keymap = 7'h25; 6'd30: keymap = 7'h16; 6'd31: keymap = 7'h34;
        // col 4: 9 I J 0 M K O N
        6'd32: keymap = 7'h09; 6'd33: keymap = 7'h17; 6'd34: keymap = 7'h26; 6'd35: keymap = 7'h0A;
        6'd36: keymap = 7'h37; 6'd37: keymap = 7'h27; 6'd38: keymap = 7'h18; 6'd39: keymap = 7'h36;
        // col 5: + P L - . : @ ,
        6'd40: keymap = 7'h5E; 6'd41: keymap = 7'h19; 6'd42: keymap = 7'h28; 6'd43: keymap = 7'h0B;
        6'd44: keymap = 7'h39; 6'd45: keymap = 7'h2A; 6'd46: keymap = 7'h1A; 6'd47: keymap = 7'h38;
        // col 6: pound * ; HOME RSHIFT = up-arrow /
        6'd48: keymap = 7'h2B; 6'd49: keymap = 7'h5D; 6'd50: keymap = 7'h29; 6'd51: keymap = 7'h5F;
        6'd52: keymap = 7'h61; 6'd53: keymap = 7'h0C; 6'd54: keymap = 7'h0D; 6'd55: keymap = 7'h3A;
        // col 7: 1 LEFT-ARROW CTRL 2 SPACE C= Q RUN/STOP
        6'd56: keymap = 7'h01; 6'd57: keymap = 7'h45; 6'd58: keymap = 7'h63; 6'd59: keymap = 7'h02;
        6'd60: keymap = 7'h40; 6'd61: keymap = 7'h66; 6'd62: keymap = 7'h10; 6'd63: keymap = 7'h42;
        default: keymap = 7'h7F;
        endcase
    endfunction
    // keys whose code changes with SHIFT (the C64 has them shifted): CRSR-RT, F7, F1, F3, F5, CRSR-DN
    function [6:0] shifted(input [5:0] i);
        case (i)
        6'd2: shifted = 7'h4F; 6'd3: shifted = 7'h57; 6'd4: shifted = 7'h51; 6'd5: shifted = 7'h53;
        6'd6: shifted = 7'h55; 6'd7: shifted = 7'h4C;
        default: shifted = 7'h7F;
        endcase
    endfunction

    // matrix plus RESTORE (position 64 = right Amiga)
    wire [64:0] kin = {restore, keys};
    reg  [64:0] prev = 65'd0;
    reg  [6:0]  kpos = 7'd0;
    reg  [5:0]  sh_var = 6'd0;                      // bit i-2: the shifted variant of key i (2..7) was sent
    reg  [16:0] gap = 17'd0;
    reg         ev_pending = 1'b0;
    reg  [7:0]  ev_data = 8'd0;
    wire        shift_down = kin[15] | kin[52];
    wire [5:0]  kk = kpos[5:0];
    wire [6:0]  code_n = keymap(kk);
    wire [6:0]  code_s = shifted(kk);
    wire [2:0]  vi = kk[2:0] - 3'd2;

    reg ctrl_d = 1'b0, lami_d = 1'b0, rami_d = 1'b0, combo_d = 1'b0;

    // mouse events: x then y
    (* ASYNC_REG = "TRUE" *) reg [2:0] mt_s = 3'b000;
    reg  [7:0] m_dx = 8'd0, m_dy = 8'd0;
    reg  [1:0] m_state = 2'd0;
    assign mouse_buttons = mouse_btn;
    // T141: the raw key queue, 16 events; the application writes only while rk_room says so
    (* ASYNC_REG = "TRUE" *) reg [2:0] rk_s = 3'b000;
    reg  [7:0] rk_q [0:15];
    reg  [3:0] rk_wp = 4'd0, rk_rp = 4'd0;
    reg  [4:0] rk_cnt = 5'd0;
    wire       rk_push = rk_s[2] ^ rk_s[1];
    wire       rk_take = ce28 && gap == 17'd0 && !ev_pending && m_state == 2'd0 && rk_cnt != 5'd0;   // the queue's turn: nothing else is due
    wire       rk_ok = rk_push && rk_cnt != 5'd16;
    wire [3:0] rk_step = 4'd1;
    assign rk_room = rk_cnt <= 5'd8;

    // joysticks: host {fire, right, left, down, up} = joy[4], joy[3], joy[2], joy[1], joy[0]
    //            Amiga  bit 0 right, 1 left, 2 down, 3 up, 4 fire
    wire [4:0] ja = joy_swap ? joy2 : joy1;
    wire [4:0] jb = joy_swap ? joy1 : joy2;
    assign joystick1 = {3'b000, jb[4], jb[0], jb[1], jb[2], jb[3]};
    // T225 D1: with the USB pad on game port 2 (mode != 0) the port comes from the pad only, the host's joystick port is off for it
    (* ASYNC_REG = "TRUE" *) reg [10:0] pw_a = 11'd0, pw_b = 11'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0]  pm_a = 2'd0,  pm_b = 2'd0;
    always @(posedge sys_clk) begin pw_a <= pad_word; pw_b <= pw_a; pm_a <= pad_mode; pm_b <= pm_a; end
    //            Amiga  bit 0 right, 1 left, 2 down, 3 up, 4 fire/red, 5 blue (2nd button), 6 green, 7 yellow, 8 forward, 9 reverse, 10 play
    wire [10:0] pad_j = {pw_b[8], pw_b[9], pw_b[10], pw_b[7:4], pw_b[0], pw_b[1], pw_b[2], pw_b[3]};
    assign joystick0 = (pm_b == 2'd0) ? {6'b000000, ja[4], ja[0], ja[1], ja[2], ja[3]} : pad_j;
    assign cd32pad2_en = (pm_b == 2'd3);

    always @(posedge sys_clk) begin
        mt_s <= {mt_s[1:0], mouse_tgl};
        rk_s <= {rk_s[1:0], rk_tgl};
        if (rk_ok) begin rk_q[rk_wp] <= rk_data; rk_wp <= rk_wp + 4'd1; end
        rk_cnt <= rk_cnt + (rk_ok ? 5'd1 : 5'd0) - (rk_take ? 5'd1 : 5'd0);
        if (mt_s[2] ^ mt_s[1]) begin m_dx <= mouse_dx; m_dy <= mouse_dy; m_state <= 2'd1; end
        if (ce28) begin
            reset_kbd <= 1'b0;
            if (gap != 17'd0) gap <= gap - 17'd1;
            else if (ev_pending) begin
                kbd_mouse_level <= ~kbd_mouse_level; kbd_mouse_type <= 2'd2; kbd_mouse_data <= ev_data;
                ev_pending <= 1'b0; gap <= 17'd32768;
            end else if (m_state == 2'd1) begin
                kbd_mouse_level <= ~kbd_mouse_level; kbd_mouse_type <= 2'd0; kbd_mouse_data <= m_dx;
                m_state <= 2'd2; gap <= 17'd64;
            end else if (m_state == 2'd2) begin
                kbd_mouse_level <= ~kbd_mouse_level; kbd_mouse_type <= 2'd1; kbd_mouse_data <= m_dy;
                m_state <= 2'd0; gap <= 17'd64;
            end else if (rk_take) begin
                ev_data <= rk_q[rk_rp]; ev_pending <= 1'b1; rk_rp <= rk_rp + rk_step;
            end else begin
                // look at one matrix position per tick
                kpos <= (kpos == 7'd64) ? 7'd0 : kpos + 7'd1;
                if (kin[kpos] != prev[kpos]) begin
                    prev[kpos] <= kin[kpos];
                    if (kpos == 7'd64) begin
                        ev_data <= {~kin[kpos], 7'h67}; ev_pending <= 1'b1;
                    end else if (code_n != 7'h7F) begin
                        if (kin[kpos]) begin
                            if (code_s != 7'h7F && shift_down) begin
                                ev_data <= {1'b0, code_s}; sh_var[vi] <= 1'b1;
                            end else begin
                                ev_data <= {1'b0, code_n};
                                if (code_s != 7'h7F) sh_var[vi] <= 1'b0;
                            end
                        end else if (code_s != 7'h7F && sh_var[vi]) ev_data <= {1'b1, code_s};
                        else ev_data <= {1'b1, code_n};
                        ev_pending <= 1'b1;
                    end
                end
            end
            // the reset combination
            ctrl_d <= kin[58]; lami_d <= kin[61]; rami_d <= kin[64];
            combo_d <= ctrl_d & lami_d & rami_d;
            if ((ctrl_d & lami_d & rami_d) && !combo_d) reset_kbd <= 1'b1;
        end
        if (sys_rst) begin prev <= 65'd0; ev_pending <= 1'b0; m_state <= 2'd0; rk_wp <= 4'd0; rk_rp <= 4'd0; rk_cnt <= 5'd0; end
    end
endmodule
