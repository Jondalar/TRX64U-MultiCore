// The expansion port's PHI2, DOTCLK and RESET under the host's port lock (T135 D6a, T164 WP10).
//
// While `lock` holds (port_lock in top.v) the port's RESET is held low and PHI2 / DOTCLK run at the C64 rate, as on a C64 with the reset
// held: dot4x / 32 and dot4x / 4 from a counter on clk, the system clock of the machine (dot4x of the System Mode in use: PAL 31.53 MHz ->
// 0.985 MHz / 7.88 MHz, NTSC 32.73 MHz -> 1.023 MHz / 8.18 MHz; the counter follows the mode because clk does). The phases are those of the
// core's own port (slot_port: PHI2 high in tk 1..16, DOTCLK = tk[1]).
// `decouple` (a swap) forces both low, as before.
//
// Hand-over (lock released or set again): per line, wait until the line in use is low, hold the output low for one full low time
// (16 clk for PHI2, 2 for DOTCLK), then switch the source while the new source is low. The output never leaves its level except for a
// whole edge of one of the two sources, and every high and low time after the switch is at least a full one: no runt, no glitch.
module portlock_clk (
    input  wire clk,
    input  wire lock,           // port_lock: the generator is the source
    input  wire decouple,       // core_decouple: both lines low
    input  wire l2_phi2,        // the core's own port lines
    input  wire l2_dotclk,
    input  wire l2_rst_o,       // slot_rst_o of the core; 1 = the port's RESET pulled low
    output wire phi2,
    output wire dotclk,
    output wire rst_o,
    output wire gen_phi2,       // the generator alone (bench)
    output wire gen_dotclk
);
    reg [4:0] cnt = 5'd0;
    always @(posedge clk) cnt <= cnt + 5'd1;
    assign gen_phi2   = (cnt >= 5'd1) && (cnt <= 5'd16);
    assign gen_dotclk = cnt[1];

    wire want_core = !lock;
    reg  sel_p = 1'b0, sel_d = 1'b0;        // 0 generator, 1 core
    reg  hold_p = 1'b0, hold_d = 1'b0;      // output forced low
    reg  [4:0] cnt_p = 5'd0;
    reg  [1:0] cnt_d = 2'd0;
    wire src_p = sel_p ? l2_phi2 : gen_phi2;
    wire src_d = sel_d ? l2_dotclk : gen_dotclk;
    wire new_p = sel_p ? gen_phi2 : l2_phi2;
    wire new_d = sel_d ? gen_dotclk : l2_dotclk;

    always @(posedge clk) begin
        if (!hold_p) begin
            if (sel_p != want_core && !src_p) begin hold_p <= 1'b1; cnt_p <= 5'd15; end
        end else if (cnt_p != 5'd0) cnt_p <= cnt_p - 5'd1;
        else if (!new_p) begin sel_p <= want_core; hold_p <= 1'b0; end
        if (!hold_d) begin
            if (sel_d != want_core && !src_d) begin hold_d <= 1'b1; cnt_d <= 2'd1; end
        end else if (cnt_d != 2'd0) cnt_d <= cnt_d - 2'd1;
        else if (!new_d) begin sel_d <= want_core; hold_d <= 1'b0; end
    end

    assign phi2   = (decouple || hold_p) ? 1'b0 : src_p;
    assign dotclk = (decouple || hold_d) ? 1'b0 : src_d;
    assign rst_o  = lock ? 1'b1 : decouple ? 1'b0 : l2_rst_o;
endmodule
