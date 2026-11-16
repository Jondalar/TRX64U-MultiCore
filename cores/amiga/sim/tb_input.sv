// T85: amiga_input (keys, RESTORE, shifted cursor keys, the reset combination, mouse pair, joystick ports) and amiga_audio.
`timescale 1ns/1ps
module tb_input;
    reg sys_clk = 0, mclk = 0;
    always #15.86 sys_clk = ~sys_clk;
    always #20.35 mclk = ~mclk;
    reg ce28 = 1; reg [3:0] n10 = 0;
    always @(posedge sys_clk) begin n10 <= (n10 == 9) ? 0 : n10 + 1; ce28 <= (n10 != 3); end
    reg sys_rst = 1;
    initial #1000 sys_rst = 0;
    reg [63:0] keys = 0; reg restore = 0; reg [4:0] joy1 = 0, joy2 = 0; reg jswap = 0;
    reg [7:0] mdx = 0, mdy = 0; reg [2:0] mbtn = 0; reg mtgl = 0;
    wire kml; wire [1:0] kmt; wire [7:0] kmd; wire [2:0] mb; wire [10:0] j0; wire [7:0] j1; wire cd32; wire rk;
    amiga_input dut (.sys_clk(sys_clk), .sys_rst(sys_rst), .ce28(ce28), .keys(keys), .restore(restore), .joy1(joy1), .joy2(joy2), .joy_swap(jswap), .pad_word(11'd0), .pad_mode(2'd0),
        .mouse_dx(mdx), .mouse_dy(mdy), .mouse_btn(mbtn), .mouse_tgl(mtgl),
        .kbd_mouse_level(kml), .kbd_mouse_type(kmt), .kbd_mouse_data(kmd), .mouse_buttons(mb), .joystick0(j0), .cd32pad2_en(cd32), .joystick1(j1),
        .reset_kbd(rk));
    reg [1:0] ev_t [0:63]; reg [7:0] ev_d [0:63]; integer nev = 0;
    always @(kml) if (!sys_rst) begin ev_t[nev] = kmt; ev_d[nev] = kmd; nev = nev + 1; end
    integer errors = 0;
    task check(input cond, input [8*64-1:0] what);
        begin if (!cond) begin errors = errors + 1; $display("FAIL: %0s", what); end else $display("ok  : %0s", what); end
    endtask
    integer n0, rk_seen = 0;
    always @(posedge sys_clk) if (rk) rk_seen <= rk_seen + 1;
    // audio
    reg signed [14:0] al = 0, ar = 0; wire signed [23:0] oal, oar;
    reg [8:0] adiv = 0; always @(posedge mclk) adiv <= adiv + 9'd1;
    reg [3:0] cdiv = 0; always @(posedge sys_clk) cdiv <= (cdiv == 4'd9) ? 4'd0 : cdiv + 4'd1;
    amiga_audio aud (.sys_clk(sys_clk), .ce28(cdiv != 4'd9), .mclk(mclk), .sample_en(adiv == 9'd0), .left(al), .right(ar), .mute(1'b0), .clr(1'b0),
                     .audio_l(oal), .audio_r(oar), .st_under(), .st_over(), .st_fill());
    initial begin
        #5000;
        n0 = nev; keys[10] = 1; #1500000;                              // A (col 1 row 2)
        check(nev == n0 + 1 && ev_t[n0] == 2 && ev_d[n0] == 8'h20, "A pressed: raw code 20");
        n0 = nev; keys[10] = 0; #1500000;
        check(nev == n0 + 1 && ev_d[n0] == 8'hA0, "A released: A0");
        n0 = nev; keys[57] = 1; #1500000; keys[57] = 0; #1500000;       // left arrow
        check(ev_d[n0] == 8'h45 && ev_d[n0 + 1] == 8'hC5, "left arrow is Escape (45 / C5)");
        n0 = nev; keys[7] = 1; #1500000; keys[7] = 0; #1500000;         // CRSR-DN alone
        check(ev_d[n0] == 8'h4D && ev_d[n0 + 1] == 8'hCD, "cursor down: 4D / CD");
        n0 = nev; keys[15] = 1; #1500000;                                 // LSHIFT down
        keys[7] = 1; #1500000; keys[7] = 0; #1500000; keys[15] = 0; #1500000;
        check(ev_d[n0] == 8'h60 && ev_d[n0 + 1] == 8'h4C && ev_d[n0 + 2] == 8'hCC && ev_d[n0 + 3] == 8'hE0, "shift + cursor down is cursor up");
        n0 = nev; restore = 1; #1500000; restore = 0; #1500000;
        check(ev_d[n0] == 8'h67 && ev_d[n0 + 1] == 8'hE7, "RESTORE is the right Amiga key");
        // the reset combination: CTRL (58) + C= (61) + RESTORE
        keys[58] = 1; #1500000; keys[61] = 1; #1500000; restore = 1; #1500000;
        check(rk_seen == 1, "CTRL + C= + RESTORE pulses the Amiga reset once");
        keys[58] = 0; keys[61] = 0; restore = 0; #6000000;
        // mouse
        n0 = nev; mdx = 8'h05; mdy = 8'hFB; mtgl = ~mtgl; #1000000;
        check(nev == n0 + 2 && ev_t[n0] == 0 && ev_d[n0] == 8'h05 && ev_t[n0 + 1] == 1 && ev_d[n0 + 1] == 8'hFB, "mouse pair: x then y");
        mbtn = 3'b011; #1000; check(mb == 3'b011, "mouse buttons pass through");
        // joysticks: host {fire, right, left, down, up}
        joy2 = 5'b01010; joy1 = 5'b10001; #1000;
        check(j1 == 8'b00000101 && j0 == 8'b00011000, "joystick bit order (right, left, down, up, fire)");
        joy2 = 5'b10000; #100; check(j1[4] === 1'b1, "fire");
        // T157 S1/S2: the swap moves the host's port 2 to the Amiga's port 1 (joystick0) and back
        joy1 = 5'd0; joy2 = 5'b10000; jswap = 0; #1000;
        check(j1[4] === 1'b1 && j0 == 8'd0, "swap off: host port 2 fire is Amiga port 2 only");
        jswap = 1; #1000;
        check(j0[4] === 1'b1 && j1 == 8'd0, "swap on: host port 2 fire is Amiga port 1, port 2 empty");
        joy1 = 5'b00001; joy2 = 5'd0; #1000;
        check(j1 == 8'b00001000 && j0 == 8'd0, "swap on: host port 1 up is Amiga port 2");
        jswap = 0; #1000;
        check(j0 == 8'b00001000 && j1 == 8'd0, "swap off again: host port 1 is Amiga port 1");
        joy1 = 0; joy2 = 0;
        // audio: the left value reaches the 24 bit output
        al = 15'sd1000; ar = -15'sd2000; #20000000;           // 20 ms: primed (5.3 ms), faded in (5.3 ms)
        check(oal == 24'sd256000 && oar == -24'sd512000, "audio: 15 bit sums to 24 bit samples");
        $display("PASS tb_input errors=%0d", errors);
        $finish;
    end
    initial begin #400000000; $display("FAIL timeout"); $finish; end
endmodule
