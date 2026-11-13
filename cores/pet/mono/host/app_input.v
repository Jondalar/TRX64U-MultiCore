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
    assign keys    = (ovl_own ? 64'd0 : phys_keys) | s2[63:0];
    assign joy1    = phys_joy1 | s2[68:64];
    assign joy2    = phys_joy2 | s2[73:69];
    assign restore = phys_restore | s2[74];
    assign reset   = s2[75];
    assign freeze  = s2[76];
endmodule
