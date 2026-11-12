// Drive access latency to DDR2 through mig_arb and the MIG UI model, with the
// RISC-V and the REU loading the memory (run.sh).
//   OLD: the path before T11 (mig_arb with two clients, 3-flop syncs): the
//        drive on the system bus in front of cpu_mem_bridge, ahead of the CPU.
//   new: the drive on mig_arb client 2.
// The drive makes one access per 1 us (70 % reads, at 0x0EE0000 + random),
// measured from its request until the read data / write acceptance is back
// in its clock (drive_mem adds one clock on either side). +cpu: the RISC-V
// back to back (75 % reads, random in the bottom 64 MB); +reu: the REU back
// to back (16-bit, at 64 MB); +cart (new only): a cart_fill line fetch (four
// reads in a row from the ROM image) every 2 us, its latency from the first
// command to the fourth word; +vid (new only): the pixel layer's worst case,
// 120 reads back to back every 17.7 us (960 RGB565 pixels per output line
// at scale 1); -DVMAX=N sets its reads in flight (mig_arb V_READS).
`timescale 1ps/1ps
module tb;
    reg cpu_clk = 0, ui_clk = 0, sys_clk = 0;
    always #5000 cpu_clk = ~cpu_clk;
    always #6667 ui_clk = ~ui_clk;
    always #15859 sys_clk = ~sys_clk;

    integer use_cpu, use_reu, use_cart, use_vid, run_us;
    initial begin
        use_vid = $test$plusargs("vid");
        use_cpu = $test$plusargs("cpu");
        use_reu = $test$plusargs("reu");
        use_cart = $test$plusargs("cart");
        if (!$value$plusargs("us=%d", run_us)) run_us = 2000;
    end

    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(1 << 16)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    reg [31:0] rnd = 32'h12345678;
    always @(posedge cpu_clk) rnd <= {rnd[30:0], rnd[31] ^ rnd[21] ^ rnd[1] ^ rnd[0]};
    reg [31:0] rnd_s = 32'h0badcafe;
    always @(posedge sys_clk) rnd_s <= {rnd_s[30:0], rnd_s[31] ^ rnd_s[21] ^ rnd_s[1] ^ rnd_s[0]};

    // ---- the RISC-V on Gideon's bus (tag 0x21) --------------------------------
    reg        c_req = 0, c_rwn = 1; reg [25:0] c_addr; reg c_open = 0;
    // ---- the drive (tag 0x01) ------------------------------------------------------
    reg        d_req = 0, d_rwn = 1; reg [25:0] d_addr; reg d_open = 0;
    integer    d_timer = 0; time d_start;

    // the bus after the arbiter (OLD: drive first)
    wire       b_drv  = d_req;
    wire       request = b_drv ? d_req : c_req;
    wire [7:0] tag     = b_drv ? 8'h01 : 8'h21;
    wire       rwn     = b_drv ? d_rwn : c_rwn;
    wire [25:0] address = b_drv ? d_addr : c_addr;
    wire [31:0] rdata; wire rack; wire [7:0] rack_tag, dack_tag;

    wire cpu_tgl, cpu_done_tgl, cpu_we; wire [25:0] cpu_addr; wire [31:0] cpu_wdata, cpu_done_rdata;
    wire [3:0] cpu_be;
    reg  preq = 0, pwe = 0; reg [25:0] paddr; wire pack; wire [31:0] prdata;
    wire port_tgl, port_done_tgl; wire [31:0] port_done_rdata;
`ifdef OLD
    cpu_mem_bridge_old br (.clk(cpu_clk), .rst(1'b0), .request(request), .tag(tag), .read_writen(rwn),
        .address(address), .byte_en(4'hF), .wdata(32'h0), .rdata(rdata), .rack(rack), .rack_tag(rack_tag),
        .dack_tag(dack_tag), .req_tgl(cpu_tgl), .a_addr(cpu_addr), .a_we(cpu_we), .a_wdata(cpu_wdata),
        .a_be(cpu_be), .done_tgl(cpu_done_tgl), .done_rdata(cpu_done_rdata));
    ddr2_port_old port (.clk(sys_clk), .req(preq), .addr(paddr), .we(pwe), .wdata(32'h0), .be(4'h3),
        .ack(pack), .rdata(prdata), .req_tgl(port_tgl), .done_tgl(port_done_tgl), .done_rdata(port_done_rdata));
    mig_arb_old arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl({port_tgl, cpu_tgl}), .addr({paddr, cpu_addr}), .we({pwe, cpu_we}),
        .wdata({32'h0, cpu_wdata}), .be({4'h3, cpu_be}),
        .done_tgl({port_done_tgl, cpu_done_tgl}), .rdata({port_done_rdata, cpu_done_rdata}),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));
    wire d_done = (d_rwn ? dack_tag == 8'h01 : rack && rack_tag == 8'h01);
    wire d_racked = rack && rack_tag == 8'h01;
