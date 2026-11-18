// T92 D10: the DDR2 sector service of the FDC. u765 (the NEC upd765 of Amstrad_MiSTer, rtl/u765) asks for 512 byte blocks of the disk image
// the way the MiSTer framework's SD interface is asked: sd_rd / sd_wr (one bit per drive) with sd_lba, then it waits for sd_ack to rise and
// fall; while sd_ack is high the requester moves the 512 bytes through sd_buff_addr / sd_buff_dout / sd_buff_wr (a read) or reads
// sd_buff_din (a write). Here the image is a block of the DDR2 that the app filled (D0_BASE / D1_BASE, size in bytes), so a block is 32
// lines of 16 bytes through the host's real-time port c_* (ui_clk): a request by toggle, compared as a level with the done toggle (at most one
// line in flight, T233 D3 below), its address and data held stable until the next request. A line (16 bytes) beyond the end of the image reads as zeros (oob is set) and a
// write of it is dropped; the lines of a block that are inside the image are served (an image of 204,544 bytes ends 256 bytes into block 399).
// A written block sets one bit per 16 KB of the image in `dirty` (the app flushes those chunks to the file and clears the bits). Written for this project (TREX), no upstream code.
// T233 D3: every wait is bounded. The ui_clk side gives up a line the DDR2 does not take or answer within U_LIM ui clocks; the sys_clk side gives
// up a line wait after S_LIM sys clocks. A lost line ends the block at once with sd_err high at the fall of sd_ack (u765 ends the command with
// ST0 0x40 / ST1 0x05). The request and done toggles are compared as levels: a line goes out only when the previous one is done, so a line left in
// flight by a reset is answered first and never taken as the next one's answer; a DDR2 side reset (ui_rst) answers it as failed.
`timescale 1ns / 1ps
module cpc_fdc (
    input  wire         clk,                        // sys_clk
    input  wire         rst,
    input  wire         hold,
    // u765
    input  wire [31:0]  sd_lba,
    input  wire [1:0]   sd_rd,
    input  wire [1:0]   sd_wr,
    output reg          sd_ack = 1'b0,
    output reg  [8:0]   sd_buff_addr = 9'd0,
    output reg  [7:0]   sd_buff_dout = 8'd0,
    output reg          sd_buff_wr = 1'b0,
    output reg          sd_err = 1'b0,              // T233 D3: the block just ended (sd_ack fell) failed; held until the next block starts
    input  wire [7:0]   sd_buff_din,
    // the images (cpu_clk values through the caller's synchronisers)
    input  wire [31:0]  d0_base,
    input  wire [31:0]  d0_size,
    input  wire [31:0]  d1_base,
    input  wire [31:0]  d1_size,
    output reg  [63:0]  dirty0 = 64'd0,
    output reg  [63:0]  dirty1 = 64'd0,
    input  wire         clr_tgl,
    input  wire         clr_d,
    input  wire [2:0]   clr_i,
    input  wire [7:0]   clr_a,
    output wire         busy,
    output reg          oob = 1'b0,
    // the host's real-time DDR2 port c_* (ui_clk)
    input  wire         ui_clk,
    input  wire         ui_rst,
    output reg  [26:0]  c_addr = 27'd0,
    output reg  [2:0]   c_cmd = 3'd0,
    output wire         c_en,
    input  wire         c_rdy,
    output reg  [127:0] c_wdf_data = 128'd0,
    output wire [15:0]  c_wdf_mask,
    output wire         c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire         c_rd_valid,
    output wire         ui_busy
);
    localparam [3:0] S_IDLE = 4'd0, S_CHECK = 4'd1, S_RREQ = 4'd2, S_RWAIT = 4'd3, S_RPUT = 4'd4, S_WGET0 = 4'd5, S_WGET1 = 4'd6, S_WGET2 = 4'd7,
                     S_WSEND = 4'd8, S_WWAIT = 4'd9, S_END = 4'd10, S_END2 = 4'd11, S_FAIL = 4'd12;
    localparam [16:0] S_LIM = 17'h1FFFF;           // T233 D3: sys clocks a line may wait (4.2 ms at 31.5 MHz; the ui side gives up after U_LIM first)
    reg [3:0]   st = S_IDLE;
    reg         drv = 1'b0, is_wr = 1'b0, any_w = 1'b0;
    reg [31:0]  lba = 32'd0, img_base = 32'd0;
    reg [5:0]   line = 6'd0;
    reg [4:0]   k = 5'd0;
    reg [127:0] ldata = 128'd0;
    reg         rq_tgl = 1'b0, rq_wr = 1'b0;
    reg [26:0]  rq_addr = 27'd0;
    reg [127:0] rq_data = 128'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] dn_s = 3'b000;
    reg         dn_tgl = 1'b0;
    reg [127:0] ui_line = 128'd0;
    reg         ui_err = 1'b0;                     // the line of the last done toggle was not answered (ui_clk, read with ui_line)
    reg [16:0]  tmr = 17'd0;                       // sys clocks of the current line wait
    (* ASYNC_REG = "TRUE" *) reg [2:0] clr_s = 3'b000;         // the app's dirty-clear toggle (cpu_clk) through three flip-flops
    assign busy = (st != S_IDLE);
    // The host's DDR2 side resetting (ui_rst) drops the transaction in flight: the ui_clk side answers it as failed (ui_err) and goes idle. The
    // sys_clk side leaves the block with it (sd_err high at the fall of sd_ack), or sd_ack would stay high and u765 would get no other block.
    (* ASYNC_REG = "TRUE" *) reg [2:0] urst_s = 3'b111;
    always @(posedge clk) urst_s <= {urst_s[1:0], ui_rst};
    wire rst_i = rst | urst_s[2];

    wire [31:0] size = drv ? d1_size : d0_size;
    // the line under `line` and the one after it: is it entirely inside the image?
    wire [41:0] ln_end  = {1'b0, lba, 9'd0} + {33'd0, line[4:0], 4'd0} + 42'd16;
    wire [41:0] ln_end1 = {1'b0, lba, 9'd0} + {33'd0, line[4:0], 4'd0} + 42'd32;
    wire        in_img  = (ln_end  <= {10'd0, size});
    wire        in_img1 = (ln_end1 <= {10'd0, size});
    wire [31:0] byte_a = img_base + {lba[22:0], 9'd0} + {22'd0, line[4:0], 4'd0};
    wire [5:0]  chunk = lba[10:5];                  // one bit per 16 KB of the image (32 sectors)

    always @(posedge clk) begin
        dn_s <= {dn_s[1:0], dn_tgl};
        clr_s <= {clr_s[1:0], clr_tgl};
        if (clr_s[2] != clr_s[1]) begin                        // the data of the app side has been stable for two clocks by now
            if (!clr_d) dirty0[8 * clr_i +: 8] <= dirty0[8 * clr_i +: 8] & ~clr_a;
            else dirty1[8 * clr_i +: 8] <= dirty1[8 * clr_i +: 8] & ~clr_a;
        end
        sd_buff_wr <= 1'b0;
        if (rst_i) begin
            if (sd_ack) sd_err <= 1'b1;                // a block cut by a reset ends as failed
            st <= S_IDLE; sd_ack <= 1'b0; oob <= 1'b0;
        end else if (!hold || st != S_IDLE) begin
            if (st == S_RREQ || st == S_RWAIT || st == S_WSEND || st == S_WWAIT) tmr <= tmr + 17'd1; else tmr <= 17'd0;
            case (st)
            S_IDLE: if ((|sd_rd) || (|sd_wr)) begin
                sd_err <= 1'b0;
                drv <= sd_rd[1] | sd_wr[1];
                is_wr <= |sd_wr;
                lba <= sd_lba;
                img_base <= (sd_rd[1] | sd_wr[1]) ? d1_base : d0_base;
                line <= 6'd0; k <= 5'd0; any_w <= 1'b0;
                sd_ack <= 1'b1;
                st <= S_CHECK;
            end
            S_CHECK: begin                                 // drv, lba and size are settled: is the first line inside the image?
                if (!in_img) oob <= 1'b1;
                if (is_wr) st <= S_WGET0;
                else if (in_img) st <= S_RREQ;
                else begin ldata <= 128'd0; k <= 5'd0; st <= S_RPUT; end
            end
            // ---- read: a line at a time into u765's buffer
            S_RREQ: if (dn_s[2] == rq_tgl) begin           // the previous line is done (also one a reset left in flight)
                rq_addr <= byte_a[27:1]; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; tmr <= 17'd0;
                st <= S_RWAIT;
            end else if (tmr == S_LIM) st <= S_FAIL;
            S_RWAIT: if (dn_s[2] == rq_tgl) begin
                if (ui_err) st <= S_FAIL;
                else begin ldata <= ui_line; k <= 5'd0; st <= S_RPUT; end
            end else if (tmr == S_LIM) st <= S_FAIL;
            S_RPUT: begin                                  // one byte of the line into the buffer per clock
                sd_buff_addr <= {line[4:0], k[3:0]};
                sd_buff_dout <= ldata[8 * k[3:0] +: 8];
                sd_buff_wr <= 1'b1;
                k <= k + 5'd1;
                if (k[3:0] == 4'd15) begin
                    if (line == 6'd31) st <= S_END;
                    else begin
                        line <= line + 6'd1;
                        if (in_img1) st <= S_RREQ; else begin ldata <= 128'd0; k <= 5'd0; oob <= 1'b1; end
                    end
                end
            end
            // ---- write: 16 bytes from u765's buffer (address, one clock for its read register, capture), then the line to the DDR2
            S_WGET0: begin sd_buff_addr <= {line[4:0], k[3:0]}; st <= S_WGET1; end
            S_WGET1: st <= S_WGET2;
            S_WGET2: begin
                ldata[8 * k[3:0] +: 8] <= sd_buff_din;
                k <= k + 5'd1;
                if (k[3:0] == 4'd15) st <= S_WSEND;   // ldata holds the 16 bytes; rq_data takes them at the request (S_WSEND)
                else st <= S_WGET0;
            end
            S_WSEND: begin
                if (!in_img) oob <= 1'b1;
                if (in_img) begin
                    if (dn_s[2] == rq_tgl) begin           // the previous line is done: its data may change now
                        rq_addr <= byte_a[27:1]; rq_data <= ldata; rq_wr <= 1'b1; rq_tgl <= ~rq_tgl; tmr <= 17'd0;
                        any_w <= 1'b1;
                        st <= S_WWAIT;
                    end else if (tmr == S_LIM) st <= S_FAIL;
                end else if (line == 6'd31) st <= S_END;
                else begin line <= line + 6'd1; k <= 5'd0; st <= S_WGET0; end
            end
            S_WWAIT: if (dn_s[2] == rq_tgl) begin
                if (ui_err) st <= S_FAIL;
                else if (line == 6'd31) st <= S_END;
                else begin line <= line + 6'd1; k <= 5'd0; st <= S_WGET0; end
            end else if (tmr == S_LIM) st <= S_FAIL;
            S_FAIL: begin sd_err <= 1'b1; st <= S_END; end   // the block ends here, sd_err high with the fall of sd_ack
            S_END: begin
                if (is_wr && any_w) begin
                    if (drv) dirty1[chunk] <= 1'b1; else dirty0[chunk] <= 1'b1;
                end
                sd_ack <= 1'b0;
                st <= S_END2;
            end
            S_END2: st <= S_IDLE;                          // u765 sees the fall and drops its busy flag before the next request is looked at
            default: st <= S_IDLE;
            endcase
        end
    end

    // ------------------------------------------------------------------ the ui_clk side
    localparam [1:0] U_IDLE = 2'd0, U_ISSUE = 2'd1, U_WAITR = 2'd2;
    localparam [15:0] U_LIM = 16'hFFFF;            // T233 D3: ui clocks a line may take (0.87 ms at 75 MHz; the DDR2 answers in well under a microsecond)
    reg [1:0]   us = U_IDLE;
    reg [15:0]  utmr = 16'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg [26:0]  u_addr = 27'd0;
    reg         u_wr = 1'b0;
    reg         c_en_i = 1'b0, c_wr_i = 1'b0;
    assign c_en = c_en_i;
    assign c_wdf_wren = c_wr_i;
    assign c_wdf_mask = 16'h0000;
    assign ui_busy = (us != U_IDLE);
    always @(posedge ui_clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        utmr <= (us == U_IDLE) ? 16'd0 : utmr + 16'd1;
        if (ui_rst) begin
            us <= U_IDLE; c_en_i <= 1'b0; c_wr_i <= 1'b0;
            if (dn_tgl != rq_s[2]) begin ui_err <= 1'b1; dn_tgl <= rq_s[2]; end   // the line in flight is answered as failed
        end else case (us)
        U_IDLE: if (rq_s[2] != dn_tgl) begin u_addr <= rq_addr; u_wr <= rq_wr; us <= U_ISSUE; end   // a level: a request is never missed
        U_ISSUE: begin
            c_en_i <= 1'b1;
            c_addr <= u_addr;
            if (u_wr) begin c_cmd <= 3'b000; c_wr_i <= 1'b1; c_wdf_data <= rq_data; end
            else begin c_cmd <= 3'b001; c_wr_i <= 1'b0; end
            if (c_en_i && c_rdy) begin
                c_en_i <= 1'b0; c_wr_i <= 1'b0;
                if (u_wr) begin ui_err <= 1'b0; dn_tgl <= ~dn_tgl; us <= U_IDLE; end else us <= U_WAITR;
            end else if (utmr == U_LIM) begin
                c_en_i <= 1'b0; c_wr_i <= 1'b0; ui_err <= 1'b1; dn_tgl <= ~dn_tgl; us <= U_IDLE;
            end
        end
        U_WAITR: if (c_rd_valid) begin ui_line <= c_rd_data; ui_err <= 1'b0; dn_tgl <= ~dn_tgl; us <= U_IDLE; end
                 else if (utmr == U_LIM) begin ui_err <= 1'b1; dn_tgl <= ~dn_tgl; us <= U_IDLE; end
        default: us <= U_IDLE;
        endcase
    end
endmodule
