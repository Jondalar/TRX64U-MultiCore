// drive_mem (its line cache and snooping) on mig_arb and the MIG model, with
// the app's stores coming through cpu_mem_bridge as on the board. run.sh.
`timescale 1ns/1ps
module tb;
    reg clk = 0, ui_clk = 0;
    always #5 clk = ~clk;
    always #6.667 ui_clk = ~ui_clk;
    localparam [25:0] BASE = 26'h0EE0000;

    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.CALIB(20)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    // the drive's side
    reg d_req = 0, d_rwn = 1; reg [7:0] d_tag = 8'h01; reg [25:0] d_addr; reg [7:0] d_wdata;
    wire [7:0] d_rdata, d_rack_tag, d_dack_tag; wire d_rack;
    // the system bus (the app's stores)
    reg s_req = 0; reg [25:0] s_addr; reg [3:0] s_be; reg [31:0] s_wdata;
    wire [31:0] s_rdata; wire s_rack; wire [7:0] s_rack_tag, s_dack_tag;
    wire dp_tgl, dp_we, dp_done; wire [25:0] dp_addr; wire [31:0] dp_wdata; wire [3:0] dp_be;
    wire [127:0] dp_rdata;
    drive_mem_flat dm (.clock(clk), .reset(1'b0),
        .d_request(d_req), .d_tag(d_tag), .d_rwn(d_rwn), .d_address(d_addr), .d_wdata(d_wdata),
        .d_rdata(d_rdata), .d_rack(d_rack), .d_rack_tag(d_rack_tag), .d_dack_tag(d_dack_tag),
        .dp_req_tgl(dp_tgl), .dp_addr(dp_addr), .dp_we(dp_we), .dp_wdata(dp_wdata), .dp_be(dp_be),
        .dp_done_tgl(dp_done), .dp_rdata(dp_rdata),
        .s_request(s_req), .s_tag(8'h21), .s_rwn(1'b0), .s_address(s_addr), .s_byte_en(s_be),
        .s_wdata(s_wdata), .s_rack(s_rack), .s_rack_tag(s_rack_tag), .s_dack_tag(s_dack_tag));

    wire unused1; wire [31:0] unused_r;
    wire cpu_tgl, cpu_done, cpu_we; wire [25:0] cpu_addr; wire [31:0] cpu_wdata, cpu_rdata; wire [3:0] cpu_be;
    cpu_mem_bridge br (.clk(clk), .rst(1'b0), .request(s_req), .tag(8'h21), .read_writen(1'b0),
        .address(s_addr), .byte_en(s_be), .wdata(s_wdata), .rdata(s_rdata), .rack(s_rack),
        .rack_tag(s_rack_tag), .dack_tag(s_dack_tag), .req_tgl(cpu_tgl), .a_addr(cpu_addr),
        .a_we(cpu_we), .a_wdata(cpu_wdata), .a_be(cpu_be), .done_tgl(cpu_done), .done_rdata(cpu_rdata));

    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl({dp_tgl, 1'b0, cpu_tgl}), .addr({dp_addr, 26'd0, cpu_addr}), .we({dp_we, 1'b0, cpu_we}),
        .wdata({dp_wdata, 32'd0, cpu_wdata}), .be({dp_be, 4'd0, cpu_be}),
        .done_tgl({dp_done, unused1, cpu_done}), .rdata({unused_r, cpu_rdata}), .rdata_line(dp_rdata),
        .c_addr(27'd0), .c_cmd(3'd0), .c_en(1'b0), .c_rdy(), .c_wdf_data(128'd0), .c_wdf_mask(16'd0),
        .c_wdf_wren(1'b0), .c_wdf_rdy(), .c_rd_data(), .c_rd_data_valid(),
        .v_addr(27'd0), .v_cmd(3'b001), .v_en(1'b0), .v_rdy(), .v_wdf_data(128'd0), .v_wdf_mask(16'd0),
        .v_wdf_wren(1'b0), .v_wdf_rdy(), .v_rd_data_valid(), .v_urgent(1'b0),
        .e_addr(27'd0), .e_cmd(3'd0), .e_en(1'b0), .e_rdy(), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    function [7:0] ddr(input [25:0] a);
        ddr = mig.mem[a[25:4]][8 * a[3:0] +: 8];
    endfunction

    integer errors = 0, lat;
    reg [7:0] v;
    task check(input [7:0] got, input [7:0] want, input [8*40-1:0] what);
        if (got !== want) begin errors = errors + 1; $display("FAIL %0s: %02x, want %02x", what, got, want); end
        else $display("ok   %0s: %02x (%0d ns)", what, got, lat * 10);
    endtask

    // one drive access (tag, read/write, byte address in the image); lat in clocks
    task drv(input [7:0] tag, input rwn, input [15:0] a, input [7:0] wd);
        begin
            @(posedge clk); d_req <= 1; d_tag <= tag; d_rwn <= rwn; d_addr <= BASE + a; d_wdata <= wd; lat = 0;
            @(posedge clk); while (!(d_rack && d_rack_tag == tag)) begin @(posedge clk); lat = lat + 1; end
            d_req <= 0;
            if (rwn) while (d_dack_tag != tag) begin @(posedge clk); lat = lat + 1; end
            v = d_rdata;
            lat = lat + 1;
        end
    endtask
    task sys_write(input [15:0] a, input [3:0] be, input [31:0] d);
        begin
            @(posedge clk); s_req <= 1; s_addr <= BASE + {a[15:2], 2'b00}; s_be <= be; s_wdata <= d;
            @(posedge clk); while (!(s_rack && s_rack_tag == 8'h21)) @(posedge clk);
            s_req <= 0;
        end
    endtask

    integer i;
    initial begin
        for (i = 0; i < 4096; i = i + 1) mig.mem[(BASE >> 4) + i] = {16{8'h00}} | i * 128'h0101_0101_0101_0101_0101_0101_0101_0101;
        mig.mem[(BASE + 26'h70) >> 4] = 128'h7F7E7D7C_7B7A7978_77767574_73727170;
        while (!calib) @(posedge ui_clk);
        repeat (10) @(posedge clk);

        drv(8'h01, 1, 16'h0075, 0); check(v, 8'h75, "miss reads the line");
        drv(8'h01, 1, 16'h0077, 0); check(v, 8'h77, "hit");
        if (lat > 3) begin errors = errors + 1; $display("FAIL hit took %0d clocks", lat); end
        drv(8'h01, 0, 16'h0072, 8'hA2); check(v, v, "write");
        drv(8'h01, 1, 16'h0072, 0); check(v, 8'hA2, "write updates the line");
        repeat (100) @(posedge clk);
        check(ddr(BASE + 16'h0072), 8'hA2, "write reaches DDR2");

        // the app sets $77/$78 (set_sw_address): the cached line must go
        sys_write(16'h0078, 4'h1, 32'h00000049);
        sys_write(16'h0074, 4'h8, 32'h29000000);
        drv(8'h01, 1, 16'h0077, 0); check(v, 8'h29, "after the app's store (miss)");
        drv(8'h01, 1, 16'h0078, 0); check(v, 8'h49, "other byte, refetched after the store");

        // a store while the line is on its way: the line is not kept stale
        fork
            drv(8'h01, 1, 16'h0105, 0);
            begin repeat (3) @(posedge clk); sys_write(16'h0104, 4'h1, 32'h000000C4); end
        join
        $display("     read during the store: %02x", v);
        repeat (300) @(posedge clk);
        drv(8'h01, 1, 16'h0104, 0); check(v, 8'hC4, "line fetched across a store");

        // the store right before the drive's read, both at once in mig_arb
        fork
            sys_write(16'h0204, 4'h1, 32'h000000D4);
            begin @(posedge clk); @(posedge clk); drv(8'h01, 1, 16'h0204, 0); end
        join
        $display("     read next to the store: %02x", v);
        repeat (300) @(posedge clk);
        drv(8'h01, 1, 16'h0204, 0); check(v, 8'hD4, "store next to a miss");

        // other tags go straight to DDR2, also in the image
        drv(8'h02, 1, 16'h0305, 0); check(v, 8'h30, "floppy read");
        drv(8'h01, 1, 16'h0305, 0);
        drv(8'h04, 0, 16'h0305, 8'h5A);
        repeat (100) @(posedge clk);
        check(ddr(BASE + 16'h0305), 8'h5A, "sound-tag write reaches DDR2");
        drv(8'h01, 1, 16'h0305, 0); check(v, 8'h5A, "and the cached line");

        if (errors) $display("FAILED, %0d errors", errors); else $display("PASS");
        $finish;
    end
endmodule
