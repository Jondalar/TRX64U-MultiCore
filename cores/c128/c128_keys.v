// T113 Q8 / D7: the C128's extra keys. The C64U's own keyboard is the C64 matrix (keys[63:0], bit col*8+row, 1 = pressed, col = PA line,
// row = PB line); the C128 has more: ESC, TAB, ALT, HELP, LINE FEED, NO SCROLL, the numeric pad (0-9 . + - ENTER), four separate cursor
// keys, CAPS LOCK, 40/80 DISPLAY - and, apart from them (Q6), the monitor-switch key of this core.
// A layer on the C64 keyboard (Alex, Q8 (c)): while CTRL and C= are held together, a key of the table below is the extra key it names
// and none of the keys involved reaches the machine as a C64 key. The USB keyboard's extra keys come in from the application's register
// (x_usb, c128_regs.v) and are or-ed in. The three special keys are given as levels; c128_regs.v toggles its latches on their rising edge.
//
// The extra matrix (columns K0-K2 on $D02F, rows on CIA 1 port B), bit k*8+r of xkeys_o (checked against the MiSTer core's keyboard
// as a reference: fpga64_keyboard.vhd, pbo(r) ki(k)):
//   K0: HELP, KP8, KP5, TAB, KP2, KP4, KP7, KP1      (rows 0-7)
//   K1: ESC,  KP+, KP-, LINE FEED, KP ENTER, KP6, KP9, KP3
//   K2: ALT,  KP0, KP., CURSOR UP, CURSOR DOWN, CURSOR LEFT, CURSOR RIGHT, NO SCROLL
// The layer (C64 key -> extra key), the table of the spec (5.5 "extra keys"):
//   F1 ESC, F3 TAB, F5 HELP, F7 LINE FEED, <- ALT, ^ NO SCROLL, I UP, J LEFT, K DOWN, L RIGHT,
//   1-9 0 . + - RETURN = KP1-KP9 KP0 KP. KP+ KP- KP ENTER, C CAPS LOCK, D 40/80 DISPLAY, V monitor switch.
`timescale 1ns / 1ps
module c128_keys (
    input  wire [63:0] keys,        // the C64U's keyboard, C64 matrix
    input  wire [23:0] x_usb,       // extra keys from the USB keyboard (the application)
    output wire [63:0] keys_o,      // the C64 keys the machine sees
    output wire [23:0] xkeys_o,     // the extra matrix
    output wire        sp_caps,     // levels of the special keys
    output wire        sp_4080,
    output wire        sp_mon
);
    localparam integer K_CTRL = 7 * 8 + 2;
    localparam integer K_CBM  = 7 * 8 + 5;
    wire act = keys[K_CTRL] & keys[K_CBM];
    // table: source key (col*8+row) and the destination bit in xkeys_o (k*8+r); 255 = a special key
    localparam integer N = 27;
    function integer src(input integer n);
        case (n)
            0: src = 4;   1: src = 5;   2: src = 6;   3: src = 3;   4: src = 57;  5: src = 54;
            6: src = 33;  7: src = 34;  8: src = 37;  9: src = 42;
            10: src = 56; 11: src = 59; 12: src = 8;  13: src = 11; 14: src = 16; 15: src = 19; 16: src = 24; 17: src = 27;
            18: src = 32; 19: src = 35; 20: src = 44; 21: src = 40; 22: src = 43; 23: src = 1;
            24: src = 20; 25: src = 18; 26: src = 31;
            default: src = 0;
        endcase
    endfunction
    function integer dst(input integer n);
        case (n)
            0: dst = 8;   1: dst = 3;   2: dst = 0;   3: dst = 11;  4: dst = 16;  5: dst = 23;
            6: dst = 19;  7: dst = 21;  8: dst = 20;  9: dst = 22;
            10: dst = 7;  11: dst = 4;  12: dst = 15; 13: dst = 5;  14: dst = 2;  15: dst = 13; 16: dst = 6;  17: dst = 1;
            18: dst = 14; 19: dst = 17; 20: dst = 18; 21: dst = 9;  22: dst = 10; 23: dst = 12;
            default: dst = 255;
        endcase
    endfunction

    reg [23:0] xl;
    reg [63:0] mask;
    reg        spc, sp4, spm;
    integer n;
    always @(*) begin
        xl = 24'd0; mask = 64'd0; spc = 1'b0; sp4 = 1'b0; spm = 1'b0;
        if (act) begin
            mask[K_CTRL] = 1'b1; mask[K_CBM] = 1'b1;
            for (n = 0; n < N; n = n + 1) begin
                mask[src(n)] = 1'b1;
                if (keys[src(n)]) begin
                    if (n == 24) spc = 1'b1;
                    else if (n == 25) sp4 = 1'b1;
                    else if (n == 26) spm = 1'b1;
                    else xl[dst(n)] = 1'b1;
                end
            end
        end
    end
    assign keys_o  = keys & ~mask;
    assign xkeys_o = xl | x_usb;
    assign sp_caps = spc;
    assign sp_4080 = sp4;
    assign sp_mon  = spm;
endmodule
