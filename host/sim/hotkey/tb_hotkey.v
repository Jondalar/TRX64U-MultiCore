// T181 D2: the hotkey mask of host/app_input.v. Keys: MA 58 and MB 60 modifiers, ACT1 10 and ACT2 11
// action keys (masked), FREE 12 (not masked), RESTORE = 64. Prints PASS only when every check holds.
`timescale 1ns/1ps
module tb_hotkey;
    reg clk = 0;
    always #15.86 clk = ~clk;
    reg ovl = 0;
    reg [63:0] pk = 0; reg prst = 0;
    reg [63:0] ak = 0;
    reg [7:0] ma = 0, mb = 0; reg [64:0] mask = 0;
    wire [63:0] keys; wire restore;
    app_input dut (.clk(clk), .ovl_own(ovl), .phys_keys(pk), .phys_joy1(5'd0), .phys_joy2(5'd0), .phys_restore(prst),
                   .app_keys(ak), .app_joy1(5'd0), .app_joy2(5'd0), .app_restore(1'b0), .app_reset(1'b0), .app_freeze(1'b0),
                   .hk_mod_a(ma), .hk_mod_b(mb), .hk_mask(mask),
                   .keys(keys), .joy1(), .joy2(), .restore(restore), .reset(), .freeze());
    integer err = 0, n = 0, i;
    localparam MA = 58, MB = 60, ACT1 = 10, ACT2 = 11, FREE = 12;
    task settle; begin repeat (6) @(posedge clk); #1; end endtask
    task chk(input [255:0] what, input [63:0] want_keys, input want_rst); begin
        settle; n = n + 1;
        if (keys !== want_keys || restore !== want_rst) begin
            err = err + 1; $display("FAIL %0s: keys %h want %h, restore %b want %b", what, keys, want_keys, restore, want_rst);
        end
    end endtask
    function [63:0] b(input integer k); b = 64'd1 << k; endfunction
    reg [63:0] rnd;
    initial begin
        #200;
        // S1 one-key modifier A, masked ACT1 + ACT2 + RESTORE
        ma = 8'h80 | MA; mb = 0; mask = b(ACT1) | b(ACT2) | (65'd1 << 64);
        settle;
        pk = b(ACT1);                chk("S1 action alone passes", b(ACT1), 0);
        pk = 0;                      chk("S1 idle", 0, 0);
        pk = b(MA);                  chk("S1 modifier passes", b(MA), 0);
        pk = b(MA) | b(ACT1);        chk("S1 action held back with modifier", b(MA), 0);
        pk = b(MA) | b(ACT1) | b(ACT2); chk("S1 both actions held back", b(MA), 0);
        pk = b(MA) | b(ACT1) | b(FREE); chk("S1 unmasked key passes with modifier", b(MA) | b(FREE), 0);
        prst = 1;                    chk("S1 RESTORE masked with modifier", b(MA) | b(FREE), 0);
        pk = 0; prst = 0;            chk("S1 all up", 0, 0);
        prst = 1;                    chk("S1 RESTORE alone passes", 0, 1);
        prst = 0;
        // S2 two-key modifier
        mb = 8'h80 | MB; settle;
        pk = b(MA) | b(ACT1);        chk("S2 only A down: action passes", b(MA) | b(ACT1), 0);
        pk = b(MB) | b(ACT1);        chk("S2 only B down: action passes", b(MB) | b(ACT1), 0);
        pk = b(MA) | b(MB);          chk("S2 A+B pass", b(MA) | b(MB), 0);
        pk = b(MA) | b(MB) | b(ACT1); chk("S2 A+B+action: action held back", b(MA) | b(MB), 0);
        pk = b(MA) | b(MB) | b(ACT1) | b(ACT2); chk("S2 two actions held back", b(MA) | b(MB), 0);
        pk = 0; chk("S2 all up", 0, 0);
        // S3 release order: modifier first, action later
        pk = b(MA) | b(MB) | b(ACT1); chk("S3 held back", b(MA) | b(MB), 0);
        pk = b(MA) | b(ACT1);        chk("S3 modifier B up, action still hidden", b(MA), 0);
        pk = b(ACT1);                chk("S3 modifier A up, action still hidden", 0, 0);
        pk = 0;                      chk("S3 action up", 0, 0);
        pk = b(ACT1);                chk("S3 press again without modifier passes", b(ACT1), 0);
        pk = 0; chk("S3 up", 0, 0);
        // S3b action released first
        pk = b(MA) | b(MB) | b(ACT1); chk("S3b hidden", b(MA) | b(MB), 0);
        pk = b(MA) | b(MB);          chk("S3b action up first", b(MA) | b(MB), 0);
        pk = 0; chk("S3b all up", 0, 0);
        // S3c action pressed first, modifier second: the core sees the action drop when the modifier completes
        pk = b(ACT1);                chk("S3c action first passes", b(ACT1), 0);
        pk = b(ACT1) | b(MA) | b(MB); chk("S3c modifier completes: action hidden", b(MA) | b(MB), 0);
        pk = 0; chk("S3c up", 0, 0);
        // S4 RESTORE as modifier A
        ma = 8'h80 | 64; mb = 0; settle;
        pk = b(ACT1);                chk("S4 RESTORE-modifier not down: action passes", b(ACT1), 0);
        pk = b(ACT1); prst = 1;      chk("S4 RESTORE modifier down: action hidden (restore masked too)", 0, 0);
        pk = 0; prst = 0; chk("S4 up", 0, 0);
        // S5 app keys are not touched
        ma = 8'h80 | MA; mb = 0; settle;
        pk = b(MA); ak = b(ACT1);    chk("S5 the app's own key passes", b(MA) | b(ACT1), 0);
        pk = 0; ak = 0; chk("S5 up", 0, 0);
        // S6 menu owns the keyboard: nothing physical reaches the core either way
        ovl = 1; pk = b(MA) | b(ACT1) | b(FREE); chk("S6 ovl_own", 0, 0);
        ovl = 0; pk = 0; chk("S6 up", 0, 0);
        // S7 feature off (modifier A unused, the rest full of garbage) = the old wiring, random walk
        ma = 8'h00; mb = 8'hFF; mask = {65{1'b1}}; settle;
        for (i = 0; i < 300; i = i + 1) begin
            rnd = {$random, $random}; pk = rnd; prst = rnd[7]; ak = {$random, $random} & {$random, $random};
            settle; n = n + 1;
            if (keys !== (pk | ak) || restore !== prst) begin err = err + 1; $display("FAIL S7 off: keys %h want %h", keys, pk | ak); end
        end
        // S8 modifier index above 64 never counts as down
        pk = 0; ak = 0; prst = 0; ma = 8'h80 | 100; mb = 0; mask = b(ACT1); settle;
        pk = b(ACT1); chk("S8 index out of range: feature never fires", b(ACT1), 0);
        pk = 0;
        if (err == 0) $display("PASS hotkey %0d checks", n); else $display("FAIL hotkey %0d errors", err);
        $finish;
    end
endmodule
