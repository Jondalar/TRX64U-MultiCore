// T119 5.15: bench of ulaw (RTL or its funcsim netlist): 20 lines of each model, counts the HBlank / HSync rises and the dots between them.
// Enables as zx_core makes them: ce_n every 4.5 clocks on average (4, 5, 4, 5 ...), ce_p two clocks after it.
`timescale 1ns / 1ps
module tb_ulaw;
    reg clk = 0, rst = 1, ce_n = 0, ce_p = 0, m128 = 0;
    wire hb, hs, vb, vs, nint; wire [3:0] igrb;
    ulaw u (.clk(clk), .rst(rst), .ce_n(ce_n), .ce_p(ce_p), .m128(m128), .hblank(hb), .hsync(hs), .vblank(vb), .vsync(vs), .nint(nint), .igrb(igrb));
    always #5 clk = ~clk;
    reg [1:0] cnt = 0; reg [3:0] pipe = 0;
    integer dots, hb_rise, hs_rise, hb_hi, hs_hi, vs_rise, k, m, last_hb, last_hs, per_hb, per_hs;
    reg hb_d = 0, hs_d = 0, vs_d = 0;
    // 4, 5, 4, 5 ...: a pulse when the phase counter wraps
    reg [3:0] ph = 0; reg odd = 0;
    always @(posedge clk) begin
        ce_n <= 0; ce_p <= 0;
        pipe <= {pipe[2:0], 1'b0};
        ph <= ph + 1;
        if (ph == (odd ? 4 : 3)) begin ph <= 0; odd <= ~odd; ce_n <= 1; pipe[0] <= 1; end
        if (pipe[1]) ce_p <= 1;
        if (ce_p) begin
            dots = dots + 1;
            if (hb && !hb_d) begin hb_rise = hb_rise + 1; per_hb = dots - last_hb; last_hb = dots; end
            if (hs && !hs_d) begin hs_rise = hs_rise + 1; per_hs = dots - last_hs; last_hs = dots; end
            if (vs && !vs_d) vs_rise = vs_rise + 1;
            if (hb) hb_hi = hb_hi + 1;
            if (hs) hs_hi = hs_hi + 1;
            hb_d <= hb; hs_d <= hs; vs_d <= vs;
        end
    end
    initial begin
        for (m = 0; m < 2; m = m + 1) begin
            m128 = m[0]; rst = 1;
            repeat (20) @(posedge clk);
            rst = 0;
            dots = 0; hb_rise = 0; hs_rise = 0; hb_hi = 0; hs_hi = 0; vs_rise = 0; last_hb = 0; last_hs = 0; per_hb = 0; per_hs = 0;
            repeat (41040) @(posedge clk);                    // 20 lines of 456 dots
            $display("%0s: %0d dots; HBlank rises %0d (period %0d, %0d dots high); HSync rises %0d (period %0d, %0d dots high); VSync rises %0d",
                     m ? "128K" : "48K ", dots, hb_rise, per_hb, hb_hi, hs_rise, per_hs, hs_hi, vs_rise);
            if (hb_rise >= 18 && per_hb == (m ? 456 : 448) && hs_rise >= 18 && per_hs == (m ? 456 : 448) && hb_hi >= 18 * (m ? 112 : 104) && hs_hi >= 18 * 32)
                $display("PASS %0s: HBlank and HSync run", m ? "128K" : "48K ");
            else $display("FAIL %0s: HBlank and / or HSync dead", m ? "128K" : "48K ");
        end
        $finish;
    end
endmodule
