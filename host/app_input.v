// T38: the app's keyboard matrix, joysticks, RESTORE, reset and freeze
// (ultimate_sys.vhd, its clock) into the C64 clock, merged with the C64U's own
// keyboard and joysticks. Everything active high = pressed, so the OR is the
// wired AND of the active-low lines: either side can hold a key down.
// The physical inputs pass through unchanged on their own wires in top.v
// (the app's keyboard scan, T44's unlock); only the C64 gets the merge.
module app_input (
    input  wire        clk,                 // C64 clock
    input  wire        ovl_own,             // the menu has the physical keyboard (synchronized)
    input  wire [63:0] phys_keys,           // col*8 + row (synchronized)
    input  wire  [4:0] phys_joy1, phys_joy2,
    input  wire        phys_restore,
    input  wire [63:0] app_keys,            // the app's clock: two flip-flops here
    input  wire  [4:0] app_joy1, app_joy2,
    input  wire        app_restore, app_reset, app_freeze,
    // T181 D2 (app clock, quasi static): modifier A/B = {used, 7-bit key: 0-63 matrix, 64 RESTORE}
    // and the action-key mask {RESTORE, 64 keys}; modifier A unused = the feature is off
    input  wire  [7:0] hk_mod_a, hk_mod_b,
    input  wire [64:0] hk_mask,
    output wire [63:0] keys,
    output wire  [4:0] joy1, joy2,
    output wire        restore,
    output wire        reset,
    output wire        freeze               // held while the app holds it
);
    // The app writes a byte at a time and the bits change at most every few
    // ms; bits of one write arriving a clock apart do no harm.
    (* ASYNC_REG = "TRUE" *) reg [76:0] s1 = 77'd0, s2 = 77'd0;
    always @(posedge clk) begin
        s1 <= {app_freeze, app_reset, app_restore, app_joy2, app_joy1, app_keys};
        s2 <= s1;
    end
    // T181 D2: the mask hides physical action keys (and a physical RESTORE) from the core while
    // every used modifier key is physically down; the modifier keys themselves are not
    // masked by this logic (the app writes action keys only). A hidden key stays hidden until
    // it is physically released, so releasing the modifier first does not show the core a
    // press. The app's own keys (s2) and the app's reads of the physical keys are unchanged.
    (* ASYNC_REG = "TRUE" *) reg [80:0] h1 = 81'd0, h2 = 81'd0;
    always @(posedge clk) begin
        h1 <= {hk_mask, hk_mod_b, hk_mod_a};
        h2 <= h1;
    end
    wire [7:0]  ma = h2[7:0], mb = h2[15:8];
    wire [64:0] mask = h2[80:16];
    wire [64:0] phys_all = {phys_restore, phys_keys};
    wire [127:0] phys_x = {63'd0, phys_all};         // an index above 64 reads as up
    wire        a_down = ma[7] & phys_x[ma[6:0]];
    wire        b_down = ~mb[7] | phys_x[mb[6:0]];
    wire        mod_down = a_down & b_down;
    reg  [64:0] hid = 65'd0;
    wire [64:0] hid_now = mask & phys_all & ({65{mod_down}} | hid);
    always @(posedge clk) hid <= hid_now;
    wire [64:0] vis = phys_all & ~hid_now;
    assign keys    = (ovl_own ? 64'd0 : vis[63:0]) | s2[63:0];
    assign joy1    = phys_joy1 | s2[68:64];
    assign joy2    = phys_joy2 | s2[73:69];
    assign restore = vis[64] | s2[74];
    assign reset   = s2[75];
    assign freeze  = s2[76];
endmodule
