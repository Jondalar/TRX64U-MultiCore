// TI-99/4A core: the register block the app sees at its core window (cw_*, cpu_clk). Same protocol and same common
// registers as vc20_regs.vhd (ack one clock after re/we, read data registered, pixel layer page 6 forwarded). Written for this project.
//   000-003 ID "TI99"   004 CAPS 0xC0   005 CAPS2 0x02   008-00B BUILD   010 IRQ_EN   014 IRQ_CAUSE (W1C)   020-027 DDR_CAL
//   0A0 CTRL   b0 reset hold, b1 halt
//   0A1 CFG    (reset value 0x04) b0 turbo CPU, b1 swap joysticks, b2 32K RAM card, b3 disk controller, b4 1K scratchpad mirror (as Mega99)
//   0A2 WBADD  added to bits 15:8 of the wishbone address (ti99_win)
//   0A3 WBHI   upper byte of the 24-bit wishbone address behind the 64 KB ROM aperture (ti99_win)
//   0A4 STATUS b0 machine in reset, b1 held
//   0B0-0B9 diagnostics, read only, 16 bit little endian: PC, ST, WP, GROM address, VDP address
//   0A8-0A9 FRAMES   0C0 FB_LATEST   0C1 FRAME_SEQ   0C2-0C3 FB_OVERRUNS   0C4-0C7 FB_BASE (0x01D00000)   0C8-0CB FB_STRIDE (0x00100000)
`timescale 1ns / 1ps
module ti99_regs (
    input  wire        clk,
    input  wire        we, re,
    input  wire [16:0] addr,
    input  wire [7:0]  wdata,
    output wire [7:0]  rdata,
    output wire        ack,
    output wire        irq,
    input  wire [31:0] build,
    input  wire [63:0] ddr_cal,
    output wire        px_we, px_re,
    input  wire [7:0]  px_rdata,
    input  wire        ev_vsync, ev_flip, ev_frame,
    output wire [7:0]  ctrl,
    output wire [7:0]  cfg,
    output wire [7:0]  wbhi,
    output wire [7:0]  wbadd,
    output wire [31:0] fb_base, fb_stride,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [7:0]  fb_latest, fb_seq,
    input  wire [15:0] fb_over,
    input  wire [15:0] dbg_pc, dbg_st, dbg_wp, dbg_ga, dbg_va    // CPU / GROM / VDP address probes (diagnostics, read only)
);
    reg [7:0]  ctrl_r = 8'h00, cfg_r = 8'h04, wbhi_r = 8'h00, wbadd_r = 8'h00, irq_en = 8'h00, cause = 8'h00, q = 8'h00;
    reg [31:0] base_r = 32'h01D00000, stride_r = 32'h00100000;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    reg [15:0] f_a = 0, f_b = 0, f_q = 0, o_a = 0, o_b = 0, o_q = 0;
    reg [15:0] pc_a = 0, pc_b = 0, pc_q = 0, st_a = 0, st_b = 0, st_q = 0, wp_a = 0, wp_b = 0, wp_q = 0, ga_a = 0, ga_b = 0, ga_q = 0, va_a = 0, va_b = 0, va_q = 0;
    reg [7:0]  s_a = 0, s_b = 0, s_q = 0, l_a = 0, l_b = 0, l_q = 0, n_a = 0, n_b = 0, n_q = 0;
    wire win = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, 5'b00000};
    wire p0 = !win && page == 4'h0;

    assign px_we = (page == 4'h6 && !win) ? we : 1'b0;
    assign px_re = (page == 4'h6 && !win) ? re : 1'b0;
    assign ctrl = ctrl_r; assign cfg = cfg_r; assign wbhi = wbhi_r; assign wbadd = wbadd_r;
    assign fb_base = base_r; assign fb_stride = stride_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    always @(posedge clk) begin
        f_a <= frames;   f_b <= f_a;   if (f_a == f_b) f_q <= f_b;
        pc_a <= dbg_pc; pc_b <= pc_a; pc_q <= pc_b;
        st_a <= dbg_st; st_b <= st_a; st_q <= st_b;
        wp_a <= dbg_wp; wp_b <= wp_a; wp_q <= wp_b;
        ga_a <= dbg_ga; ga_b <= ga_a; ga_q <= ga_b;
        va_a <= dbg_va; va_b <= va_a; va_q <= va_b;
        s_a <= status;   s_b <= s_a;   if (s_a == s_b) s_q <= s_b;
        l_a <= fb_latest; l_b <= l_a;  if (l_a == l_b) l_q <= l_b;
        n_a <= fb_seq;   n_b <= n_a;   if (n_a == n_b) n_q <= n_b;
        o_a <= fb_over;  o_b <= o_a;   if (o_a == o_b) o_q <= o_b;
        cause <= cause | ev;
        ack_r <= re | we;
        q_p6  <= re && !win && page == 4'h6;
        if (we && p0) begin
            case (off)
                12'h010: irq_en <= wdata;
                12'h014: cause <= (cause & ~wdata) | ev;
                12'h0A0: ctrl_r <= wdata;
                12'h0A1: cfg_r  <= wdata;
                12'h0A2: wbadd_r <= wdata;
                12'h0A3: wbhi_r <= wdata;
                12'h0C4: base_r[7:0]    <= wdata;
                12'h0C5: base_r[15:8]   <= wdata;
                12'h0C6: base_r[23:16]  <= wdata;
                12'h0C7: base_r[31:24]  <= wdata;
                12'h0C8: stride_r[7:0]  <= wdata;
                12'h0C9: stride_r[15:8] <= wdata;
                12'h0CA: stride_r[23:16]<= wdata;
                12'h0CB: stride_r[31:24]<= wdata;
                default: ;
            endcase
        end
        q <= 8'h00;
        if (p0) begin
            case (off)
                12'h000: q <= "T";   12'h001: q <= "I";  12'h002: q <= "9";  12'h003: q <= "9";
                12'h004: q <= 8'hC0; 12'h005: q <= 8'h02;
                12'h008: q <= build[7:0];   12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16]; 12'h00B: q <= build[31:24];
                12'h010: q <= irq_en; 12'h014: q <= cause;
                12'h020: q <= ddr_cal[7:0];   12'h021: q <= ddr_cal[15:8];  12'h022: q <= ddr_cal[23:16]; 12'h023: q <= ddr_cal[31:24];
                12'h024: q <= ddr_cal[39:32]; 12'h025: q <= ddr_cal[47:40]; 12'h026: q <= ddr_cal[55:48]; 12'h027: q <= ddr_cal[63:56];
                12'h0A0: q <= ctrl_r; 12'h0A1: q <= cfg_r; 12'h0A2: q <= wbadd_r; 12'h0A3: q <= wbhi_r; 12'h0A4: q <= s_q;
                12'h0A8: q <= f_q[7:0]; 12'h0A9: q <= f_q[15:8];
                12'h0B0: q <= pc_q[7:0]; 12'h0B1: q <= pc_q[15:8]; 12'h0B2: q <= st_q[7:0]; 12'h0B3: q <= st_q[15:8];
                12'h0B4: q <= wp_q[7:0]; 12'h0B5: q <= wp_q[15:8]; 12'h0B6: q <= ga_q[7:0]; 12'h0B7: q <= ga_q[15:8];
                12'h0B8: q <= va_q[7:0]; 12'h0B9: q <= va_q[15:8];
                12'h0C0: q <= l_q; 12'h0C1: q <= n_q; 12'h0C2: q <= o_q[7:0]; 12'h0C3: q <= o_q[15:8];
                12'h0C4: q <= base_r[7:0];  12'h0C5: q <= base_r[15:8];  12'h0C6: q <= base_r[23:16]; 12'h0C7: q <= base_r[31:24];
                12'h0C8: q <= stride_r[7:0]; 12'h0C9: q <= stride_r[15:8]; 12'h0CA: q <= stride_r[23:16]; 12'h0CB: q <= stride_r[31:24];
                default: ;
            endcase
        end
    end
endmodule
