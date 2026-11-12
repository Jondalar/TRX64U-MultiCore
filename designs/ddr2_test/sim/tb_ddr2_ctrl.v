// ddr2_ctrl (host/ddr2_ctrl.v: ddr2_lean + cal_seq, the block c64_basic instantiates in
// place of the MIG) against the DDR2 model. Two runs, chosen by NODRAM:
//   0  a board: the calibration ends ok by itself, init_calib_complete rises, cal_res says what
//      the calibration found (the word the app and trxdbg read), app_rdy opens, then random
//      words written and read back through the app_* port;
//   1  no DRAM on the pins: the calibration ends with cal_done = 1 and cal_ok = 0, init_calib_complete
//      and app_rdy stay low (what c64_basic does on a dead DDR2: the boot ROM waits for ever, the
//      result word shows it).
//   iverilog -g2012 -Ptb.NODRAM=1 -o tb.vvp tb_ddr2_ctrl.v ../../../host/cal_seq.v ../../../host/ddr2_lean.v \
//     ../../../host/ddr2_ctrl.v xilinx_models.v ddr2_model.v && vvp tb.vvp
`timescale 1ps/1ps
module tb #(
    parameter integer WR_DLY = 700,
    parameter integer RD_DLY = 900,
    parameter integer NODRAM = 0,
    parameter integer NWORDS = 600
);
    reg clk200 = 0;
    always #2500 clk200 = ~clk200;
    reg sys_rst = 1'b1;
    initial #100000 sys_rst = 1'b0;

    wire [15:0] dq; wire [1:0] dqs_p, dqs_n; wire [12:0] ddr_a; wire [2:0] ddr_ba;
    wire ras_n, cas_n, we_n; wire [0:0] ck_p, ck_n, cke, cs_n, odt; wire [1:0] dm;
    wire ui_clk, ui_rst, calib, cal_done, cal_ok;
    wire [63:0] cal_res;
    reg  [26:0] app_addr = 0; reg [2:0] app_cmd = 0; reg app_en = 0;
    wire app_rdy, app_wdf_rdy, app_rd_valid;
    reg  [127:0] wdata = 0; reg [15:0] wmask = 0; reg wren = 0;
    wire [127:0] rdata;

    ddr2_ctrl dut (
        .ddr2_dq(dq), .ddr2_dqs_n(dqs_n), .ddr2_dqs_p(dqs_p), .ddr2_addr(ddr_a), .ddr2_ba(ddr_ba),
        .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n), .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n),
        .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_dm(dm), .ddr2_odt(odt),
        .sys_clk_i(clk200), .sys_rst(sys_rst),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(wdata), .app_wdf_mask(wmask), .app_wdf_wren(wren), .app_wdf_rdy(app_wdf_rdy),
        .app_rd_data(rdata), .app_rd_data_valid(app_rd_valid),
        .ui_clk(ui_clk), .ui_clk_sync_rst(ui_rst), .init_calib_complete(calib),
        .cal_done(cal_done), .cal_ok(cal_ok), .cal_res(cal_res));
    generate if (!NODRAM) begin : g
        ddr2_board #(.WR_DLY(WR_DLY), .RD_DLY(RD_DLY)) board (
            .ddr2_addr(ddr_a), .ddr2_ba(ddr_ba), .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n),
            .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n), .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_odt(odt),
            .ddr2_dm(dm), .ddr2_dq(dq), .ddr2_dqs_p(dqs_p), .ddr2_dqs_n(dqs_n),
            .fpga_oe_dq(dut.u_lean.phy.dq[0].tq === 1'b0), .fpga_oe_dqs(dut.u_lean.phy.dqs[0].tq === 1'b0));
    end endgenerate

    function [127:0] pat(input [31:0] i);
        reg [31:0] y;
        begin
            y = i * 32'h9E3779B1; y = y ^ (y >> 15);
            pat = {y * 32'h85EBCA6B, y ^ 32'hA5A5A5A5, y + 32'h1234567, ~y};
        end
    endfunction

    integer errors = 0, i, got = 0, t;
    reg cmd_taken, dat_taken;
    reg [127:0] rq [0:1023];
    integer rn = 0, rd_i = 0;
    always @(posedge ui_clk) if (app_rd_valid) begin
        if (rdata !== pat(rd_i * 7 + 3)) begin
            errors = errors + 1;
            if (errors < 5) $display("MISMATCH read %0d: got %h exp %h", rd_i, rdata, pat(rd_i * 7 + 3));
        end
        rd_i = rd_i + 1;
    end

    initial begin #20000000000; $display("FAILED: timeout"); $finish; end   // 20 ms
    initial begin
        t = 0;
        while (!cal_done && t < 30000) begin #100000; t = t + 1; end      // 3 ms of simulated time at most
        if (!cal_done) begin $display("FAIL: calibration did not end in %0d us", t / 10); $finish; end
        $display("CAL_RES %08x %08x  done %b ok %b calib %b at %0t", cal_res[31:0], cal_res[63:32], cal_done, cal_ok, calib, $time);
        if (NODRAM) begin
            if (cal_ok || calib || app_rdy || cal_res[30] || cal_res[31] || !cal_res[29] || !cal_res[28])
                begin $display("FAILED: dead DDR2 must give done=1 ok=0 calib=0 rdy=0"); errors = errors + 1; end
            #20000000;
            if (calib || app_rdy) begin $display("FAILED: calib or app_rdy rose"); errors = errors + 1; end
            if (errors) $display("FAILED %0d", errors);
            else $display("PASS ddr2_ctrl: no DRAM -> cal_done 1, cal_ok 0, init_calib_complete 0, app_rdy 0, result word %08x", cal_res[31:0]);
            $finish;
        end
        if (!cal_ok || !calib) begin $display("FAILED: calibration not ok"); $finish; end
        if (cal_res[31:28] !== 4'b1111) begin $display("FAILED: flags %b", cal_res[31:28]); errors = errors + 1; end
        if (cal_res[5:0] < 6'd10 || cal_res[13:8] < 6'd10) begin $display("FAILED: window widths %0d %0d", cal_res[5:0], cal_res[13:8]); errors = errors + 1; end
        // write NWORDS words: command and data are accepted independently (MIG's rule), each held until taken
        @(posedge ui_clk);
        for (i = 0; i < NWORDS; i = i + 1) begin
            app_addr <= (i * 7 + 3) * 8; app_cmd <= 3'd0; wdata <= pat(i * 7 + 3); wmask <= 16'd0;
            app_en <= 1'b1; wren <= 1'b1;
            cmd_taken = 0; dat_taken = 0;
            while (!(cmd_taken && dat_taken)) begin
                @(posedge ui_clk);                       // values here are those before the edge
                if (app_en && app_rdy) cmd_taken = 1;
                if (wren && app_wdf_rdy) dat_taken = 1;
                if (cmd_taken) app_en <= 1'b0;
                if (dat_taken) wren <= 1'b0;
            end
        end
        // the reads of the same words; the data is checked in the always block above
        for (i = 0; i < NWORDS; i = i + 1) begin
            app_addr <= (i * 7 + 3) * 8; app_cmd <= 3'd1; app_en <= 1'b1;
            cmd_taken = 0;
            while (!cmd_taken) begin
                @(posedge ui_clk);
                if (app_en && app_rdy) cmd_taken = 1;
            end
            app_en <= 1'b0;
        end
        app_en <= 1'b0;
        repeat (200) @(posedge ui_clk);
        if (rd_i != NWORDS) begin $display("FAILED: %0d of %0d reads came back", rd_i, NWORDS); errors = errors + 1; end
        if (errors) $display("FAILED %0d", errors);
        else $display("PASS ddr2_ctrl: calibration ok (res %08x %08x), %0d words written and read back, 0 mismatches", cal_res[31:0], cal_res[63:32], NWORDS);
        $finish;
    end
endmodule