`else
    cpu_mem_bridge br (.clk(cpu_clk), .rst(1'b0), .request(c_req), .tag(8'h21), .read_writen(c_rwn),
        .address(c_addr), .byte_en(4'hF), .wdata(32'h0), .rdata(rdata), .rack(rack), .rack_tag(rack_tag),
        .dack_tag(dack_tag), .req_tgl(cpu_tgl), .a_addr(cpu_addr), .a_we(cpu_we), .a_wdata(cpu_wdata),
        .a_be(cpu_be), .done_tgl(cpu_done_tgl), .done_rdata(cpu_done_rdata));
    ddr2_port port (.clk(sys_clk), .req(preq), .addr(paddr), .we(pwe), .wdata(32'h0), .be(4'h3),
        .ack(pack), .rdata(prdata), .req_tgl(port_tgl), .done_tgl(port_done_tgl), .done_rdata(port_done_rdata));
    // the drive's port as drive_mem drives it; cart_fill
    reg [26:0] k_addr; reg k_en = 0; wire k_rdy, k_valid;
    reg [26:0] v_addr = 27'h0400000; reg v_en = 0; wire v_rdy, v_valid;
    reg dp_tgl = 0; reg [25:0] dp_addr; reg dp_we; reg [1:0] dp_done_s = 0;
    wire dp_done_tgl; wire [127:0] dp_rdata;
`ifndef VMAX
`define VMAX 8
`endif
    mig_arb #(.V_READS(`VMAX)) arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl({dp_tgl, port_tgl, cpu_tgl}), .addr({dp_addr, paddr, cpu_addr}), .we({dp_we, pwe, cpu_we}),
        .wdata({32'h0, 32'h0, cpu_wdata}), .be({4'h1, 4'h3, cpu_be}),
        .done_tgl({dp_done_tgl, port_done_tgl, cpu_done_tgl}), .rdata({port_done_rdata, cpu_done_rdata}),
        .rdata_line(dp_rdata),
        .c_addr(k_addr), .c_cmd(3'b001), .c_en(k_en), .c_rdy(k_rdy), .c_wdf_data(128'd0),
        .c_wdf_mask(16'hFFFF), .c_wdf_wren(1'b0), .c_wdf_rdy(), .c_rd_data(),
        .c_rd_data_valid(k_valid),
        .v_addr(v_addr), .v_cmd(3'b001), .v_en(v_en), .v_rdy(v_rdy), .v_wdf_data(128'd0), .v_wdf_mask(16'd0),
        .v_wdf_wren(1'b0), .v_wdf_rdy(), .v_rd_data_valid(v_valid), .v_urgent(1'b0),
        .e_addr(27'd0), .e_cmd(3'd0), .e_en(1'b0), .e_rdy(), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));
    always @(posedge cpu_clk) dp_done_s <= {dp_done_s[0], dp_done_tgl};
    // cart_fill: four reads every 2 us
    reg [7:0] k_timer = 0; reg [2:0] k_issued = 0;
    always @(posedge ui_clk) begin
        if (calib && use_cart) k_timer <= k_timer == 149 ? 0 : k_timer + 1;
        if (k_timer == 149 && !k_en) begin
            k_en <= 1; k_issued <= 0; k_addr <= 27'h1E00000 | {rnd[13:0], 5'd0};
        end else if (k_en && k_rdy) begin
            k_addr <= k_addr + 8; k_issued <= k_issued + 1;
            if (k_issued == 3) k_en <= 0;
        end
    end
    // cart line latency
    integer k_got = 0, n_k = 0; time k_t0, k_max = 0, k_sum = 0;
    always @(posedge ui_clk) begin
        if (k_timer == 149 && !k_en) begin k_t0 = $time; k_got = 0; end
        if (k_valid) begin
            k_got = k_got + 1;
            if (k_got == 4) begin
                n_k = n_k + 1; k_sum = k_sum + ($time - k_t0);
                if ($time - k_t0 > k_max) k_max = $time - k_t0;
            end
        end
    end
    // video: 120 reads every 1330 ui_clk
    integer v_timer = 0, v_left = 0, v_late = 0, v_rcv = 0; time v_t0, v_max = 0;
    always @(posedge ui_clk) begin
        if (calib && use_vid) v_timer <= v_timer == 1329 ? 0 : v_timer + 1;
        if (v_timer == 1329) begin
            if (v_left != 0 || v_rcv != 0) v_late = v_late + 1;
            v_left <= 120; v_rcv = 120; v_en <= 1; v_t0 = $time;
        end else if (v_en && v_rdy) begin
            v_addr <= v_addr + 8; v_left <= v_left - 1;
            if (v_left == 1) v_en <= 0;
        end
        if (v_valid) begin
            v_rcv = v_rcv - 1;
            if (v_rcv == 0 && $time - v_t0 > v_max) v_max = $time - v_t0;
        end
    end
    wire d_done = d_open && dp_done_s[1] == dp_tgl;
    wire d_racked = 1'b0;
`endif

    // CPU: back to back
    always @(posedge cpu_clk) begin
        if (!c_open && use_cpu && calib) begin
            c_open <= 1; c_req <= 1; c_rwn <= rnd[1:0] != 0; c_addr <= {rnd[25:2], 2'b00} & 26'h3FFFFFC;
        end else if (c_open) begin
            if (rack && rack_tag == 8'h21) c_req <= 0;
            if ((c_rwn && dack_tag == 8'h21) || (!c_rwn && rack && rack_tag == 8'h21)) c_open <= 0;
        end
    end

    // REU: back to back on ddr2_port
    always @(posedge sys_clk) begin
        if (!preq && use_reu && calib) begin
            preq <= 1; pwe <= rnd_s[0]; paddr <= 26'h2000000 | rnd_s[20:0];
        end else if (pack) preq <= 0;
    end

    // drive: one access per us
    integer n_rd = 0, n_wr = 0, over_500 = 0, over_1000 = 0;
    time    sum_rd = 0, sum_wr = 0, max_rd = 0, max_wr = 0, min_rd = 1 << 40, lat;
    always @(posedge cpu_clk) begin
        if (calib) d_timer <= d_timer + 1;
        if (d_timer == 99 && !d_open) begin
            d_timer <= 0;
            d_open  <= 1; d_start <= $time;
            d_rwn   <= rnd[9:0] < 717;
            d_addr  <= 26'h0EE0000 | rnd[25:10];
`ifdef OLD
            d_req   <= 1;
`else
            dp_tgl  <= ~dp_tgl; dp_addr <= (26'h0EE0000 | rnd[25:10]) >> 1; dp_we <= !(rnd[9:0] < 717);
`endif
        end else if (d_timer == 99) d_timer <= 0;
        if (d_racked) d_req <= 0;
        if (d_open && d_done) begin
            d_open <= 0;
            lat = $time - d_start;
            if (d_rwn) begin
                n_rd = n_rd + 1; sum_rd = sum_rd + lat;
                if (lat > max_rd) max_rd = lat;
                if (lat < min_rd) min_rd = lat;
            end else begin
                n_wr = n_wr + 1; sum_wr = sum_wr + lat;
                if (lat > max_wr) max_wr = lat;
            end
            if (lat > 500000) over_500 = over_500 + 1;
            if (lat > 1000000) over_1000 = over_1000 + 1;
        end
    end

    // throughput of the others
    integer n_cpu = 0, n_reu = 0;
    always @(posedge cpu_clk) if ((c_rwn && dack_tag == 8'h21) || (!c_rwn && rack && rack_tag == 8'h21)) n_cpu = n_cpu + 1;
    always @(posedge sys_clk) if (pack) n_reu = n_reu + 1;

    initial begin
        #(64'd1000000 * (run_us + 3));
        $display("%s cpu=%0d reu+2*cart=%0d: drive reads %0d min %0d avg %0d max %0d ns, writes %0d avg %0d max %0d ns, >500 ns %0d, >1 us %0d; cpu %0d, reu %0d accesses/ms",
`ifdef OLD
            "OLD",
`else
            "NEW",
`endif
            use_cpu, use_reu + 2 * use_cart, n_rd, min_rd / 1000, n_rd ? sum_rd / n_rd / 1000 : 0, max_rd / 1000,
            n_wr, n_wr ? sum_wr / n_wr / 1000 : 0, max_wr / 1000, over_500, over_1000,
            n_cpu * 1000 / run_us, n_reu * 1000 / run_us);
`ifndef OLD
        if (use_cart) $display("    cart lines %0d avg %0d max %0d ns", n_k, n_k ? k_sum / n_k / 1000 : 0, k_max / 1000);
        if (use_vid) $display("    video (in flight <= %0d): line of 120 reads max %0d ns, late %0d", `VMAX, v_max / 1000, v_late);
`endif
        $finish;
    end
endmodule
