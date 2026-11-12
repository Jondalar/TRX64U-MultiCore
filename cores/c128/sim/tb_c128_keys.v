// T113 S10: c128_keys against the vectors of gen_keys_vec.py (+VEC=<file>).
`timescale 1ns / 1ps
module tb_c128_keys;
    reg  [63:0] keys = 0;
    reg  [23:0] usb = 0;
    wire [63:0] keys_o;
    wire [23:0] x_o;
    wire sc, s4, sm;
    c128_keys dut (.keys(keys), .x_usb(usb), .keys_o(keys_o), .xkeys_o(x_o), .sp_caps(sc), .sp_4080(s4), .sp_mon(sm));
    reg [1023:0] fn;
    integer fd, n, bad, r;
    reg [63:0] ek;
    reg [23:0] ex;
    reg [3:0]  esp;
    initial begin
        if (!$value$plusargs("VEC=%s", fn)) begin $display("FAIL: +VEC=<file>"); $finish; end
        fd = $fopen(fn, "r");
        n = 0; bad = 0;
        while (!$feof(fd)) begin
            r = $fscanf(fd, "%h %h %h %h %h\n", keys, usb, ek, ex, esp);
            if (r == 5) begin
                #1;
                n = n + 1;
                if (keys_o !== ek || x_o !== ex || {sm, s4, sc} !== esp[2:0]) begin
                    bad = bad + 1;
                    if (bad < 6) $display("FAIL vector %0d: keys %h usb %h -> keys_o %h (want %h) x %h (want %h) sp %b (want %b)", n, keys, usb, keys_o, ek, x_o, ex, {sm, s4, sc}, esp[2:0]);
                end
            end
        end
        $display("%0d vectors, %0d wrong", n, bad);
        if (bad == 0 && n > 100) $display("PASS tb_c128_keys"); else $display("FAIL tb_c128_keys");
        $finish;
    end
endmodule
