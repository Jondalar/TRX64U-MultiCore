// T260 P3: the capture of the diag core. CAPTURE (probe.v op 10) starts it in the background: every K clocks of cpu_clk it takes one 32 bit sample, bit j = the pin CAP_SEL[j] of the
// 128 bit snapshot (live), four samples fill one 16 byte line, and the lines go into a ring in the DDR2 through the engine client (e_*) of mig_arb in ui_clk. Written for this project (TREX).
// The ring is DDR2 bytes BASE_W*2 .. +8 MiB; nothing else in the diag build writes it (spec T260 section 5, "DDR2 ring"). A line goes through NBUF buffers: cpu_clk fills one, a toggle per
// buffer (pub / ack, 2 flops each way) hands it to ui_clk, which writes it and toggles back. Sample i of a capture sits at ring byte 16*first_line + 4*i (little endian); a capture ends on
// a line boundary (a partial line is stored with zeros), the next one starts on the next line. When all NBUF buffers are busy the line is dropped (lost += its samples, overrun, the ring has
// a hole at that line). The reader (rd_*) fetches one 16 byte line of the ring through the same client; the application cannot address that range (its bus ends at 64 MB).
// A sample taken at tick clock T holds the pins as the synchronised snapshot showed them in clock T (the lane muxes are two stages deep and the line register one more: the line holds
// it three clocks later).
`timescale 1ns / 1ps
module diag_cap #(
    parameter LANES = 32,
    parameter [26:0] BASE_W = 27'h3000000       // ring start, a 16 bit word address (byte 0x06000000)
) (
    input  wire         clk,                    // cpu_clk
    input  wire         rst,
    input  wire [127:0] live,
    input  wire [223:0] sel,                    // lane j: sel[7j +: 7], 0x7F = unused
    input  wire         go,
    input  wire [23:0]  k,
    input  wire [31:0]  m,
    input  wire         stop,                   // abort, decouple, reset: no new sample
    input  wire         ring_rst,               // CAP RESET
    output reg          refused,
    output wire         sampling,
    output wire         idle_all,               // not sampling and every line is in the DDR2
    output reg          overrun,
    output reg  [31:0]  count, lost,
    output reg  [19:0]  lines,
    // the reader
    input  wire         rd_start,
    input  wire [18:0]  rd_line_ix,
    output reg          rd_busy, rd_valid,
    output wire [127:0] rd_data,
    // mig_arb's engine client
    input  wire         ui_clk,
    input  wire         ui_rst,
    input  wire         calib,
    output wire         e_en,
    output wire [26:0]  e_addr,
    output wire [2:0]   e_cmd,
    output wire [127:0] e_wdf_data,
    output wire [15:0]  e_wdf_mask,
    output wire         e_wdf_wren,
    input  wire         e_rdy,
    input  wire         e_rd_valid,
    input  wire [127:0] e_rd_data
);
    localparam [19:0] RING_LINES = 20'd524288;

    // ---- the lane muxes, two stages: 16:1 inside eight chunks, then the chunk
    reg  [255:0] m1 = 256'd0;
    reg  [31:0]  smp = 32'd0;
    genvar l, c;
    generate for (l = 0; l < 32; l = l + 1) begin : lane
        if (l < LANES) begin : used
            wire [6:0] sl = sel[7*l +: 7];
            for (c = 0; c < 8; c = c + 1) begin : chunk
                always @(posedge clk) m1[8*l + c] <= live[16*c + sl[3:0]];
            end
            always @(posedge clk) smp[l] <= m1[8*l + sl[6:4]];
        end else begin : unused
            always @(posedge clk) smp[l] <= 1'b0;
        end
    end endgenerate

    // ---- sampling
    reg         issuing = 1'b0, v1 = 1'b0, v2 = 1'b0, fin = 1'b0;
    reg  [23:0] kreg = 24'd1, kcnt = 24'd0;
    reg  [31:0] mreg = 32'd0, mtaken = 32'd0;
    reg  [1:0]  lane_i = 2'd0, wb = 2'd0;
    reg         drop = 1'b0;
    reg  [127:0] lb [0:3];
    reg  [3:0]  pub_t = 4'd0;
    reg  [18:0] pub_ln [0:3];
    (* ASYNC_REG = "TRUE" *) reg [3:0] ack_s1 = 4'd0, ack_s2 = 4'd0;
    wire [3:0]  ack_t;
    wire        tick = issuing && !stop && kcnt == 24'd0;
    wire [3:0]  free = ~(pub_t ^ ack_s2);
    assign sampling = issuing | v1 | v2 | fin;
    assign idle_all = !sampling && (pub_t == ack_s2);
    wire [31:0] need = (m + 32'd3) >> 2;
    wire        bad = go && (k == 24'd0 || m == 32'd0 || m > 32'd2097152 || sampling || ({12'd0, lines} + need) > {12'd0, RING_LINES});
    integer i;
    initial begin for (i = 0; i < 4; i = i + 1) begin lb[i] = 128'd0; pub_ln[i] = 19'd0; end refused = 1'b0; overrun = 1'b0; count = 32'd0; lost = 32'd0; lines = 20'd0; rd_busy = 1'b0; rd_valid = 1'b0; end

    always @(posedge clk) begin
        ack_s1 <= ack_t; ack_s2 <= ack_s1;
        v1 <= tick; v2 <= v1;
        if (go) begin
            refused <= bad;
            if (!bad) begin issuing <= 1'b1; kcnt <= 24'd0; kreg <= k; mreg <= m; mtaken <= 32'd0; end
        end else if (issuing) begin
            if (stop) issuing <= 1'b0;
            else if (kcnt == 24'd0) begin
                kcnt <= kreg - 24'd1;
                count <= count + 32'd1;
                mtaken <= mtaken + 32'd1;
                if (mtaken + 32'd1 == mreg) issuing <= 1'b0;
            end else kcnt <= kcnt - 24'd1;
        end
        // the line register: v2 = the sample of the tick two clocks ago is in smp
        if (v2) begin
            if (lane_i == 2'd0) begin
                if (free[wb]) begin lb[wb] <= {96'd0, smp}; drop <= 1'b0; end
                else begin drop <= 1'b1; lost <= lost + 32'd1; overrun <= 1'b1; end
            end else if (!drop) begin
                lb[wb][32*lane_i +: 32] <= smp;
            end else lost <= lost + 32'd1;
            lane_i <= lane_i + 2'd1;
            if (lane_i == 2'd3) begin
                lines <= lines + 20'd1;
                if (!drop) begin pub_t[wb] <= ~pub_t[wb]; pub_ln[wb] <= lines[18:0]; wb <= wb + 2'd1; end
            end
        end
        // the end of a capture: a partial line goes out with zeros behind its samples
        if (!issuing && !v1 && !v2 && lane_i != 2'd0 && !fin) fin <= 1'b1;
        if (fin) begin
            fin <= 1'b0; lane_i <= 2'd0; lines <= lines + 20'd1;
            if (!drop) begin pub_t[wb] <= ~pub_t[wb]; pub_ln[wb] <= lines[18:0]; wb <= wb + 2'd1; end
        end
        if (ring_rst && !sampling && idle_all) begin lines <= 20'd0; count <= 32'd0; lost <= 32'd0; overrun <= 1'b0; end
        if (rst) begin issuing <= 1'b0; v1 <= 1'b0; v2 <= 1'b0; fin <= 1'b0; lane_i <= 2'd0; end
    end

    // ---- ui_clk: the writer and the reader share the one command at a time of the client
    (* ASYNC_REG = "TRUE" *) reg [3:0] pub_s1 = 4'd0, pub_s2 = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg       rq_s1 = 1'b0, rq_s2 = 1'b0;
    reg         rq_t = 1'b0;                    // cpu_clk
    reg  [18:0] rq_ln = 19'd0;
    reg         rd_done_t = 1'b0;               // ui_clk
    (* ASYNC_REG = "TRUE" *) reg dn_s1 = 1'b0, dn_s2 = 1'b0;
    reg  [127:0] u_rd = 128'd0;
    reg  [3:0]  ack_r = 4'd0;
    reg  [1:0]  rb = 2'd0;
    reg  [1:0]  us = 2'd0;                      // 0 idle, 1 write command, 2 read command, 3 waits for the read data
    assign ack_t = ack_r;
    wire        ok = calib && !ui_rst;
    assign e_en = (us == 2'd1) || (us == 2'd2);
    assign e_cmd = (us == 2'd1) ? 3'b000 : 3'b001;
    assign e_addr = BASE_W + {5'd0, (us == 2'd1) ? pub_ln[rb] : rq_ln, 3'b000};
    assign e_wdf_data = lb[rb];
    assign e_wdf_mask = 16'd0;
    assign e_wdf_wren = (us == 2'd1);
    always @(posedge ui_clk) begin
        pub_s1 <= pub_t; pub_s2 <= pub_s1;
        rq_s1 <= rq_t; rq_s2 <= rq_s1;
        case (us)
        2'd0: if (ok) begin
            if (pub_s2[rb] != ack_r[rb]) us <= 2'd1;
            else if (rq_s2 != rd_done_t) us <= 2'd2;
        end
        2'd1: if (e_rdy) begin   // a pending next buffer goes out in the very next clock (mig_arb takes one command a clock)
            ack_r[rb] <= ~ack_r[rb]; rb <= rb + 2'd1;
            us <= (pub_s2[rb + 2'd1] != ack_r[rb + 2'd1]) ? 2'd1 : 2'd0;
        end
        2'd2: if (e_rdy) us <= 2'd3;
        default: if (e_rd_valid) begin u_rd <= e_rd_data; rd_done_t <= ~rd_done_t; us <= 2'd0; end
        endcase
        if (ui_rst) us <= 2'd0;
    end

    // ---- the reader's cpu_clk side
    assign rd_data = u_rd;
    always @(posedge clk) begin
        dn_s1 <= rd_done_t; dn_s2 <= dn_s1;
        if (rd_start && !rd_busy) begin rq_ln <= rd_line_ix; rq_t <= ~rq_t; rd_busy <= 1'b1; rd_valid <= 1'b0; end
        else if (rd_busy && dn_s2 == rq_t) begin rd_busy <= 1'b0; rd_valid <= 1'b1; end
    end
endmodule
