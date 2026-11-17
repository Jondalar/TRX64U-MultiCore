// T164 WP7 (G2, all cores): the HDMI mode request and its status, in the overlay window of the application (0x10144xxx, byte addresses 0x4Fxx).
//   write 0x4F00  request = family * 6 + hdmi (hdmi_modes.vh). A value with bits 7:4 set, above 11, or not a row of the table (hm_valid 0:
//                 family 0 with hdmi 3..5) is refused: nothing changes, the CDCE is not touched. A C64-type core (c64_type = 1) is held to
//                 its family: the family part of the request is replaced by fam_v60 (mode_ctl cdce_v60, the System Mode) and only a locked
//                 row (hm_locked, the 1080p rows) is accepted. A frame-buffer core (c64_type = 0) gets the request as it is.
//   read  0x4F04  magic 0x48
//         0x4F05  version 1 (4 in the Amiga copies: T234 adds 0x4F02 / 0x4F0C, below)
//         0x4F06  {last request refused, 0, 0, c64_type, row in use is locked, running, pix_locked, gt_ready}
//         0x4F07  the mode hdmi_timing runs (mode_q), 0..11
//         0x4F08  requests written (clk_sys count)
//         0x4F09  requests processed (clk50 count, crossed as 8 bits, so one read may be torn; equal to 0x4F08 = the refused bit is current)
// T234 (the Amiga copies only): write 0x4F02 bit 0 = Frame follow (0 Off, the power-up value; 1 On); read 0x4F0C = the value taken. Off: a request for
//   row 0, 1 or 2 (the 50 Hz television rows) runs the host's standard rows 12, 13, 14 of hdmi_modes.vh and hdmi_timing does not follow the core on them;
//   On: rows 0..2 are T188's. 0x4F07 reports the requested row (0..2) either way. A change of the value is a change of the effective row: the CDCE
//   is written again and the timing generator restarts like after a request. The version byte (0x4F05) is 4.
// An accepted request (and any change of the effective row, e.g. the family bit) holds vm_rst for 8 clk50 clocks: cdce_mode starts its two
// writes again, the transmitters follow (top.v), the pixel clock moves, mmcm_pix relocks and hdmi_timing takes mode_pix while its reset is high.
// The connections to the overlay window are the write decode here and the read mux (rhit / rdata over overlay.v's rdata, one clk_sys after
// addr like overlay.v); overlay.v itself is not touched.
module mode_req (
    input  wire        clk_sys,
    input  wire        we,
    input  wire [14:0] addr,
    input  wire [7:0]  wdata,
    output reg  [7:0]  rdata = 8'd0,
    output reg         rhit = 1'b0,        // rdata is this module's (addr one clock before was 0x4F04..0x4F09)
    input  wire        clk50,
    input  wire        c64_type,           // static: the core is held to the VIC frame lock
    input  wire        fam_v60,            // clk50: the System Mode's family (mode_ctl cdce_v60)
    output reg  [3:0]  vm_mode = 4'd14,    // clk50: the row the synthesizer is written for (to cdce_mode)
    output wire        vm_rst,             // clk50: restart cdce_mode
    input  wire        clk_pix,
    output reg  [3:0]  mode_pix = 4'd14,   // clk_pix: vm_mode, for hdmi_timing (taken while its rst is high)
    input  wire        gt_ready,           // any clock
    input  wire        pix_locked,         // any clock
    input  wire        running,            // hdmi_timing.running (clk_pix)
    input  wire [3:0]  mode_q              // hdmi_timing.mode_q (clk_pix)
);
`include "hdmi_modes.vh"
    // ---- clk_sys: the write
    reg  [7:0] req_val = 8'd2;
    reg        req_tgl = 1'b0;
    reg  [7:0] wr_cnt = 8'd0;
    reg        fol = 1'b0;                // T234: Frame follow, Off at power-up
    always @(posedge clk_sys) begin
        if (we && addr == 15'h4F00) begin req_val <= wdata; req_tgl <= ~req_tgl; wr_cnt <= wr_cnt + 8'd1; end
        if (we && addr == 15'h4F02) fol <= wdata[0];
    end

    // ---- clk50: take, check, hold
    (* ASYNC_REG = "TRUE" *) reg [2:0] tgl_s = 3'b000;
    reg  [3:0] req_m = 4'd2;              // the accepted request (the requested row, not the effective one)
    reg  [3:0] hold = 4'd0;
    reg  [7:0] ack_cnt = 8'd0;
    reg        refused = 1'b0;
    assign vm_rst = hold != 4'd0;
    wire [3:0] fam_off = fam_v60 ? 4'd6 : 4'd0;
    wire [3:0] c_row   = ((req_val[3:0] >= 4'd6) ? req_val[3:0] - 4'd6 : req_val[3:0]) + fam_off;
    wire       in_rng  = req_val[7:4] == 4'd0 && req_val[3:0] < 4'd12;
    wire       ok      = in_rng && (c64_type ? hm_locked(c_row) : hm_valid(req_val[3:0]));
    wire       take    = tgl_s[2] != tgl_s[1];
    wire [3:0] req_n   = (take && ok) ? req_val[3:0] : req_m;
    wire [3:0] eff_r   = c64_type ? (((req_n >= 4'd6) ? req_n - 4'd6 : req_n) + fam_off) : req_n;
    (* ASYNC_REG = "TRUE" *) reg [1:0] fol_s = 2'b00;
    wire [3:0] eff_n   = (!fol_s[1] && eff_r < 4'd3) ? eff_r + 4'd12 : eff_r;     // T234: Off = the host's standard 50 Hz rows 12..14
    always @(posedge clk50) begin
        tgl_s <= {tgl_s[1:0], req_tgl};
        fol_s <= {fol_s[0], fol};
        if (hold != 4'd0) hold <= hold - 4'd1;
        req_m <= req_n;
        if (take) begin ack_cnt <= ack_cnt + 8'd1; refused <= ~ok; end
        if ((take && ok) || eff_n != vm_mode) begin vm_mode <= eff_n; hold <= 4'd8; end
    end

    // ---- clk_pix: the mode for hdmi_timing
    (* ASYNC_REG = "TRUE" *) reg [3:0] mp1 = 4'd14;
    always @(posedge clk_pix) begin mp1 <= vm_mode; mode_pix <= mp1; end

    // ---- clk_sys: the status
    (* ASYNC_REG = "TRUE" *) reg [2:0] gt_s = 3'd0, pl_s = 3'd0, ru_s = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [3:0] mq_a = 4'd0, mq_b = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg [7:0] ack_a = 8'd0, ack_b = 8'd0;
    (* ASYNC_REG = "TRUE" *) reg       ref_a = 1'b0, ref_b = 1'b0;
    wire [3:0] mq_row = (mq_b >= 4'd12) ? mq_b - 4'd12 : mq_b;             // T234: the requested row, whichever table row runs it
    always @(posedge clk_sys) begin
        gt_s <= {gt_s[1:0], gt_ready};
        pl_s <= {pl_s[1:0], pix_locked};
        ru_s <= {ru_s[1:0], running};
        mq_a <= mode_q; mq_b <= mq_a;
        ack_a <= ack_cnt; ack_b <= ack_a;
        ref_a <= refused; ref_b <= ref_a;
        rhit <= addr[14:4] == 11'h4F0 && ((addr[3:0] >= 4'd4 && addr[3:0] <= 4'd9) || addr[3:0] == 4'd12);
        case (addr[3:0])
        4'd4: rdata <= 8'h48;
        4'd5: rdata <= 8'd4;
        4'd6: rdata <= {ref_b, 2'b00, c64_type, hm_locked(mq_b), ru_s[2], pl_s[2], gt_s[2]};
        4'd7: rdata <= {4'd0, mq_row};
        4'd8: rdata <= wr_cnt;
        4'd12: rdata <= {7'd0, fol};
        default: rdata <= ack_b;
        endcase
    end
endmodule
