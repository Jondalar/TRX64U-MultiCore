// T85 S4: amiga_mem on the REAL controller (cal_seq + ddr2_lean + the DRAM model of designs/ddr2_test) behind the real
// mig_arb, with the host's other traffic (the pixel layer's line fetches as the video client, the frame writer on e_*).
// A small machine plays the Minimig's bus: every 7 MHz cycle a DMA stream read, a CPU read or a CPU write, checked
// against a shadow memory at the tick where the CPU takes its data. Reports the read latency of the c_ port in
// ui_clk cycles (command taken to data) and the stall rate (clock enable blocked / clocks).
`timescale 1ps/1ps
module tb_lat #(parameter integer WR_DLY = 700, parameter integer RD_DLY = 900, parameter integer CYCLES = 20000, parameter VIDEO = 1);
    reg sys_clk = 0, ui_dummy = 0, clk200 = 0;
    always #15860 sys_clk = ~sys_clk;                  // 31.53 MHz
    always #2500 clk200 = ~clk200;
    reg sys_rst = 1'b1;
    initial #100000 sys_rst = 1'b0;

    wire [15:0] dq; wire [1:0] dqs_p, dqs_n; wire [12:0] ddr_a; wire [2:0] ddr_ba;
    wire ras_n, cas_n, we_n; wire [0:0] ck_p, ck_n, cke, cs_n, odt; wire [1:0] dm;
    wire ui_clk, ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    wire        r_we; wire [3:0] r_addr; wire [31:0] r_wdata, r_rdata;
    wire        cs_done, cs_ok, cs_init;
    wire [4:0]  t0, t1; wire [2:0] dly; wire [3:0] bs, ah; wire [5:0] w0, w1; wire tr0, tr1;
    cal_seq cs (.clk(ui_clk), .rst(ui_rst), .r_we(r_we), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata),
        .init_done(cs_init), .cal_done(cs_done), .cal_ok(cs_ok), .res_tap0(t0), .res_tap1(t1), .res_dly(dly),
        .res_bs(bs), .res_w0(w0), .res_w1(w1), .res_tr0(tr0), .res_tr1(tr1), .att_hits(ah));
    ddr2_lean lean (
        .ddr2_dq(dq), .ddr2_dqs_n(dqs_n), .ddr2_dqs_p(dqs_p), .ddr2_addr(ddr_a), .ddr2_ba(ddr_ba),
        .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n), .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n),
        .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_dm(dm), .ddr2_odt(odt),
        .sys_clk_i(clk200), .sys_rst(sys_rst),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_end(app_wdf_wren), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy),
        .app_rd_data(app_rd_data), .app_rd_data_end(), .app_rd_data_valid(app_rd_data_valid),
        .ui_clk(ui_clk), .ui_clk_sync_rst(ui_rst), .init_calib_complete(calib),
        .r_we(r_we), .r_addr(r_addr), .r_wdata(r_wdata), .r_rdata(r_rdata));
    ddr2_board #(.WR_DLY(WR_DLY), .RD_DLY(RD_DLY)) board (
        .ddr2_addr(ddr_a), .ddr2_ba(ddr_ba), .ddr2_ras_n(ras_n), .ddr2_cas_n(cas_n), .ddr2_we_n(we_n),
        .ddr2_ck_p(ck_p), .ddr2_ck_n(ck_n), .ddr2_cke(cke), .ddr2_cs_n(cs_n), .ddr2_odt(odt),
        .ddr2_dm(dm), .ddr2_dq(dq), .ddr2_dqs_p(dqs_p), .ddr2_dqs_n(dqs_n),
        .fpga_oe_dq(lean.phy.dq[0].tq === 1'b0), .fpga_oe_dqs(lean.phy.dqs[0].tq === 1'b0));
    task poke(input [22:0] b, input [127:0] x);
        reg [19:0] ix;
        begin ix = {b[22:10], b[2:0], b[9:3]}; board.dram.mem[ix] = x; board.dram.memv[ix] = 1'b1; end
    endtask
    function [31:0] h32(input [31:0] x);
        reg [31:0] y;
        begin y = x * 32'h9E3779B1; y = y ^ (y >> 15); y = y * 32'h85EBCA6B; h32 = y ^ (y >> 13); end
    endfunction
    function [127:0] ro_word(input [22:0] b);
        ro_word = {h32({b, 2'd3}), h32({b, 2'd2}), h32({b, 2'd1}), h32({b, 2'd0})};
    endfunction
    function [15:0] expect_word(input [21:0] a);       // the Amiga word of the preloaded DRAM contents
        reg [127:0] l;
        begin l = ro_word(a >> 3); expect_word = {l[16*(a & 7) +: 8], l[16*(a & 7) + 8 +: 8]}; end
    endfunction

    // the arbiter with a pixel-layer-like video client and the frame writer on e_*
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en, c_rdy, c_wdf_rdy, c_valid; wire [127:0] c_wdf_data, c_rd; wire [15:0] c_wdf_mask; wire c_wdf_wren;
    reg [26:0] v_addr = 0; reg v_en = 0, v_urgent = 0; wire v_rdy, v_wdf_rdy, v_valid;
    reg [26:0] e_addr = 0; reg e_en = 0; reg [2:0] e_cmd = 3'b000; reg [127:0] e_data = 128'd0; wire e_rdy, e_valid;
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl(3'b000), .addr(78'd0), .we(3'b000), .wdata(96'd0), .be(12'd0), .done_tgl(), .rdata(), .rdata_line(),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask),
        .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(c_wdf_rdy), .c_rd_data(c_rd), .c_rd_data_valid(c_valid),
        .v_addr(v_addr), .v_cmd(3'b001), .v_en(v_en), .v_rdy(v_rdy), .v_wdf_data(128'd0), .v_wdf_mask(16'd0), .v_wdf_wren(1'b0),
        .v_wdf_rdy(v_wdf_rdy), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_data), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(e_valid),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy), .app_wdf_data(app_wdf_data),
        .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren), .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data),
        .app_rd_data_valid(app_rd_data_valid));

    // ---- the Amiga side
    wire ce28; reg [1:0] phase = 2'd2;
    wire pend; wire [15:0] dout;
    reg bus_cs = 0, we = 0; reg [21:0] addr = 0; reg [1:0] be = 2'b11; reg [15:0] din = 0;
    wire clk7_en = (phase == 2'd1);
    amiga_ce ce (.clk(sys_clk), .phase(phase), .pend(pend), .pause(1'b0), .ce28(ce28));
    amiga_mem #(.BASE(27'd0)) mem (.sys_clk(sys_clk), .sys_rst(sys_rst), .flush(~calib), .ce28(ce28), .clk7_en(clk7_en),
        .cs(bus_cs), .we(we), .addr(addr), .be(be), .din(din), .dout(dout), .pend(pend),
        .p2_sel(1'b0), .p2_wr(1'b0), .p2_addr(22'd0), .p2_be(2'b00), .p2_din(16'd0), .p2_dout(), .p2_ready(),
        .ui_clk(ui_clk), .ui_rst(ui_rst | ~calib), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(c_wdf_rdy),
        .c_rd_data(c_rd), .c_rd_valid(c_valid));

    reg [31:0] rng = 32'h2468ace;
    function [31:0] rnd(input dummy);
        begin rng = rng ^ (rng << 13); rng = rng ^ (rng >> 17); rng = rng ^ (rng << 5); rnd = rng; end
    endfunction
    reg [15:0] shadow [0:16383]; reg shv [0:16383];
    integer ii; initial for (ii = 0; ii < 16384; ii = ii + 1) shv[ii] = 0;
    integer ptr [0:3]; initial begin ptr[0] = 0; ptr[1] = 0; ptr[2] = 0; ptr[3] = 0; end
    integer errors = 0, ncyc = 0, nreads = 0, nwrites = 0, ticks = 0, clks = 0;
    reg started = 0;
    reg [15:0] want; reg chk = 0;
    integer r, k;
    // ticks: phase advances on ce28; a new request after the tick that ends phase 1 (the 7 MHz tick)
    always @(posedge sys_clk) if (started) begin
        clks <= clks + 1;
        if (ce28) begin
            ticks <= ticks + 1;
            // the CPU takes its data at the tick that ends phase 0
            if (phase == 2'd0 && chk) begin
                if (dout !== want) begin errors = errors + 1; $display("FAIL read %0h got %04x want %04x", addr, dout, want); end
                chk <= 0;
            end
            phase <= phase + 2'd1;
            if (phase == 2'd1) begin                                  // the 7 MHz tick: the next cycle's request
                ncyc <= ncyc + 1;
                r = rnd(0) % 100;
                bus_cs <= 1; we <= 0; be <= 2'b11;
                if (r < 60) begin
                    k = rnd(0) % 4;
                    addr <= (k * 22'h1000) + (ptr[k] % 2048); ptr[k] = ptr[k] + 1;
                    nreads = nreads + 1;
                    chk <= 1;
                    want <= shv[(k * 22'h1000 + (ptr[k] - 1) % 2048) % 16384] ? shadow[(k * 22'h1000 + (ptr[k] - 1) % 2048) % 16384] : expect_word(k * 22'h1000 + (ptr[k] - 1) % 2048);
                end else if (r < 85) begin
                    k = 22'h3000 + (rnd(0) % 4096);
                    addr <= k; nreads = nreads + 1; chk <= 1;
                    want <= shv[k % 16384] ? shadow[k % 16384] : expect_word(k);
                end else if (r < 97) begin
                    k = 22'h3000 + (rnd(0) % 4096);
                    addr <= k; we <= 1; din <= rnd(0); be <= (rnd(0) % 3 == 0) ? rnd(0) : 2'b11; nwrites = nwrites + 1;
                    // the shadow takes the write at once (the next read of it is cycles away)
                    if (!shv[k % 16384]) begin shadow[k % 16384] = expect_word(k); shv[k % 16384] = 1; end
                end else begin
                    bus_cs <= 0;
                end
            end
        end
    end
    // the shadow update for a write has to see din/be: after the cycle's request is placed
    reg wr_d = 0; reg [21:0] wa; reg [15:0] wd; reg [1:0] wb;
    always @(posedge sys_clk) if (ce28 && started) begin
        wr_d <= (phase == 2'd2) && bus_cs && we;
        if (phase == 2'd2 && bus_cs && we) begin
            if (be[1]) shadow[addr % 16384][15:8] = din[15:8];
            if (be[0]) shadow[addr % 16384][7:0] = din[7:0];
        end
    end

    // the host's other traffic: pixel-layer line fetches (120 reads per ~35 us, some urgent) and frame-writer writes
    reg load_on = 0;
    integer vi, vlen;
    initial begin
        wait (load_on);
        forever begin
            if (VIDEO) begin
                vlen = 60 + rnd(0) % 60; v_urgent <= (rnd(0) % 5 == 0);
                for (vi = 0; vi < vlen; vi = vi + 1) begin
                    @(posedge ui_clk); v_en <= 1; v_addr <= {23'd60000 + vi, 3'b000};
                    @(posedge ui_clk); while (!v_rdy) @(posedge ui_clk);
                    v_en <= 0;
                end
            end
            repeat (200 + rnd(0) % 1500) @(posedge ui_clk);
        end
    end
    integer ei;
    initial begin
        wait (load_on);
        forever begin
            for (ei = 0; ei < 90; ei = ei + 1) begin
                @(posedge ui_clk); e_en <= 1; e_cmd <= 3'b000; e_addr <= {23'd70000 + ei, 3'b000}; e_data <= {4{h32(ei)}};
                @(posedge ui_clk); while (!e_rdy) @(posedge ui_clk);
                e_en <= 0;
            end
            repeat (400 + rnd(0) % 600) @(posedge ui_clk);
        end
    end
    // latency of c_ reads: command taken to data, in ui_clk cycles
    integer lat_t0 [0:15]; integer lat_w = 0, lat_r = 0, lat_n = 0, lat_sum = 0, lat_max = 0, lat_min = 1000000, cyc_ui = 0;
    integer hist [0:63]; integer hh; initial for (hh = 0; hh < 64; hh = hh + 1) hist[hh] = 0;
    always @(posedge ui_clk) begin
        cyc_ui <= cyc_ui + 1;
        if (c_en && c_rdy && c_cmd == 3'b001) begin lat_t0[lat_w % 16] = cyc_ui; lat_w = lat_w + 1; end
        if (c_valid) begin
            hh = cyc_ui - lat_t0[lat_r % 16]; lat_r = lat_r + 1;
            lat_n = lat_n + 1; lat_sum = lat_sum + hh; if (hh > lat_max) lat_max = hh; if (hh < lat_min) lat_min = hh;
            hist[hh > 63 ? 63 : hh] = hist[hh > 63 ? 63 : hh] + 1;
        end
    end

    // timeline of the first misses
    integer nm = 0; time t_cf, t_take, t_val, t_fd, t_ans;
    always @(posedge sys_clk) begin
        if (mem.cf_wr && mem.cf_wdata[41:40] == 2'b01 && nm < 8) t_cf = $time;
        if (mem.fill_done && mem.fill_inflight && nm < 8) t_fd = $time;
        if (mem.st == 3'd3 && nm < 8 && mem.chip_pend == 0 && t_cf != 0 && $time > t_cf) begin end
    end
    always @(posedge ui_clk) begin
        if (c_en && c_rdy && c_cmd == 3'b001 && nm < 8) t_take = $time;
        if (c_valid && nm < 8) t_val = $time;
    end
    always @(negedge mem.chip_pend) if (nm < 8 && t_cf != 0 && t_fd > t_cf) begin
        nm = nm + 1;
        $display("miss %0d: cmd pushed %0t -> taken by arbiter +%0d ns -> data +%0d ns -> fill_done seen +%0d ns -> answered +%0d ns", nm, t_cf,
                 (t_take - t_cf)/1000, (t_val - t_cf)/1000, (t_fd - t_cf)/1000, ($time - t_cf)/1000);
    end
    initial begin
        wait (cs_done);
        if (!cs_ok) begin $display("FAIL: calibration"); $finish; end
        for (ii = 0; ii < 2048; ii = ii + 1) poke(ii, ro_word(ii));
        #2000000;
        load_on = 1; started = 1;
        wait (ncyc >= CYCLES);
        $display("S4 (video load %0d): %0d bus cycles, %0d reads %0d writes; ticks %0d in %0d clocks (ideal 0.90): stall %0d.%02d %%", VIDEO, ncyc, nreads, nwrites,
            ticks, clks, 100 - ticks * 1000 / clks / 9 , (1000 - ticks * 10000 / clks / 9 * 10 / 10) % 100);
        $display("c_ read latency (command taken to data, ui_clk cycles of 13.33 ns): n=%0d min %0d avg %0d.%02d max %0d", lat_n, lat_min,
            lat_sum / lat_n, (lat_sum * 100 / lat_n) % 100, lat_max);
        $display("cache: reads %0d miss %0d prefetch %0d", mem.st_reads, mem.st_miss, mem.st_pf);
        $display("hist: 8:%0d 9:%0d 10:%0d 11:%0d 12:%0d 13:%0d 14:%0d 15:%0d 16:%0d 17+:%0d", hist[8], hist[9], hist[10], hist[11], hist[12], hist[13], hist[14], hist[15], hist[16], lat_n - hist[8] - hist[9] - hist[10] - hist[11] - hist[12] - hist[13] - hist[14] - hist[15] - hist[16]);
        $display("%s tb_lat errors=%0d", errors == 0 ? "PASS" : "FAIL", errors);
        $finish;
    end
    initial begin #3000000000; $display("FAIL timeout, cycles %0d", ncyc); $finish; end
endmodule
