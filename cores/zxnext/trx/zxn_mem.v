// T116 D2 / P2, P3: the Next's 2 MB address space (ROM, DivMMC, 1792 KB of RAM: o_RAM_A_ADDR, 21 bits) and Layer 2's second port (o_RAM_B_*) in
// the DDR2, behind two caches in block RAM, over the host's real-time port c_* (ui_clk). The SRAM of the original answers in one 28 MHz cycle;
// the DDR2 answers a line in 0.35 to 0.85 us (T95 5.9), so
//   - port A (CPU and DMA, read and write) has a 32 KB direct-mapped cache of 16 byte lines, write through without allocation. A read that
//     hits is answered three cpu_clk after the request appears; a miss HOLDS THE WHOLE MACHINE (`stall` takes the clock enables away,
//     zxn_clk) until the line is in: the machine's bus cycles are those of a memory that always answers in time and everything stops
//     together (T95 5.9, the Atari core does the same). The CPU is held and so is the rest; the machine runs a little slower while it waits.
//   - port B (Layer 2 reads one byte per 7 MHz pixel, row by row in 256x192 and COLUMN by column in 320x256 and 640x256, address
//     {x, y}) has a 16 KB cache of 1024 lines with a next-line prefetch: an access to the second half of a line asks for the line behind it
//     (the next 16 pixels of a row, or the next 16 lines of a column), so the band of the next 16 scan lines is in before the beam gets
//     there. A prefetch never stalls the machine; a demand miss does. Writes by the CPU invalidate a line of this cache they hit.
// Both are keyed by the physical address the machine produced, so a bank switch needs no invalidation. The engine does one DDR2 command
// at a time and takes, in this order: the buffered write, the miss of A, the miss of B, a line of the SD buffer's job, a prefetch. A
// written byte goes to the DDR2 as a 128 bit write with one byte lane enabled (the circuit of nes_mem.v).
// Timing: the request signals of the machine are combinational functions of flops that change at a clock-enable pulse (zxn_clk); they are
// captured by the cpu_clk after it (one clock, core_timing.tcl does not relax this) and the block RAMs are read from that capture. The
// machine takes a read's byte at the second CPU clock edge after the request at the earliest (a wait state at 28 MHz: sram_wait_n, zxnext.vhd).
`timescale 1ns / 1ps
module zxn_mem (
    input  wire         clk,
    input  wire         rst,
    input  wire [31:0]  ram_base,          // DDR2 byte address of the 2 MB image of the address space (bits 20:0 ignored)
    // port A
    input  wire [20:0]  a_addr,
    input  wire         a_req_t,           // o_RAM_A_REQ: high from the master cycle a request begins until the next r28
    input  wire         a_rd_n,
    input  wire [7:0]   a_do,
    output reg  [7:0]   a_di = 8'd0,
    // port B
    input  wire [20:0]  b_addr,
    input  wire         b_req_t,           // o_RAM_B_REQ_T: a toggle
    output reg  [7:0]   b_di = 8'd0,
    output wire         stall,
    // the SD card's 512 byte buffer (zxn_sd) and its staging sector in the DDR2: a job moves 32 lines between the two
    input  wire         sd_start,
    input  wire         sd_to_ddr,         // 1: buffer -> staging sector, 0: staging sector -> buffer
    input  wire [31:0]  sd_stage,          // DDR2 byte address of the staging sector (512 aligned)
    output reg          sd_done = 1'b0,
    output reg  [4:0]   sd_k = 5'd0,
    output reg          sd_we = 1'b0,
    output reg  [127:0] sd_wdata = 128'd0,
    input  wire [127:0] sd_rdata,          // the buffer's line sd_k, valid one clock after sd_k
    output wire         busy,              // the engine has something on its way (Freeze waits for it)
    // the host's real-time DDR2 port (ui_clk)
    input  wire         ui_clk,
    input  wire         ui_rst,
    output reg  [26:0]  c_addr = 27'd0,
    output reg  [2:0]   c_cmd = 3'd0,
    output wire         c_en,
    input  wire         c_rdy,
    output reg  [127:0] c_wdf_data = 128'd0,
    output reg  [15:0]  c_wdf_mask = 16'hFFFF,
    output wire         c_wdf_wren,
    input  wire [127:0] c_rd_data,
    input  wire         c_rd_valid,
    // counters for the bring-up
    output reg  [31:0]  st_miss_a = 32'd0,
    output reg  [31:0]  st_miss_b = 32'd0,
    output reg  [31:0]  st_pref = 32'd0,
    output reg  [31:0]  st_stall = 32'd0,
    output reg  [31:0]  st_late = 32'd0       // Layer 2 requests that had to wait for a line (the prefetch was not there in time)
);
    // ================================================================ port A: arrays
    reg [20:0] ca_q = 21'd0;
    reg [7:0]  cd_q = 8'd0;
    reg        crd_q = 1'b1;
    reg        a_tq = 1'b0, a_go = 1'b0, a_go2 = 1'b0;
    wire       a_start = a_req_t & ~a_tq;
    // index = the line number's low 11 bits folded with the 6 bits above them; tag = a_addr[20:15] and a valid bit
    function [10:0] aidx(input [20:0] a);
        aidx = a[14:4] ^ {5'd0, a[20:15]};
    endfunction
    (* ram_style = "block" *) reg [127:0] adat [0:2047];
    (* ram_style = "block" *) reg [6:0]   atag [0:2047];
    reg [127:0] adq = 128'd0;
    reg [6:0]   atq = 7'd0;
    reg         atwe = 1'b0;
    reg [10:0]  ataddr = 11'd0;
    reg [6:0]   atdata = 7'd0;
    reg [15:0]  adwe = 16'd0;
    reg [10:0]  adaddr = 11'd0;
    reg [127:0] addata = 128'd0;
    integer     bi;
    always @(posedge clk) begin
        adq <= adat[aidx(ca_q)];
        atq <= atag[aidx(ca_q)];
        for (bi = 0; bi < 16; bi = bi + 1) if (adwe[bi]) adat[adaddr][8 * bi +: 8] <= addata[8 * bi +: 8];
        if (atwe) atag[ataddr] <= atdata;
    end
    wire a_hit = atq[6] && (atq[5:0] == ca_q[20:15]);
    function [7:0] lane8(input [127:0] w, input [3:0] l);
        lane8 = w[8 * l +: 8];
    endfunction

    // ================================================================ port B: arrays
    function [9:0] bidx(input [20:0] a);       // {line bit 0, address bits 16:8}: a row's lines alternate, a column's x is injective
        bidx = {a[4], a[16:8]};
    endfunction
    function [7:0] btag_of(input [20:0] a);    // valid and what bidx leaves of the line number: address bits 20:17 and 7:5
        btag_of = {1'b1, a[20:17], a[7:5]};
    endfunction
    (* ram_style = "block" *) reg [127:0] bdat [0:1023];
    (* ram_style = "block" *) reg [7:0]   btag [0:1023];
    reg [9:0]   b_ra = 10'd0;
    reg [127:0] bdq = 128'd0;
    reg [7:0]   btq = 8'd0;
    reg         btwe = 1'b0, bdwe = 1'b0;
    reg [9:0]   btaddr = 10'd0, bdaddr = 10'd0;
    reg [7:0]   btdata = 8'd0;
    reg [127:0] bddata = 128'd0;
    always @(posedge clk) begin
        bdq <= bdat[b_ra];
        btq <= btag[b_ra];
        if (bdwe) bdat[bdaddr] <= bddata;
        if (btwe) btag[btaddr] <= btdata;
    end

    // ================================================================ the engine's state
    localparam [2:0] J_NONE = 3'd0, J_WR = 3'd1, J_AF = 3'd2, J_BF = 3'd3, J_SD = 3'd4, J_BP = 3'd5, J_SDI = 3'd6;
    reg [2:0]   job = J_NONE;
    reg         rq_tgl = 1'b0;
    reg         rq_wr = 1'b0;
    reg [26:0]  rq_addr = 27'd0;
    reg [15:0]  rq_mask = 16'hFFFF;
    reg [127:0] rq_data = 128'd0;
    reg         dn_tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] dn_s = 3'b000;
    reg [127:0] ui_line = 128'd0;
    wire        dn_ev = dn_s[2] ^ dn_s[1];
    wire [31:0] rbase = {ram_base[31:21], 21'd0};

    reg         wb_v = 1'b0;                       // the buffered write
    reg [20:0]  wb_a = 21'd0;
    reg [7:0]   wb_d = 8'd0;
    reg         pw_v = 1'b0;                       // a write that waits for the buffer (the machine is held meanwhile)
    reg [20:0]  pw_a = 21'd0;
    reg [7:0]   pw_d = 8'd0;
    reg         inv_pend = 1'b0;                   // a write wants Layer 2's cache to drop the line
    reg [20:0]  inv_a = 21'd0;
    reg         a_missing = 1'b0, b_missing = 1'b0;
    reg [20:0]  a_ma = 21'd0, b_ma = 21'd0;
    reg         pf_pend = 1'b0;
    reg [20:0]  pf_a = 21'd0;
    reg [20:0]  jline = 21'd0;
    reg         sdj = 1'b0, sdj_dir = 1'b0;
    reg [31:0]  sdj_base = 32'd0;
    reg [4:0]   sdp = 5'd0;                        // clocks the buffer's read needs before its line is taken

    reg [11:0]  fl_cnt = 12'd2048;
    wire        sweeping = (fl_cnt != 12'd2048);
    reg         rst_q = 1'b0;
    assign stall = a_missing | b_missing | pw_v | sweeping | rst;
    always @(posedge clk) if ((a_missing | b_missing | pw_v) && !rst) st_stall <= st_stall + 32'd1;
    assign busy = (job != J_NONE) | wb_v | pw_v | sdj | a_missing | b_missing | pf_pend | inv_pend;

    // ---- port B lookup FSM
    localparam [2:0] B_IDLE = 3'd0, B_L1 = 3'd1, B_L2 = 3'd2, B_P1 = 3'd3, B_P2 = 3'd4, B_I1 = 3'd5, B_I2 = 3'd6;
    reg [2:0]   bs = B_IDLE;
    reg         b_tq = 1'b0, b_pend = 1'b0;
    reg [20:0]  bq = 21'd0;
    reg [20:0]  bp = 21'd0;
    wire        b_start = (b_req_t != b_tq);
    wire [20:0] bq_next = {bq[20:4] + 17'd1, 4'd0};

    wire        wr_go = a_go2 & ~crd_q;
    wire        rd_go = a_go2 & crd_q;
    wire [15:0] lane_mask = 16'd1 << ca_q[3:0];

    always @(posedge clk) begin
        atwe <= 1'b0; adwe <= 16'd0; btwe <= 1'b0; bdwe <= 1'b0;
        sd_done <= 1'b0; sd_we <= 1'b0;
        rst_q <= rst;
        dn_s <= {dn_s[1:0], dn_tgl};
        // ---------------- port A: the request is captured when it starts and held; the arrays are read with the held address one clock
        // later (a_go), the lookup is looked at one clock after that (a_go2). The machine's address is a long combinational path, so it
        // ends in these flops and not in the block RAMs' address pins (CI 942: -0.401 ns to adat/ADDRBWRADDR in a single cpu_clk).
        a_tq <= a_req_t;
        if (a_start) begin ca_q <= a_addr; cd_q <= a_do; crd_q <= ~a_rd_n; end
        a_go <= a_start & ~rst;
        a_go2 <= a_go & ~rst;
        if (rd_go) begin
            if (a_hit) a_di <= lane8(adq, ca_q[3:0]);
            else begin a_missing <= 1'b1; a_ma <= ca_q; st_miss_a <= st_miss_a + 32'd1; end
        end
        if (wr_go) begin
            if (a_hit) begin
                adwe <= lane_mask;
                adaddr <= aidx(ca_q); addata <= {16{cd_q}};
            end
            if (!wb_v && !inv_pend && !pw_v) begin
                wb_v <= 1'b1; wb_a <= ca_q; wb_d <= cd_q; inv_pend <= 1'b1; inv_a <= ca_q;
            end else begin
                pw_v <= 1'b1; pw_a <= ca_q; pw_d <= cd_q;
            end
        end
        if (pw_v && !wb_v && !inv_pend && !wr_go) begin
            wb_v <= 1'b1; wb_a <= pw_a; wb_d <= pw_d; inv_pend <= 1'b1; inv_a <= pw_a; pw_v <= 1'b0;
        end
        // ---------------- the engine: select a job
        case (job)
        J_NONE: if (!sweeping && !rst) begin
            if (wb_v) begin
                rq_addr <= (rbase + {11'd0, wb_a}) >> 1; rq_wr <= 1'b1;
                rq_data <= {16{wb_d}}; rq_mask <= ~(16'd1 << wb_a[3:0]); rq_tgl <= ~rq_tgl; job <= J_WR; wb_v <= 1'b0;
            end else if (a_missing) begin
                rq_addr <= (rbase + {11'd0, a_ma[20:4], 4'd0}) >> 1; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; job <= J_AF;
            end else if (b_missing) begin
                rq_addr <= (rbase + {11'd0, b_ma[20:4], 4'd0}) >> 1; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; job <= J_BF;
                st_miss_b <= st_miss_b + 32'd1;
            end else if (sdj) begin
                if (sdj_dir && sdp != 5'd20) sdp <= sdp + 5'd1;                  // zxn_sd reads the 16 bytes of the line in 18 clocks
                else begin
                    rq_addr <= (sdj_base + {23'd0, sd_k, 4'd0}) >> 1; rq_wr <= sdj_dir; rq_data <= sd_rdata; rq_mask <= 16'h0000;
                    rq_tgl <= ~rq_tgl; job <= J_SD; sdp <= 5'd0;
                end
            end else if (pf_pend) begin
                rq_addr <= (rbase + {11'd0, pf_a[20:4], 4'd0}) >> 1; rq_wr <= 1'b0; rq_tgl <= ~rq_tgl; job <= J_BP; jline <= pf_a; pf_pend <= 1'b0;
                st_pref <= st_pref + 32'd1;
            end
        end
        J_WR: if (dn_ev) job <= J_NONE;
        J_AF: if (dn_ev) begin
            adwe <= 16'hFFFF; adaddr <= aidx(a_ma); addata <= ui_line;
            atwe <= 1'b1; ataddr <= aidx(a_ma); atdata <= {1'b1, a_ma[20:15]};
            a_di <= lane8(ui_line, a_ma[3:0]); a_missing <= 1'b0; job <= J_NONE;
        end
        J_BF: if (dn_ev) begin
            bdwe <= 1'b1; bdaddr <= bidx(b_ma); bddata <= ui_line;
            btwe <= 1'b1; btaddr <= bidx(b_ma); btdata <= btag_of(b_ma);
            b_di <= lane8(ui_line, b_ma[3:0]); b_missing <= 1'b0; job <= J_NONE; st_late <= st_late + 32'd1;
        end
        J_BP: if (dn_ev) begin
            bdwe <= 1'b1; bdaddr <= bidx(jline); bddata <= ui_line;
            btwe <= 1'b1; btaddr <= bidx(jline); btdata <= btag_of(jline);
            job <= J_NONE;
        end
        J_SD: if (dn_ev) begin
            if (!sdj_dir) begin sd_wdata <= ui_line; sd_we <= 1'b1; end          // staging -> buffer, line sd_k (sd_k moves a clock later)
            job <= J_SDI;
        end
        J_SDI: begin
            if (sd_k == 5'd31) begin sd_done <= 1'b1; sdj <= 1'b0; sd_k <= 5'd0; end
            else sd_k <= sd_k + 5'd1;
            job <= J_NONE;
        end
        default: job <= J_NONE;
        endcase
        if (sd_start && !sdj) begin
            sdj <= 1'b1; sdj_dir <= sd_to_ddr; sdj_base <= sd_stage; sd_k <= 5'd0; sdp <= 5'd0;
        end
        // ---------------- port B: request, lookup, prefetch, invalidation
        b_tq <= b_req_t;
        if (b_start && !rst) begin b_pend <= 1'b1; bq <= b_addr; end
        case (bs)
        B_IDLE: begin
            if (b_pend) begin b_ra <= bidx(bq); b_pend <= 1'b0; bs <= B_L1; end
            else if (inv_pend) begin b_ra <= bidx(inv_a); bp <= inv_a; inv_pend <= 1'b0; bs <= B_I1; end
        end
        B_L1: bs <= B_L2;
        B_L2: begin
            if (btq == btag_of(bq)) begin
                b_di <= lane8(bdq, bq[3:0]);
                if (bq[3] && !pf_pend && bq[20:4] != 17'h1FFFF) begin b_ra <= bidx(bq_next); bp <= bq_next; bs <= B_P1; end
                else bs <= B_IDLE;
            end else begin
                b_missing <= 1'b1; b_ma <= bq; bs <= B_IDLE;
            end
        end
        B_P1: bs <= B_P2;
        B_P2: begin
            if (btq != btag_of(bp)) begin pf_pend <= 1'b1; pf_a <= bp; end
            bs <= B_IDLE;
        end
        B_I1: bs <= B_I2;
        B_I2: begin
            if (btq == btag_of(bp)) begin btwe <= 1'b1; btaddr <= bidx(bp); btdata <= 8'd0; end
            bs <= B_IDLE;
        end
        default: bs <= B_IDLE;
        endcase
        // ---------------- reset: flush both caches
        if (rst && !rst_q) fl_cnt <= 12'd0;
        if (sweeping) begin
            atwe <= 1'b1; ataddr <= fl_cnt[10:0]; atdata <= 7'd0;
            if (fl_cnt < 12'd1024) begin btwe <= 1'b1; btaddr <= fl_cnt[9:0]; btdata <= 8'd0; end
            fl_cnt <= fl_cnt + 12'd1;
        end
        if (rst) begin
            a_missing <= 1'b0; b_missing <= 1'b0; pw_v <= 1'b0; wb_v <= 1'b0; pf_pend <= 1'b0; inv_pend <= 1'b0; b_pend <= 1'b0;
            bs <= B_IDLE; sdj <= 1'b0;          // a job on its way is left to finish: its answer would otherwise meet the next job
        end
    end

    // ================================================================ the ui side
    localparam [1:0] U_IDLE = 2'd0, U_ISSUE = 2'd1, U_WAITR = 2'd2;
    reg [1:0]  us = U_IDLE;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg [26:0] u_addr = 27'd0;
    reg        u_wr = 1'b0;
    reg [127:0] u_data = 128'd0;
    reg [15:0] u_mask = 16'hFFFF;
    reg        c_en_i = 1'b0, c_wr_i = 1'b0;
    always @(posedge ui_clk) begin
        rq_s <= {rq_s[1:0], rq_tgl};
        if (ui_rst) begin
            us <= U_IDLE; c_en_i <= 1'b0; c_wr_i <= 1'b0;
        end else begin
            case (us)
            U_IDLE: if (rq_s[2] != rq_s[1]) begin
                u_addr <= rq_addr; u_wr <= rq_wr; u_data <= rq_data; u_mask <= rq_mask; us <= U_ISSUE;
            end
            U_ISSUE: begin
                c_en_i <= 1'b1;
                c_addr <= u_addr;
                if (u_wr) begin
                    c_cmd <= 3'b000; c_wr_i <= 1'b1; c_wdf_data <= u_data; c_wdf_mask <= u_mask;
                end else begin
                    c_cmd <= 3'b001; c_wr_i <= 1'b0; c_wdf_mask <= 16'hFFFF;
                end
                if (c_en_i && c_rdy) begin
                    c_en_i <= 1'b0; c_wr_i <= 1'b0;
                    if (u_wr) begin dn_tgl <= ~dn_tgl; us <= U_IDLE; end else us <= U_WAITR;
                end
            end
            U_WAITR: if (c_rd_valid) begin ui_line <= c_rd_data; dn_tgl <= ~dn_tgl; us <= U_IDLE; end
            default: us <= U_IDLE;
            endcase
        end
    end
    assign c_en = c_en_i;
    assign c_wdf_wren = c_wr_i;
endmodule
