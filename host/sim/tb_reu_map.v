// T60: the REU's 16 MB through reu_ddr_map, ddr2_port (the REU/JTAG port of top.v) and mig_arb against the MIG
// model: iverilog -g2012 -o tb_reu_map.vvp tb_reu_map.v ../reu_ddr_map.v ../ddr2_port.v mig_ui_model.v
// c64_top hands the REU's byte address b as the 16-bit word address {1'b0, b[23:1]} with the byte lane in
// wdata_we; every command mig_arb gives the controller must be at (0x800000 + b/2)/8 * 8 (128-bit words, in
// 16-bit units), from the lowest to the highest byte of the 16 MB, none outside 0x800000-0xFFFFF8 (byte 0x1000000-0x1FFFFFF, Gideon's REU address).
// The model's memory is 64 K words and wraps, so the test addresses differ in the low 16 bits of their word.
`timescale 1ns/1ps
module tb;
    reg ui_clk = 0, sys_clk = 0;
    always #6.667 ui_clk = ~ui_clk;
    always #15.86 sys_clk = ~sys_clk;

    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(65536), .STALL_PCT(20), .CALIB(20)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst),
        .calib_done(calib), .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    // c64_top's REU port for a byte address, and top.v's mapping
    reg  [23:0] b_addr = 0;
    wire [23:0] cmd_addr = {1'b0, b_addr[23:1]};
    wire [1:0]  lanes    = b_addr[0] ? 2'b10 : 2'b01;
    wire [25:0] ddr_addr;
    wire [3:0]  ddr_be;
    reu_ddr_map map (.cmd_addr(cmd_addr), .wdata_we(lanes), .ddr_addr(ddr_addr), .ddr_be(ddr_be));

    reg preq = 0, pwe = 0; reg [31:0] pwdata; reg [25:0] paddr; reg [3:0] pbe;
    wire pack; wire [31:0] prdata; wire port_tgl, port_done_tgl; wire [31:0] port_done_rdata;
    ddr2_port port (.clk(sys_clk), .req(preq), .addr(paddr), .we(pwe), .wdata(pwdata), .be(pbe),
        .ack(pack), .rdata(prdata), .req_tgl(port_tgl), .done_tgl(port_done_tgl), .done_rdata(port_done_rdata));
    wire [127:0] dline; wire [31:0] idle_rd; wire idle_d0, idle_d2;
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl({1'b0, port_tgl, 1'b0}), .addr({26'd0, paddr, 26'd0}), .we({1'b0, pwe, 1'b0}),
        .wdata({32'd0, pwdata, 32'd0}), .be({4'd0, pbe, 4'd0}),
        .done_tgl({idle_d2, port_done_tgl, idle_d0}), .rdata({port_done_rdata, idle_rd}), .rdata_line(dline),
        .c_addr(27'd0), .c_cmd(3'd1), .c_en(1'b0), .c_rdy(), .c_wdf_data(128'd0), .c_wdf_mask(16'd0),
        .c_wdf_wren(1'b0), .c_wdf_rdy(), .c_rd_data(), .c_rd_data_valid(),
        .v_addr(27'd0), .v_cmd(3'b001), .v_en(1'b0), .v_rdy(), .v_wdf_data(128'd0), .v_wdf_mask(16'd0),
        .v_wdf_wren(1'b0), .v_wdf_rdy(), .v_rd_data_valid(), .v_urgent(1'b0),
        .e_addr(27'd0), .e_cmd(3'd0), .e_en(1'b0), .e_rdy(), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    // what the controller was asked for
    reg [26:0] lo = 27'h7FFFFFF, hi = 27'h0, last_a;
    reg [2:0]  last_c;
    integer n_cmd = 0;
    always @(posedge ui_clk) if (app_en && app_rdy) begin
        last_a <= app_addr; last_c <= app_cmd; n_cmd <= n_cmd + 1;
        if (app_addr < lo) lo <= app_addr;
        if (app_addr > hi) hi <= app_addr;
    end

    // top.v's access: write the byte into both halves of the word, the lane in be; read: pick the half
    task access(input w, input [23:0] b, input [7:0] d, output [7:0] q);
        reg [15:0] h;
        begin
            b_addr = b;
            #1;
            @(posedge sys_clk); preq <= 1; pwe <= w; paddr <= ddr_addr; pbe <= ddr_be; pwdata <= {4{d}};
            @(posedge sys_clk); while (!pack) @(posedge sys_clk);
            preq <= 0;
            h = ddr_addr[0] ? prdata[31:16] : prdata[15:0];
            q = b[0] ? h[15:8] : h[7:0];
        end
    endtask

    integer errors = 0, i;
    task check(input [31:0] got, input [31:0] want, input [8*28-1:0] what);
        begin
            if (got !== want) begin errors = errors + 1; $display("FAIL %0s: %0h, want %0h", what, got, want); end
            else $display("ok   %0s: %0h", what, got);
        end
    endtask

    reg [23:0] list [0:8];
    reg [7:0] q, v;
    reg [26:0] want_a;
    initial begin
        list[0] = 24'h000000; list[1] = 24'h000001; list[2] = 24'h00F3A0; list[3] = 24'h123456;
        list[4] = 24'h5A5A5A; list[5] = 24'h7FEFFE; list[6] = 24'h80F000; list[7] = 24'hFFFFFE;
        list[8] = 24'hFFFFFF;
        while (!calib) @(posedge ui_clk);
        for (i = 0; i < 9; i = i + 1) begin
            v = 8'h10 + i * 8'h11;
            access(1, list[i], v, q);
            want_a = ((27'h0800000 + {4'd0, list[i][23:1]}) >> 3) << 3;
            check(last_a, want_a, "write command address");
            check(last_c, 3'd0, "write command");
        end
        for (i = 0; i < 9; i = i + 1) begin
            v = 8'h10 + i * 8'h11;
            access(0, list[i], 8'h00, q);
            want_a = ((27'h0800000 + {4'd0, list[i][23:1]}) >> 3) << 3;
            check(last_a, want_a, "read command address");
            check(q, v, "byte read back");
        end
        check(lo, 27'h0800000, "lowest command address");
        check(hi, 27'h0FFFFF8, "highest command address");
        check(hi >> 3, 27'h1FFFFF, "top 128-bit word (byte 1FFFFFF)");
        if (errors) $display("FAILED"); else $display("PASS");
        $finish;
    end
endmodule
