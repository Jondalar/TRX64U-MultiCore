// The real mig_arb (host/ddr2_port.v) with all its clients in front of ddr2_lean and the
// DDR2 model, a mixed load: cpu_mem_bridge (RISC-V), ddr2_port (REU/JTAG), the drive's toggle port,
// cart_fill's line reads, the video client (streams of reads, sometimes urgent) and the engine
// client, all at once from their own clocks, each checked: reads of a region preloaded in the
// DRAM model (cart, video, engine, drive lines), read-after-write of private regions against a
// shadow memory (cpu, port, drive). First the functional list of host/sim/tb_mig_arb.v.
//
//   iverilog -g2012 -o tb.vvp tb_mig_arb_lean.v ../cal_seq.v ../../../host/ddr2_lean.v \
//     ../../../host/ddr2_port.v xilinx_models.v ddr2_model.v && vvp tb.vvp
`timescale 1ps/1ps
module tb #(
    parameter integer WR_DLY = 700,
    parameter integer RD_DLY = 900,
    parameter integer LOADCYC = 60000                  // ui_clk cycles of mixed load
);
    reg cpu_clk = 0, sys_clk = 0, clk200 = 0;
    always #5000 cpu_clk = ~cpu_clk;                   // 100 MHz
    always #7937 sys_clk = ~sys_clk;                   // 63 MHz
    always #2500 clk200 = ~clk200;
    reg sys_rst = 1'b1;
    initial #100000 sys_rst = 1'b0;

    wire [15:0] dq; wire [1:0] dqs_p, dqs_n; wire [12:0] ddr_a; wire [2:0] ddr_ba;
    wire ras_n, cas_n, we_n; wire [0:0] ck_p, ck_n, cke, cs_n, odt; wire [1:0] dm;
    wire ui_clk, ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;

    // calibration
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

    // backdoor into the DRAM model: burst b of the app's address (word address / 8)
    task poke(input [22:0] b, input [127:0] x);
        reg [19:0] ix;
        begin
            ix = {b[22:10], b[2:0], b[9:3]};
            board.dram.mem[ix] = x; board.dram.memv[ix] = 1'b1;
        end
    endtask
    function [127:0] peek(input [22:0] b);
        reg [19:0] ix;
        begin ix = {b[22:10], b[2:0], b[9:3]}; peek = board.dram.mem[ix]; end
    endfunction
    function [31:0] h32(input [31:0] x);
        reg [31:0] y;
        begin y = x * 32'h9E3779B1; y = y ^ (y >> 15); y = y * 32'h85EBCA6B; h32 = y ^ (y >> 13); end
    endfunction
    function [127:0] ro_word(input [22:0] b);
        ro_word = {h32({b, 2'd3}), h32({b, 2'd2}), h32({b, 2'd1}), h32({b, 2'd0})};
    endfunction

    // ---- the mig_arb and its clients ----
    reg dtgl = 0, dwe = 0; reg [25:0] daddr; reg [31:0] dwdata; reg [3:0] dbe; reg [1:0] ds = 0;
    wire ddone; wire [127:0] dline;
    always @(posedge cpu_clk) ds <= {ds[0], ddone};
    task drive_access(input w, input [25:0] a, input [3:0] b, input [31:0] d);
        begin
            @(posedge cpu_clk); dwe <= w; daddr <= a; dbe <= b; dwdata <= d; dtgl <= ~dtgl;
            @(posedge cpu_clk); while (ds[1] != dtgl) @(posedge cpu_clk);
        end
    endtask

    reg [26:0] c_addr; reg [2:0] c_cmd; reg c_en = 0, c_wren = 0; reg [127:0] c_wd; reg [15:0] c_mask;
    wire c_rdy, c_wdf_rdy, c_valid; wire [127:0] c_rd;
    reg [127:0] c_got [0:3]; integer c_n = 0;
    always @(posedge ui_clk) if (c_valid) begin c_got[c_n % 4] <= c_rd; c_n <= c_n + 1; end
    task cart_line(input [26:0] a);
        integer i;
        begin
            c_n = 0;
            for (i = 0; i < 4; i = i + 1) begin
                @(posedge ui_clk); c_en <= 1; c_cmd <= 3'b001; c_addr <= a + 8 * i;
                @(posedge ui_clk); while (!c_rdy) @(posedge ui_clk);
                c_en <= 0;
            end
            while (c_n != 4) @(posedge ui_clk);
        end
    endtask

    reg [26:0] v_addr = 0; reg v_en = 0, v_urgent = 0; wire v_rdy, v_wdf_rdy, v_valid;
    reg [26:0] e_addr = 0; reg e_en = 0; wire e_rdy, e_valid;

    reg request = 0, rwn = 1; reg [7:0] tag = 8'h21; reg [25:0] address; reg [3:0] be; reg [31:0] wdata;
    wire [31:0] rdata; wire rack; wire [7:0] rack_tag, dack_tag;
    wire cpu_tgl, cpu_done_tgl, cpu_we; wire [25:0] cpu_addr; wire [31:0] cpu_wdata, cpu_done_rdata; wire [3:0] cpu_be;
    cpu_mem_bridge br (.clk(cpu_clk), .rst(1'b0), .request(request), .tag(tag), .read_writen(rwn),
        .address(address), .byte_en(be), .wdata(wdata), .rdata(rdata), .rack(rack), .rack_tag(rack_tag),
        .dack_tag(dack_tag), .req_tgl(cpu_tgl), .a_addr(cpu_addr), .a_we(cpu_we), .a_wdata(cpu_wdata),
        .a_be(cpu_be), .done_tgl(cpu_done_tgl), .done_rdata(cpu_done_rdata));
    reg preq = 0, pwe = 0; reg [25:0] paddr; reg [31:0] pwdata; reg [3:0] pbe;
    wire pack; wire [31:0] prdata; wire port_tgl, port_done_tgl; wire [31:0] port_done_rdata;
    ddr2_port port (.clk(sys_clk), .req(preq), .addr(paddr), .we(pwe), .wdata(pwdata), .be(pbe),
        .ack(pack), .rdata(prdata), .req_tgl(port_tgl), .done_tgl(port_done_tgl), .done_rdata(port_done_rdata));

    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl({dtgl, port_tgl, cpu_tgl}), .addr({daddr, paddr, cpu_addr}), .we({dwe, pwe, cpu_we}),
        .wdata({dwdata, pwdata, cpu_wdata}), .be({dbe, pbe, cpu_be}),
        .done_tgl({ddone, port_done_tgl, cpu_done_tgl}), .rdata({port_done_rdata, cpu_done_rdata}),
        .rdata_line(dline),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wd),
        .c_wdf_mask(c_mask), .c_wdf_wren(c_wren), .c_wdf_rdy(c_wdf_rdy), .c_rd_data(c_rd),
        .c_rd_data_valid(c_valid),
        .v_addr(v_addr), .v_cmd(3'b001), .v_en(v_en), .v_rdy(v_rdy), .v_wdf_data(128'd0), .v_wdf_mask(16'd0),
        .v_wdf_wren(1'b0), .v_wdf_rdy(v_wdf_rdy), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .e_addr(e_addr), .e_cmd(3'b001), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(e_valid),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    task cpu_write(input [25:0] a, input [3:0] b, input [31:0] d);
        begin
            @(posedge cpu_clk); request <= 1; rwn <= 0; address <= a; be <= b; wdata <= d;
            @(posedge cpu_clk); while (!(rack && rack_tag == tag)) @(posedge cpu_clk);
            request <= 0;
        end
    endtask
    task cpu_read(input [25:0] a, output [31:0] d);
        begin
            @(posedge cpu_clk); request <= 1; rwn <= 1; address <= a; be <= 4'hF;
            @(posedge cpu_clk); while (!(rack && rack_tag == tag)) @(posedge cpu_clk);
            request <= 0;
            while (dack_tag != tag) @(posedge cpu_clk);
            d = rdata;
        end
    endtask
    task port_access(input w, input [25:0] a, input [3:0] b, input [31:0] d, output [31:0] q);
        begin
            @(posedge sys_clk); preq <= 1; pwe <= w; paddr <= a; pbe <= b; pwdata <= d;
            @(posedge sys_clk); while (!pack) @(posedge sys_clk);
            preq <= 0; q = prdata;
        end
    endtask

    integer errors = 0;
    reg [31:0] v, w;
    task check(input [31:0] got, input [31:0] want, input [8*24-1:0] what);
        begin
            if (got !== want) begin errors = errors + 1; $display("FAIL %0s: %08x, want %08x", what, got, want); end
            else $display("ok   %0s: %08x", what, got);
        end
    endtask

    // ---- the mixed load ----
    // regions (bursts): ro 0..1023, cpu 2048..2559, port 4096..4607, drive 6144..6655
    reg [31:0] sh_cpu [0:2047], sh_port [0:2047], sh_drv [0:2047];     // 32-bit words, valid flags
    reg        sv_cpu [0:2047], sv_port [0:2047], sv_drv [0:2047];
    integer ii;
    initial for (ii = 0; ii < 2048; ii = ii + 1) begin sv_cpu[ii] = 0; sv_port[ii] = 0; sv_drv[ii] = 0; end
    reg [31:0] rng = 32'h1234567;
    function [31:0] rnd(input dummy);
        begin rng = rng ^ (rng << 13); rng = rng ^ (rng >> 17); rng = rng ^ (rng << 5); rnd = rng; end
    endfunction
    integer n_cpu = 0, n_port = 0, n_drv = 0, n_cart = 0, n_vid = 0, n_eng = 0;
    reg load_on = 0;

    // monitors of the shared read bus: expected addresses queued per client
    reg [22:0] vq [0:255]; integer vq_w = 0, vq_r = 0;
    reg [22:0] eq [0:255]; integer eq_w = 0, eq_r = 0;
    always @(posedge ui_clk) begin
        if (v_valid) begin
            if (c_rd !== ro_word(vq[vq_r % 256])) begin errors = errors + 1; $display("FAIL video read %0h", vq[vq_r % 256]); end
            vq_r = vq_r + 1; n_vid = n_vid + 1;
        end
        if (e_valid) begin
            if (c_rd !== ro_word(eq[eq_r % 256])) begin errors = errors + 1; $display("FAIL engine read %0h", eq[eq_r % 256]); end
            eq_r = eq_r + 1; n_eng = n_eng + 1;
        end
    end

    // cpu: words at byte addresses 4 * (16 * burst + word) inside its region, random byte enables
    integer k;
    task cpu_load;
        reg [31:0] a, d, got; reg [3:0] b; integer idx;
        begin
            while (load_on) begin
                idx = rnd(0) % 2048;
                a = 32'd32768 + idx * 4;                      // byte address, burst 2048 .. (16 bytes per burst)
                if (!sv_cpu[idx] || rnd(0) % 2 == 0) begin
                    d = rnd(0); b = (rnd(0) % 3 == 0 && sv_cpu[idx]) ? rnd(0) : 4'hF;
                    cpu_write(a[25:0], b, d);
                    if (!sv_cpu[idx]) begin sh_cpu[idx] = 0; sv_cpu[idx] = 1; end
                    for (k = 0; k < 4; k = k + 1) if (b[k]) sh_cpu[idx][8*k +: 8] = d[8*k +: 8];
                end else begin
                    cpu_read(a[25:0], got);
                    if (got !== sh_cpu[idx]) begin errors = errors + 1; $display("FAIL cpu word %0d: %08x want %08x", idx, got, sh_cpu[idx]); end
                end
                n_cpu = n_cpu + 1;
                repeat (rnd(0) % 6) @(posedge cpu_clk);
            end
        end
    endtask
    // port: 16-bit word addresses, 32-bit access at addr & ~1
    task port_load;
        reg [25:0] a; reg [31:0] d, got; reg [3:0] b; integer idx;
        begin
            while (load_on) begin
                idx = rnd(0) % 2048;
                a = 26'd32768 + idx * 2;                      // burst 4096 .. in 16-bit words
                if (!sv_port[idx] || rnd(0) % 2 == 0) begin
                    d = rnd(0); b = (rnd(0) % 3 == 0 && sv_port[idx]) ? rnd(0) : 4'hF;
                    port_access(1, a, b, d, got);
                    if (!sv_port[idx]) begin sh_port[idx] = 0; sv_port[idx] = 1; end
                    for (k = 0; k < 4; k = k + 1) if (b[k]) sh_port[idx][8*k +: 8] = d[8*k +: 8];
                end else begin
                    port_access(0, a, 4'h0, 0, got);
                    if (got !== sh_port[idx]) begin errors = errors + 1; $display("FAIL port word %0d: %08x want %08x", idx, got, sh_port[idx]); end
                end
                n_port = n_port + 1;
                repeat (rnd(0) % 6) @(posedge sys_clk);
            end
        end
    endtask
    // drive: writes a byte lane word, reads the 128-bit line back and checks the line it hit
    task drive_load;
        reg [25:0] a; reg [31:0] d; reg [3:0] b; integer idx, j;
        begin
            while (load_on) begin
                idx = rnd(0) % 2048;
                a = 26'd49152 + idx * 2;                      // burst 6144 ..
                if (!sv_drv[idx] || rnd(0) % 2 == 0) begin
                    d = rnd(0); b = (rnd(0) % 3 == 0 && sv_drv[idx]) ? rnd(0) : 4'hF;
                    drive_access(1, a, b, d);
                    if (!sv_drv[idx]) begin sh_drv[idx] = 0; sv_drv[idx] = 1; end
                    for (k = 0; k < 4; k = k + 1) if (b[k]) sh_drv[idx][8*k +: 8] = d[8*k +: 8];
                end else begin
                    drive_access(0, a, 4'h0, 0);
                    // the line holds the words idx & ~3 .. +3
                    j = idx % 4;
                    if (dline[32*j +: 32] !== sh_drv[idx]) begin errors = errors + 1; $display("FAIL drive line word %0d", idx); end
                end
                n_drv = n_drv + 1;
                repeat (rnd(0) % 8) @(posedge cpu_clk);
            end
        end
    endtask
    // cart_fill: four-burst lines of the read-only region
    task cart_load;
        reg [22:0] b; integer i, bad;
        begin
            while (load_on) begin
                b = (rnd(0) % 256) * 4;
                cart_line({b, 3'b000});
                bad = 0;
                for (i = 0; i < 4; i = i + 1) if (c_got[i] !== ro_word(b + i)) bad = bad + 1;
                if (bad) begin errors = errors + 1; $display("FAIL cart line %0h", b); end
                n_cart = n_cart + 1;
                repeat (rnd(0) % 40) @(posedge ui_clk);
            end
        end
    endtask
    // video: streams of up to 48 reads, some urgent
    task video_load;
        integer i, len; reg [22:0] b;
        begin
            while (load_on) begin
                len = 1 + rnd(0) % 48; b = rnd(0) % 900;
                v_urgent <= (rnd(0) % 4 == 0);
                for (i = 0; i < len; i = i + 1) begin
                    @(posedge ui_clk); v_en <= 1; v_addr <= {b + i, 3'b000};
                    @(posedge ui_clk); while (!v_rdy) @(posedge ui_clk);
                    vq[vq_w % 256] = b + i; vq_w = vq_w + 1;
                    v_en <= 0;
                end
                repeat (rnd(0) % 60) @(posedge ui_clk);
            end
        end
    endtask
    // engine: one read at a time, the frame ring's records
    task engine_load;
        reg [22:0] b;
        begin
            while (load_on) begin
                b = rnd(0) % 900;
                @(posedge ui_clk); e_en <= 1; e_addr <= {b, 3'b000};
                @(posedge ui_clk); while (!e_rdy) @(posedge ui_clk);
                eq[eq_w % 256] = b; eq_w = eq_w + 1;
                e_en <= 0;
                repeat (rnd(0) % 30) @(posedge ui_clk);
                while (eq_r != eq_w) @(posedge ui_clk);
            end
        end
    endtask

    integer cnt;
    initial begin
        wait (cs_done);
        if (!cs_ok) begin $display("FAIL: calibration"); $finish; end
        $display("CAL: dly=%0d bs=%0d taps %0d/%0d widths %0d/%0d", dly, bs, t0, t1, w0, w1);
        for (ii = 0; ii < 1024; ii = ii + 1) poke(ii, ro_word(ii));
        // the functional list of tb_mig_arb.v
        cpu_write(26'h0010, 4'hF, 32'h12345678);
        cpu_read (26'h0010, v);              check(v, 32'h12345678, "cpu word");
        cpu_write(26'h0010, 4'h2, 32'h0000AA00);
        cpu_read (26'h0010, v);              check(v, 32'h1234AA78, "cpu byte 1");
        cpu_write(26'h0010, 4'hC, 32'h69690000);
        cpu_read (26'h0010, v);              check(v, 32'h6969AA78, "cpu half 1");
        cpu_write(26'h001C, 4'hF, 32'hCAFEF00D);
        port_access(0, 26'h000E, 4'h0, 0, v); check(v, 32'hCAFEF00D, "port word at 0x1C");
        port_access(0, 26'h0009, 4'h0, 0, v); check(v[31:16], 16'h6969, "port 16-bit odd");
        port_access(1, 26'h0020, 4'hF, 32'hA5A55A5A, v);
        cpu_read (26'h0040, v);              check(v, 32'hA5A55A5A, "port write, cpu read");
        fork
            cpu_read(26'h001C, v);
            port_access(0, 26'h0008, 4'h0, 0, w);
        join
        check(v, 32'hCAFEF00D, "concurrent cpu");
        check(w, 32'h6969AA78, "concurrent port");
        drive_access(1, 26'h0009, 4'h4, 32'hEEEEEEEE);
        drive_access(0, 26'h0008, 4'h0, 0);   check(dline[31:0], 32'h69EEAA78, "drive line word 0");
        check(dline[127:96], 32'hCAFEF00D, "drive line word 3");
        cpu_read (26'h0010, v);              check(v, 32'h69EEAA78, "drive byte, cpu read");
        poke(16, {4{32'h11111111}}); poke(17, {4{32'h22222222}}); poke(18, {4{32'h33333333}}); poke(19, {4{32'h44444444}});
        fork
            cart_line(27'h80);
            cpu_read(26'h0040, v);
            drive_access(0, 26'h0008, 4'h0, 0);
            port_access(0, 26'h000E, 4'h0, 0, w);
        join
        check(c_got[0][31:0], 32'h11111111, "cart line word 0");
        check(c_got[3][127:96], 32'h44444444, "cart line word 3");
        check(v, 32'hA5A55A5A, "cpu next to the cart");
        check(w, 32'hCAFEF00D, "port next to the cart");
        check(dline[31:0], 32'h69EEAA78, "drive next to the cart");
        for (ii = 0; ii < 1024; ii = ii + 1) poke(ii, ro_word(ii));          // the list changed 16..19

        // everything at once
        load_on = 1;
        fork
            cpu_load; port_load; drive_load; cart_load; video_load; engine_load;
            begin repeat (LOADCYC) @(posedge ui_clk); load_on = 0; end
        join
        cnt = 0;
        while ((vq_r != vq_w || eq_r != eq_w) && cnt < 5000) begin @(posedge ui_clk); cnt = cnt + 1; end
        repeat (200) @(posedge ui_clk);
        $display("LOAD: cpu %0d port %0d drive %0d cart lines %0d video reads %0d engine reads %0d; DRAM refreshes %0d reads %0d writes %0d, longest refresh gap %0d ns",
                 n_cpu, n_port, n_drv, n_cart, n_vid, n_eng, board.dram.n_ref, board.dram.n_rd, board.dram.n_wr, board.dram.max_ref_gap / 1000);
        if (vq_r != vq_w || eq_r != eq_w) begin errors = errors + 1; $display("FAIL: reads outstanding"); end
        if (board.dram.errors != 0 || board.contention != 0) begin errors = errors + 1; $display("FAIL: DRAM errors %0d, contention %0d", board.dram.errors, board.contention); end
        if (errors) $display("FAILED %0d", errors); else $display("PASS mig_arb on ddr2_lean: functional list and mixed load, 0 errors");
        $finish;
    end
    initial begin #(4000000000); $display("FAIL: TIMEOUT"); $finish; end
endmodule
