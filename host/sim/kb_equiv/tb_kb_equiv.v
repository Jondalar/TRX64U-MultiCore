// Old and new c64u_keyboard side by side, each on its own PCA9555 + codec model
// driven by the same stimulus. Every output is compared every clock.
`timescale 1ns/1ps

// PCA9555 (0x21) with a real 8x8 key matrix behind it, codec (0x1A) that only acks.
//   mode 0: no diodes (current both ways, ghosting), 1: diodes column->row only,
//   2: diodes row->column only
module pca_model (
    input  wire        clk,
    input  wire        scl_low, sda_low,
    output wire        sda,
    input  wire [63:0] keys,            // [col*8+row]
    input  wire [1:0]  mode,
    input  wire        nack
);
    wire scl = ~scl_low;
    reg slave_sda_low = 0;
    assign sda = ~(sda_low | slave_sda_low);

    reg [7:0] out0 = 8'hFF, out1 = 8'hFF, cfg0 = 8'hFF, cfg1 = 8'hFF, ptr = 0;
    // node levels: low = driven low or connected to a driven-low node through a key
    reg [7:0] clow, rlow;
    integer it, c, r;
    always @* begin
        clow = ~out0 & ~cfg0;
        rlow = ~out1 & ~cfg1;
        for (it = 0; it < 17; it = it + 1)
            for (c = 0; c < 8; c = c + 1)
                for (r = 0; r < 8; r = r + 1)
                    if (keys[c*8+r]) begin
                        if (mode != 2 && clow[c]) rlow[r] = 1'b1;
                        if (mode != 1 && rlow[r]) clow[c] = 1'b1;
                    end
    end
    wire [7:0] in0 = ~clow;
    wire [7:0] in1 = ~rlow;

    reg [7:0] sh; integer nb = 0, bytes = 0; reg rd = 0, addressed = 0, sda_d = 1, scl_d = 1;
    reg [7:0] txb; reg [6:0] who;
    always @(posedge clk) begin
        sda_d <= sda; scl_d <= scl;
        if (scl && sda_d && !sda) begin nb <= 0; bytes <= 0; addressed <= 0; rd <= 0; end
        else if (scl && !sda_d && sda) begin nb <= 0; end
        else if (scl && !scl_d) begin
            if (nb < 8) begin sh <= {sh[6:0], sda}; nb <= nb + 1; end
            else nb <= 0;
        end else if (!scl && scl_d) begin
            if (nb == 8 && !rd) begin
                if (bytes == 0) begin
                    who <= sh[7:1];
                    addressed <= (sh[7:1] == 7'h21) || (sh[7:1] == 7'h1A); rd <= sh[0];
                    slave_sda_low <= !nack && ((sh[7:1] == 7'h21) || (sh[7:1] == 7'h1A));
                    txb <= (ptr[0] ? in1 : in0);
                end else if (addressed) begin
                    slave_sda_low <= !nack;
                    if (who == 7'h21) begin
                        if (bytes == 1) ptr <= sh;
                        else begin
                            case (ptr)
                                8'h02: out0 <= sh; 8'h03: out1 <= sh;
                                8'h06: cfg0 <= sh; 8'h07: cfg1 <= sh;
                            endcase
                            ptr <= ptr ^ 8'h01;
                        end
                    end
                end
                bytes <= bytes + 1;
            end else if (rd && addressed && nb < 8) slave_sda_low <= ~txb[7 - nb];
            else slave_sda_low <= 0;
        end
    end
endmodule

module tb;
    reg clk = 0; always #10 clk = ~clk;
    reg rst = 1, pause = 0, nack = 0;
    reg [63:0] keys_in = 0;
    reg [1:0] mode = 0;

    wire o_scl, o_sda, n_scl, n_sda, o_sdain, n_sdain, o_par, n_par;
    wire [63:0] o_keys, n_keys; wire [15:0] o_st, n_st;
    c64u_keyboard_old old (.clk(clk), .rst(rst), .sda_in(o_sdain), .scl_low(o_scl), .sda_low(o_sda),
                           .keys(o_keys), .status(o_st), .pause(pause), .parked(o_par));
    c64u_keyboard     new (.clk(clk), .rst(rst), .sda_in(n_sdain), .scl_low(n_scl), .sda_low(n_sda),
                           .keys(n_keys), .status(n_st), .pause(pause), .parked(n_par));
    pca_model mo (.clk(clk), .scl_low(o_scl), .sda_low(o_sda), .sda(o_sdain), .keys(keys_in), .mode(mode), .nack(nack));
    pca_model mn (.clk(clk), .scl_low(n_scl), .sda_low(n_sda), .sda(n_sdain), .keys(keys_in), .mode(mode), .nack(nack));

    // compare
    integer diffs = 0, cycles = 0, armed = 0;
    reg seen_keys = 0; reg [63:0] allkeys = 0;
    reg [63:0] kmax = 0;
    always @(negedge clk) begin
        if (!rst) armed <= 1;
        if (armed) begin
            cycles = cycles + 1;
            if ({o_scl, o_sda, o_par, o_st, o_keys} !== {n_scl, n_sda, n_par, n_st, n_keys}) begin
                diffs = diffs + 1;
                if (diffs < 10) $display("DIFF t=%0t old scl%b sda%b par%b st%h keys%h | new scl%b sda%b par%b st%h keys%h",
                    $time, o_scl, o_sda, o_par, o_st, o_keys, n_scl, n_sda, n_par, n_st, n_keys);
            end
            allkeys = allkeys | o_keys;
        end
    end

    // the sequence table against the old function, every pc
    integer pcx, romdiff = 0;
    initial begin
        #1;
        for (pcx = 0; pcx < 128; pcx = pcx + 1)
            if (old.prog(pcx[6:0]) !== new.ROM[pcx*12 +: 12]) romdiff = romdiff + 1;
        $display("ROM entries differing: %0d of 128", romdiff);
    end

    integer seed = 32'h5eed1234;
    integer i, k, hold, a, b, c2, d2;
    task wait_clks(input integer n); begin repeat (n) @(posedge clk); end endtask
    task ms(input integer n); begin wait_clks(n * 50000); end endtask

    // C64 matrix positions (col*8+row as the module reports): left shift (col 1,row 7), right shift (col 6,row 4)
    initial begin
        wait_clks(10); rst = 0;
        ms(30);                                   // settle, codec, first scans, no key
        // every key alone, three diode modes in turn (hold covers one full scan of both directions)
        for (i = 0; i < 64; i = i + 1) begin
            mode = i % 3; keys_in = 64'd1 << i; ms(13);
        end
        keys_in = 0; ms(13);
        // SHIFT LOCK (left shift held) alone and with other keys, all modes
        for (mode = 0; mode < 3; mode = mode + 1) begin
            keys_in = 64'd1 << (1*8+7); ms(13);
            for (i = 0; i < 64; i = i + 9) begin keys_in = (64'd1 << (1*8+7)) | (64'd1 << i); ms(7); end
        end
        // ghosting: three corners of a rectangle, and all four, in every mode
        for (mode = 0; mode < 3; mode = mode + 1)
            for (i = 0; i < 12; i = i + 1) begin
                a = $urandom(seed) % 8; b = $urandom(seed) % 8;
                c2 = (a + 1 + $urandom(seed) % 7) % 8; d2 = (b + 1 + $urandom(seed) % 7) % 8;
                keys_in = (64'd1 << (a*8+b)) | (64'd1 << (a*8+d2)) | (64'd1 << (c2*8+b));
                ms(7);
                keys_in = keys_in | (64'd1 << (c2*8+d2)); ms(7);
            end
        // whole rows / columns / everything
        for (mode = 0; mode < 3; mode = mode + 1) begin
            keys_in = 64'hFFFFFFFFFFFFFFFF; ms(13);
            keys_in = 64'h00000000000000FF << (8*mode); ms(7);
            keys_in = 64'h0101010101010101 << mode; ms(7);
        end
        keys_in = 0;
        // pause: park and release at random phases
        for (i = 0; i < 30; i = i + 1) begin
            keys_in = {$urandom(seed), $urandom(seed)} & {$urandom(seed), $urandom(seed)};
            wait_clks($urandom(seed) % 300000); pause = 1; wait_clks($urandom(seed) % 400000 + 100); pause = 0;
            wait_clks($urandom(seed) % 300000);
        end
        // slave NACKs in bursts
        for (i = 0; i < 30; i = i + 1) begin
            mode = $urandom(seed) % 3;
            keys_in = {$urandom(seed), $urandom(seed)} & {$urandom(seed), $urandom(seed)};
            wait_clks($urandom(seed) % 200000); nack = 1; wait_clks($urandom(seed) % 100000 + 10); nack = 0;
        end
        // resets at random points, short and long, with the key matrix moving
        for (i = 0; i < 40; i = i + 1) begin
            keys_in = {$urandom(seed), $urandom(seed)} & {$urandom(seed), $urandom(seed)};
            wait_clks($urandom(seed) % 600000 + 1); rst = 1; wait_clks(1 + $urandom(seed) % 5000); rst = 0;
            wait_clks($urandom(seed) % 100000);
        end
        // everything random at once
        for (i = 0; i < 400; i = i + 1) begin
            mode = $urandom(seed) % 3;
            keys_in = keys_in ^ ({$urandom(seed), $urandom(seed)} & {$urandom(seed), $urandom(seed)} & {$urandom(seed), $urandom(seed)});
            if ($urandom(seed) % 8 == 0) nack = ~nack;
            if ($urandom(seed) % 16 == 0) pause = ~pause;
            if ($urandom(seed) % 100 == 0) begin rst = 1; wait_clks(1 + $urandom(seed) % 40); rst = 0; end
            wait_clks($urandom(seed) % 200000 + 1000);
        end
        pause = 0; nack = 0; rst = 0; ms(15);
        $display("cycles compared: %0d, differences: %0d, ROM diffs: %0d, keys ever seen: %h", cycles, diffs, romdiff, allkeys);
        if (diffs == 0 && romdiff == 0 && allkeys == 64'hFFFFFFFFFFFFFFFF) $display("PASS"); else $display("FAIL");
        $finish;
    end
endmodule
