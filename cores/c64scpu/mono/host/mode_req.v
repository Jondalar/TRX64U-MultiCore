// SPDX-License-Identifier: GPL-3.0-or-later
// U16 D4: the HDMI mode request, the picture's scale and their status, in the overlay window of the application (0x10140000, byte
// addresses 0x4Fxx; overlay.v decodes nothing there). Ported from TRX64U-MultiCore's host/mode_req.v (T164 WP7 there); this design is a
// C64-type core (c64_type = 1, a constant here) and adds the scale (U16 D3).
//   write 0x4F00  request = family * 6 + hdmi (scpu_hdmi_modes.vh). A value with bits 7:4 set or above 11 is refused; otherwise its family
//                 part is replaced by fam_v60 (mode_ctl cdce_v60, the System Mode) and the row so named must be locked (hm_locked: the
//                 seven rows of U16 D2), else it is refused. Refused: nothing changes, the CDCE is not touched.
//   write 0x4F01  scale: 0 Integer, 1 Fit, 2 Fill (U16 D3). Any other value is refused and nothing changes (it touches neither the
//                 counters nor the refused bit, which belong to 0x4F00). Taken into clk_pix and handed to the picture (scale_pix) at
//                 the next frame start (fstart): no restart of the synthesizer, the transmitters or mmcm_pix. Power-up 1 (Fit).
//   read  0x4F04  magic 0x48
//         0x4F05  version 2 (T222: this core's copy; U16 and the host's generic mode_req.v answer 1; 2 = the scale register 0x4F01 / 0x4F0A exists)
//         0x4F06  {last request refused, 0, 0, c64_type = 1, row in use is locked, running, pix_locked, gt_ready}
//         0x4F07  the mode hdmi_timing runs (mode_q), 0..11
//         0x4F08  requests written to 0x4F00 (clk_sys count)
//         0x4F09  requests processed (clk50 count, crossed as 8 bits, so one read may be torn; equal to 0x4F08 = the refused bit is current)
//         0x4F0A  the scale in use (scale_pix), 0..2
// A System Mode switch (fam_v60) moves the row to the other family; where the request's row does not exist there (800 x 600, hdmi 3,
// has no 50 Hz row) the family's first row, 576p, is taken, as the application maps it (U16 D5), so the synthesizer and the timing
// never get a row that is not in the table (MultiCore's mode_req.v has only the two 1080p rows locked and needs no such fallback).
// An accepted request (and any change of the effective row, e.g. the family bit of a System Mode switch) holds vm_rst for 8 clk50
// clocks: cdce_mode starts its two writes again, the transmitters follow (top.v gt_rst), the pixel clock moves, mmcm_pix relocks and
// hdmi_timing takes mode_pix while its reset is high. The connections to the overlay window are the write decode here and the read mux
// (rhit / rdata over overlay.v's rdata, one clk_sys after addr like overlay.v).
module mode_req (
    input  wire        clk_sys,
    input  wire        we,
    input  wire [14:0] addr,
    input  wire [7:0]  wdata,
    output reg  [7:0]  rdata = 8'd0,
    output reg         rhit = 1'b0,        // rdata is this module's (addr one clock before was 0x4F04..0x4F0A)
    input  wire        clk50,
    input  wire        fam_v60,            // clk50: the System Mode's family (mode_ctl cdce_v60)
    output reg  [3:0]  vm_mode = 4'd2,     // clk50: the row the synthesizer is written for (to cdce_mode)
    output wire        vm_rst,             // clk50: restart cdce_mode
    input  wire        clk_pix,
    output reg  [3:0]  mode_pix = 4'd2,    // clk_pix: vm_mode, for hdmi_timing (taken while its rst is high)
    input  wire        fstart,             // clk_pix: a frame starts (one clock)
    output reg  [1:0]  scale_pix = 2'd1,   // clk_pix: the scale the picture uses, changes only at fstart
    input  wire        gt_ready,           // any clock
    input  wire        pix_locked,         // any clock
    input  wire        running,            // hdmi_timing.running (clk_pix)
    input  wire [3:0]  mode_q              // hdmi_timing.mode_q (clk_pix)
);
`include "scpu_hdmi_modes.vh"
    // ---- clk_sys: the writes
    reg  [7:0] req_val = 8'd2;
    reg        req_tgl = 1'b0;
    reg  [7:0] wr_cnt = 8'd0;
    reg  [1:0] sc_val = 2'd1;
    reg        sc_tgl = 1'b0;
    always @(posedge clk_sys) begin
        if (we && addr == 15'h4F00) begin req_val <= wdata; req_tgl <= ~req_tgl; wr_cnt <= wr_cnt + 8'd1; end
        if (we && addr == 15'h4F01 && wdata < 8'd3) begin sc_val <= wdata[1:0]; sc_tgl <= ~sc_tgl; end
    end

    // ---- clk50: take, check, hold
    (* ASYNC_REG = "TRUE" *) reg [2:0] tgl_s = 3'b000;
    reg  [3:0] req_m = 4'd2;              // the accepted request
    reg  [3:0] hold = 4'd0;
    reg  [7:0] ack_cnt = 8'd0;
    reg        refused = 1'b0;
    assign vm_rst = hold != 4'd0;
    wire [3:0] fam_off = fam_v60 ? 4'd6 : 4'd0;
    wire [3:0] c_row   = ((req_val[3:0] >= 4'd6) ? req_val[3:0] - 4'd6 : req_val[3:0]) + fam_off;
    wire       in_rng  = req_val[7:4] == 4'd0 && req_val[3:0] < 4'd12;
    wire       ok      = in_rng && hm_locked(c_row);
    wire       take    = tgl_s[2] != tgl_s[1];
    wire [3:0] req_n   = (take && ok) ? req_val[3:0] : req_m;
    wire [3:0] eff_c   = ((req_n >= 4'd6) ? req_n - 4'd6 : req_n) + fam_off;
    wire [3:0] eff_n   = hm_locked(eff_c) ? eff_c : fam_off;
    always @(posedge clk50) begin
        tgl_s <= {tgl_s[1:0], req_tgl};
        if (hold != 4'd0) hold <= hold - 4'd1;
        req_m <= req_n;
        if (take) begin ack_cnt <= ack_cnt + 8'd1; refused <= ~ok; end
        if ((take && ok) || eff_n != vm_mode) begin vm_mode <= eff_n; hold <= 4'd8; end
    end

    // ---- clk_pix: the mode for hdmi_timing, the scale at a frame start
    (* ASYNC_REG = "TRUE" *) reg [3:0] mp1 = 4'd2;
    (* ASYNC_REG = "TRUE" *) reg [2:0] sc_s = 3'b000;
    reg  [1:0] sc_req = 2'd1;             // sc_val, taken a clock after its toggle arrived (it is stable from the toggle on)
    always @(posedge clk_pix) begin
        mp1 <= vm_mode; mode_pix <= mp1;
        sc_s <= {sc_s[1:0], sc_tgl};
        if (sc_s[2] != sc_s[1]) sc_req <= sc_val;
        if (fstart) scale_pix <= sc_req;
    end

    // ---- clk_sys: the status
    (* ASYNC_REG = "TRUE" *) reg [2:0] gt_s = 3'd0, pl_s = 3'd0, ru_s = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [3:0] mq_a = 4'd0, mq_b = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ack_a = 8'd0, ack_b = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg       ref_a = 1'b0, ref_b = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] sq_a = 2'd1, sq_b = 2'd1;
    always @(posedge clk_sys) begin
        gt_s <= {gt_s[1:0], gt_ready};
        pl_s <= {pl_s[1:0], pix_locked};
        ru_s <= {ru_s[1:0], running};
        mq_a <= mode_q; mq_b <= mq_a;
        ack_a <= ack_cnt; ack_b <= ack_a;
        ref_a <= refused; ref_b <= ref_a;
        sq_a <= scale_pix; sq_b <= sq_a;
        rhit <= addr[14:4] == 11'h4F0 && addr[3:0] >= 4'd4 && addr[3:0] <= 4'd10;
        case (addr[3:0])
        4'd4: rdata <= 8'h48;
        4'd5: rdata <= 8'd2;
        4'd6: rdata <= {ref_b, 2'b00, 1'b1, hm_locked(mq_b), ru_s[2], pl_s[2], gt_s[2]};
        4'd7: rdata <= {4'd0, mq_b};
        4'd8: rdata <= wr_cnt;
        4'd10: rdata <= {6'd0, sq_b};
        default: rdata <= ack_b;
        endcase
    end
endmodule
