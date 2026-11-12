// T119 5.15: the post-synthesis (write_verilog -mode funcsim) netlist of zx_core, driven through its ports only: no hierarchical reference
// into the core. The app's switch sequence as tb_zx coldapp48 / swapp48 makes it (registers and ROM window), then the JTAG words 10-13
// (HBlank rises, HSync, line length, lines per frame) are read. +MODE=cold48 | sw48 | cold128; +ROM=<hex> (+ROM2=<hex> for sw48); +N=<sys_clk cycles per window>.
`timescale 1ns / 1ps
module tb_fsim;
    reg sys_clk = 1'b0, cpu_clk = 1'b0, mclk = 1'b0, ui_clk = 1'b0;
    always #15.86 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #40.69 mclk = ~mclk;
    always #6.667 ui_clk = ~ui_clk;
    reg sys_rst = 1'b1, ui_rst = 1'b1, reset_req = 1'b0;
    reg [11:0] dbg_a = 12'd0;
    wire [31:0] j_rd;
    reg dma_req_tgl = 1'b0, dma_we = 1'b0, dma_rom = 1'b0;
    reg [15:0] dma_addr = 16'd0;
    reg [7:0] dma_wdata = 8'd0;
    wire dma_done_tgl;
    wire [7:0] dma_rdata;
    reg cw_we = 1'b0, cw_re = 1'b0;
    reg [16:0] cw_addr = 17'd0;
    reg [7:0] cw_wdata = 8'd0;
    wire [7:0] cw_rdata;
    wire [26:0] c_addr, e_addr;
    wire [2:0] c_cmd, e_cmd;
    wire c_en, c_rdy, c_rd_valid, e_en, e_wdf_wren;
    wire [127:0] c_rd_data, e_wdf_data;
    wire [15:0] e_wdf_mask;
    wire [3:0] pix_color; wire [9:0] pix_x; wire [8:0] pix_y;
    wire signed [23:0] audio_l, audio_r;

    zx_core dut (
        .sys_clk(sys_clk), .sys_rst(sys_rst), .cpu_clk(cpu_clk), .mclk(mclk), .ui_clk(ui_clk), .ui_rst(ui_rst),
        .reset_req(reset_req), .halt_req(1'b0), .halted(), .running(),
        .keys(64'd0), .joy1(5'd0), .joy2(5'd0),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(1'b0),
        .win_port_use(), .win_port_addr(), .win_port_rom(), .win_port_we(), .win_port_wdata(),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .audio_l(audio_l), .audio_r(audio_r),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(), .cw_irq(),
        .px_we(), .px_re(), .px_rdata(8'd0), .px_ev_vsync(1'b0), .px_ev_flip(1'b0),
        .build(32'h12345678), .ddr_cal(64'd0), .dbg_re(1'b0), .dbg_addr(dbg_a), .j_rdata(j_rd), .j_rvalid(),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(1'b1), .e_wdf_data(e_wdf_data), .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren)
    );
    ddr2c_model ddr (.ui_clk(ui_clk), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_rd_data(c_rd_data), .c_rd_valid(c_rd_valid));

    task cw_wr(input [11:0] a, input [7:0] d);
        begin
            @(posedge cpu_clk); cw_addr <= {5'd0, a}; cw_wdata <= d; cw_we <= 1'b1;
            @(posedge cpu_clk); cw_we <= 1'b0;
            @(posedge cpu_clk);
        end
    endtask
    task win_wr(input [15:0] a, input [7:0] d);
        reg t0;
        begin
            cw_wr(12'h0D4, {5'd0, a[15:14], 1'b0});
            t0 = dma_done_tgl;
            @(posedge cpu_clk); dma_rom <= 1'b1; dma_we <= 1'b1; dma_addr <= {2'b01, a[13:0]}; dma_wdata <= d; dma_req_tgl <= ~dma_req_tgl;
            while (dma_done_tgl == t0) @(posedge cpu_clk);
        end
    endtask

    reg [8*200-1:0] mode, romfile, rom2file;
    reg [7:0] img [0:32767];
    integer i, n;
    // the app's model switch: reset hold up, model, ROM image through the window, hold down
    task load(input model128, input [8*200-1:0] f);
        begin
            for (i = 0; i < 32768; i = i + 1) img[i] = 8'd0;
            $readmemh(f, img);
            reset_req = 1'b1;
            cw_wr(12'h0A0, 8'h01);
            cw_wr(12'h0A1, {7'd0, model128});
            for (i = 0; i < 512; i = i + 1) win_wr(i[15:0], img[i]);
            cw_wr(12'h0D4, 8'h00);
            cw_wr(12'h0A0, 8'h00);
            reset_req = 1'b0;
        end
    endtask
    reg [31:0] w[0:15];
    task jtag(input integer k);
        begin dbg_a = k[11:0]; repeat (3) @(posedge sys_clk); w[k] = j_rd; end
    endtask
    task report(input [8*8-1:0] tag);
        integer k;
        begin
            for (k = 1; k <= 13; k = k + 1) jtag(k);
            $display("%0s: w1 status %02x frames %04x | w5 d_ls %04x | w10 d_hbr %0d d_hbh %0d | w11 d_ce %0d line_starts %0d | w12 HSync-period %0d rises %0d | w13 HSync-high %0d lines/frame %0d",
                     tag, w[1][7:0], w[2][15:0], w[5][15:0], w[10][31:16], w[10][15:0], w[11][31:16], w[11][15:0], w[12][31:16], w[12][15:0], w[13][31:16], w[13][15:0]);
        end
    endtask
    reg [31:0] h0;
    initial begin
        if (!$value$plusargs("MODE=%s", mode)) mode = "cold48";
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "";
        if (!$value$plusargs("ROM2=%s", rom2file)) rom2file = "";
        if (!$value$plusargs("N=%d", n)) n = 7000000;
        repeat (20) @(posedge cpu_clk);
        sys_rst = 1'b0; ui_rst = 1'b0;
        repeat (100) @(posedge cpu_clk);
        if (mode == "sw48") begin
            load(1'b1, romfile);
            repeat (n) @(posedge sys_clk);
            report("128K");
            load(1'b0, rom2file);
        end else if (mode == "cold128") load(1'b1, romfile);
        else load(1'b0, romfile);
        repeat (4000) @(posedge sys_clk);
        jtag(10); h0 = w[10];
        repeat (n) @(posedge sys_clk);
        report(mode);
        $display("%0s: HBlank rises in the last window: %0d", mode, (w[10][31:16] - h0[31:16]) & 16'hFFFF);
        if (((w[10][31:16] - h0[31:16]) & 16'hFFFF) > 100 && w[12][31:16] == (mode == "cold128" ? 16'd456 : 16'd448)) $display("PASS %0s: HBlank rises in the netlist", mode);
        else $display("FAIL %0s: HBlank dead in the netlist", mode);
        $finish;
    end
endmodule
