// ddr2_lean against the DDR2 model: init and calibration through cal_seq (the same RTL as the
// ddr2_test design), then random read/write/mask traffic checked against a reference memory.
//
//   iverilog -g2012 -PWR_DLY=500 -PRD_DLY=500 -PNCMD=100000 -o tb.vvp tb_ddr2_lean.v \
//     ../cal_seq.v ../../../host/ddr2_lean.v xilinx_models.v ddr2_model.v && vvp tb.vvp
//
// WR_DLY / RD_DLY: board flight times (ps) FPGA -> DRAM and back; NCMD: commands of the traffic;
// IDLE: maximum idle clocks between commands (0 = back to back); REFI: refresh interval written to
// the controller (cycles), 0 = leave the default.
`timescale 1ps/1ps
module tb #(
    parameter integer WR_DLY = 500,
    parameter integer RD_DLY = 500,
    parameter integer NCMD   = 100000,
    parameter integer IDLE   = 2,
    parameter integer REFI   = 0,
    parameter integer SEED   = 1,
    parameter integer DQSCK  = 200,
    parameter integer SKEW   = 3              // data/command offsets up to 3 clocks, now and then up to 12 when SKEW > 3
);
    reg clk200 = 1'b0;
    always #2500 clk200 = ~clk200;
    reg sys_rst = 1'b1;
    initial begin #100000 sys_rst = 1'b0; end

    wire [15:0] dq; wire [1:0] dqs_p, dqs_n;
    wire [12:0] addr; wire [2:0] ba; wire ras_n, cas_n, we_n;
    wire [0:0] ck_p, ck_n, cke, cs_n, odt; wire [1:0] dm;

    reg  [26:0]  app_addr = 0;
    reg  [2:0]   app_cmd = 0;
    reg          app_en = 0;
    wire         app_rdy;
    reg  [127:0] app_wdf_data = 0;
    reg  [15:0]  app_wdf_mask = 0;
    reg          app_wdf_wren = 0;
    wire         app_wdf_rdy;
    wire [127:0] app_rd_data;
    wire         app_rd_valid, ui_clk, ui_rst, calib;

    wire        r_we; wire [3:0] r_addr; wire [31:0] r_wdata, r_rdata;
    reg         m_we = 0; reg [3:0] m_addr = 0; reg [31:0] m_wdata = 0;
    wire        cs_init_done, cs_done, cs_ok;
    wire [4:0]  res_tap0, res_tap1; wire [2:0] res_dly; wire [3:0] res_bs; wire [5:0] res_w0, res_w1;
    wire        res_tr0, res_tr1; wire [3:0] att_hits;

    cal_seq cs (.clk(ui_clk), .rst(ui_rst), .r_we(r_we), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata),
        .init_done(cs_init_done), .cal_done(cs_done), .cal_ok(cs_ok), .res_tap0(res_tap0), .res_tap1(res_tap1),
        .res_dly(res_dly), .res_bs(res_bs), .res_w0(res_w0), .res_w1(res_w1), .res_tr0(res_tr0), .res_tr1(res_tr1),
        .att_hits(att_hits));
    wire        b_we    = cs_done ? m_we : r_we;
    wire [3:0]  b_addr  = cs_done ? m_addr : r_addr;
    wire [31:0] b_wdata = cs_done ? m_wdata : r_wdata;

    ddr2_lean dut (
        .ddr2_dq(dq), .ddr2_dqs_n(dqs_n), .ddr2_dqs_p(dqs_p), .ddr2_addr(addr), .ddr2_ba(ba),
        .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n), .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n),
        .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_dm(dm), .ddr2_odt(odt),
        .sys_clk_i(clk200), .sys_rst(sys_rst),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_end(app_wdf_wren), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy),
        .app_rd_data(app_rd_data), .app_rd_data_end(), .app_rd_data_valid(app_rd_valid),
        .ui_clk(ui_clk), .ui_clk_sync_rst(ui_rst), .init_calib_complete(calib),
        .r_we(b_we), .r_addr(b_addr), .r_wdata(b_wdata), .r_rdata(r_rdata)
    );
    ddr2_board #(.WR_DLY(WR_DLY), .RD_DLY(RD_DLY), .DQSCK(DQSCK)) board (
        .ddr2_addr(addr), .ddr2_ba(ba), .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n),
        .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n), .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_odt(odt),
        .ddr2_dm(dm), .ddr2_dq(dq), .ddr2_dqs_p(dqs_p), .ddr2_dqs_n(dqs_n),
        .fpga_oe_dq(dut.phy.dq[0].tq === 1'b0), .fpga_oe_dqs(dut.phy.dqs[0].tq === 1'b0));

    // ---- reference memory and client ----
    localparam integer WIN = 1 << 16;                 // bursts
    reg [127:0] refm [0:WIN-1];
    reg         refv [0:WIN-1];
    integer i;
    initial for (i = 0; i < WIN; i = i + 1) refv[i] = 1'b0;

    reg [127:0] exq [0:63];
    reg [63:0]  exa [0:63];
    integer q_w = 0, q_r = 0;
    integer mism = 0, reads = 0, writes = 0, issued = 0;
    reg [31:0] rng = SEED * 32'h9E3779B1 + 1;
    function [31:0] rnd(input dummy);
        begin
            rng = rng ^ (rng << 13); rng = rng ^ (rng >> 17); rng = rng ^ (rng << 5);
            rnd = rng;
        end
    endfunction
    function [127:0] rnd128(input dummy);
        rnd128 = {rnd(0), rnd(0), rnd(0), rnd(0)};
    endfunction

    // reads come back in order
    always @(posedge ui_clk) if (app_rd_valid) begin
        if (q_r == q_w) begin $display("TB: read data with nothing outstanding"); mism = mism + 1; end
        else begin
            if (exq[q_r % 64] !== app_rd_data) begin
                mism = mism + 1;
                if (mism < 10) $display("TB: MISMATCH burst %0h: got %h expected %h", exa[q_r % 64], app_rd_data, exq[q_r % 64]);
            end
            q_r = q_r + 1;
            reads = reads + 1;
        end
    end

    task reg_write(input [3:0] a, input [31:0] d);
        begin
            @(posedge ui_clk); m_we <= 1; m_addr <= a; m_wdata <= d;
            @(posedge ui_clk); m_we <= 0;
        end
    endtask

    // one command: the data of a write may come a few clocks before or after the command
    task cmd(input is_wr, input [22:0] burst, input [127:0] data, input [15:0] mask);
        integer cd, dd, t;
        reg c_ok, d_ok;
        reg [127:0] m;
        integer k;
        begin
            cd = (rnd(0) % 4 == 0) ? rnd(0) % 4 : 0;
            dd = is_wr ? ((rnd(0) % 4 == 0) ? rnd(0) % 4 : 0) : 0;
            if (SKEW > 3 && rnd(0) % 16 == 0) begin cd = rnd(0) % 13; dd = rnd(0) % 13; end
            c_ok = 0; d_ok = !is_wr;
            // start both at the next clock edge plus their own offsets
            @(posedge ui_clk);
            t = 0;
            while (!(c_ok && d_ok)) begin
                #1;
                if (!c_ok && t >= cd) begin app_en <= 1; app_cmd <= is_wr ? 3'b000 : 3'b001; app_addr <= {1'b0, burst, 3'b000}; end
                if (is_wr && !d_ok && t >= dd) begin app_wdf_wren <= 1; app_wdf_data <= data; app_wdf_mask <= mask; end
                @(posedge ui_clk);
                if (app_en && app_rdy) begin app_en <= 0; c_ok = 1; end
                if (app_wdf_wren && app_wdf_rdy) begin app_wdf_wren <= 0; d_ok = 1; end
                t = t + 1;
            end
            if (is_wr) begin
                m = refv[burst[15:0]] ? refm[burst[15:0]] : 128'd0;
                for (k = 0; k < 16; k = k + 1) if (!mask[k]) m[8*k +: 8] = data[8*k +: 8];
                refm[burst[15:0]] = m; refv[burst[15:0]] = 1;
                writes = writes + 1;
            end else begin
                exq[q_w % 64] = refm[burst[15:0]]; exa[q_w % 64] = burst; q_w = q_w + 1;
            end
            issued = issued + 1;
            // idle clocks
            if (IDLE > 0 && (rnd(0) % 3 == 0)) repeat (rnd(0) % (IDLE + 1)) @(posedge ui_clk);
        end
    endtask

    // addresses: mostly a working set of 2048 bursts (so reads find written data and the same
    // banks and rows come back), sequential runs, and now and then anywhere in the window
    function [22:0] pick_burst(input [31:0] r, input [22:0] last);
        begin
            case (r[2:0])
            3'd0, 3'd1, 3'd2: pick_burst = {12'd0, r[31:21]};                   // working set
            3'd3:             pick_burst = last + 23'd1;                          // sequential
            3'd4:             pick_burst = {last[22:3], 3'b000} + {20'd0, r[7:5]}; // the eight banks of one row
            3'd5:             pick_burst = {10'd0, r[31:19]} & 23'h7FFFF8;          // bank 0, many rows
            default:          pick_burst = {7'd0, r[31:16]};                       // anywhere
            endcase
            pick_burst[22:16] = 7'd0;
        end
    endfunction

    // outstanding reads are bounded: the bench has no queue of its own
    reg [22:0] last_b = 0;
    reg [22:0] b;
    reg [127:0] d;
    reg [15:0] mk;
    integer n, waitc, gap_max;
    reg started = 0;
    integer t_start;
    initial begin
        // init + calibration
        wait (cs_done);
        $display("CAL: ok=%0d dly=%0d bs=%0d tap0=%0d tap1=%0d width0=%0d width1=%0d trunc=%0d%0d hits=%0d wr_dly=%0d rd_dly=%0d",
                 cs_ok, res_dly, res_bs, res_tap0, res_tap1, res_w0, res_w1, res_tr0, res_tr1, att_hits, WR_DLY, RD_DLY);
        if (!cs_ok) begin $display("TB FAIL: calibration found no window"); $finish; end
        if (REFI != 0) begin reg_write(4'd11, REFI); board.dram.refs_live = 0; board.dram.max_ref_gap = 0; end
        repeat (20) @(posedge ui_clk);
        // fill the window's first part so reads have something defined, then random traffic
        t_start = $time;
        for (n = 0; n < NCMD; n = n + 1) begin
            d = rnd128(0);
            mk = (rnd(0) % 3 == 0) ? rnd(0) : 16'h0000;
            b = pick_burst(rnd(0), last_b);
            last_b = b;
            // reads of never-written bursts: write them first
            if (!refv[b[15:0]]) cmd(1, b, d, 16'h0000);
            else case (rnd(0) % 10)
            0, 1: cmd(1, b, d, mk);
            2:    cmd(1, b, d, 16'h0000);
            default: cmd(0, b, 128'd0, 16'd0);
            endcase
        end
        // drain
        waitc = 0;
        while (q_r != q_w && waitc < 2000) begin @(posedge ui_clk); waitc = waitc + 1; end
        repeat (100) @(posedge ui_clk);
        $display("TRAFFIC: %0d commands (%0d writes, %0d reads) in %0d ns, mismatches %0d, outstanding %0d",
                 issued, writes, reads, ($time - t_start) / 1000, mism, q_w - q_r);
        $display("DRAM: refreshes %0d, activates %0d, reads %0d, writes %0d, longest refresh gap %0d ns, errors %0d, contention %0d",
                 board.dram.n_ref, board.dram.n_act, board.dram.n_rd, board.dram.n_wr, board.dram.max_ref_gap / 1000,
                 board.dram.errors, board.contention);
        // refresh: never more than 7.8 us (the default interval 560 cycles = 7.47 us plus the wait for idle
        // banks) between two, or REFI cycles plus 0.6 us when it was set shorter
        gap_max = (REFI == 0) ? 7800 : (REFI * 13336 / 1000 + 600);
        if (board.dram.max_ref_gap / 1000 > gap_max) begin
            $display("FAIL ddr2_lean refresh: longest gap %0d ns > %0d ns", board.dram.max_ref_gap / 1000, gap_max);
            mism = mism + 1;
        end
        if (mism == 0 && board.dram.errors == 0 && board.contention == 0 && q_r == q_w)
            $display("PASS ddr2_lean traffic: wr_dly %0d rd_dly %0d, %0d commands, 0 mismatches, %0d refreshes, longest gap %0d ns, 0 DRAM errors",
                     WR_DLY, RD_DLY, issued, board.dram.n_ref, board.dram.max_ref_gap / 1000);
        else $display("FAIL ddr2_lean traffic");
        $finish;
    end
    initial begin
        #(64'd600000000 + 64'd400000 * NCMD + 64'd5000 * IDLE * NCMD);   // 0.6 ms init, 400 ns per command, the idle clocks
        $display("FAIL: timeout (calibration %0d, init %0d)", cs_done, cs_init_done);
        $finish;
    end
endmodule
