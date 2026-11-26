// Unit bench for the cartridge (iverilog, run.sh): cart_slot_c64ref (the C64 reference core's cartridge slot) with icepi's
// model, cart_fill against a MIG model (random stalls, read latency) and
// trx_io in the app's clock. The app's interrupt handler is played here: it
// applies each flash operation to the DDR2 image with direct stores (its own
// sector map), then writes INVAL | ACK. So is the C64: bus cycles on the
// cartridge port with kawari's strobes (phi2_p, phi2_h 9 clocks later, phi2_n
// at the end of the CPU half), ROML/ROMH/IO1/IO2 decoded from EXROM/GAME the
// way the PLA does, a CPU read repeated while wait_cpu holds it (RDY).
// The ROM image starts as pat(byte offset); the checks compare against it.
`timescale 1ns / 1ps
module tb_cart_unit;
    `include "desc_app.vh"                  // cart_desc(CART_TYPE): tools/gen_cart_descriptors.py
    reg clk = 1'b0, ui_clk = 1'b0, sclk = 1'b0;
    always #16 clk = ~clk;          // C64 dot4x
    always #6.5 ui_clk = ~ui_clk;   // MIG ui_clk
    always #5 sclk = ~sclk;         // the app's clock

    integer fails = 0, checks = 0;
    task check(input cond, input [8*64-1:0] what);
        begin
            checks = checks + 1;
            if (!cond) begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask

    function [7:0] pe(input [22:0] i);        // the EasyFlash 3 image (8 MB), as ROM_SEL 1 holds it
        pe = pat(i[21:0]) ^ (i[22] ? 8'hC3 : 8'h00) ^ 8'h11;
    endfunction
    function [7:0] pat(input [21:0] i);
        pat = i[7:0] ^ i[15:8] ^ {i[21:16], 2'b01} ^ (i[13] ? 8'h5A : 8'h00);
    endfunction

    // ---- the cartridge ---------------------------------------------------------------
    reg  [15:0] addr = 16'd0;
    reg   [7:0] din = 8'hFF;
    reg         we = 1'b0, roml_n = 1'b1, romh_n = 1'b1, io1_n = 1'b1, io2_n = 1'b1;
    reg         phi2_p = 1'b0, phi2_h = 1'b0, phi2_n = 1'b0, reset = 1'b1;
    reg   [7:0] cart_type = 8'd0;
    reg         kill_tgl = 1'b0, force_tgl = 1'b0;
    wire        cart_active, exrom, game, nmi, irq, io2_we_valid, wait_cpu;
    wire  [7:0] dout;
    wire        req_tgl, req_we, req_cached, ack_tgl;
    wire [23:0] req_addr;
    wire  [7:0] req_data, fill_be;
    wire  [5:0] req_slot;
    wire  [8:0] fill_addr;
    wire [63:0] fill_data;
    wire        fl_enable, evt_tgl, fl_ack_tgl, inval_tgl, ee_tgl;
    wire  [1:0] rom_sel;                    // trx_io ROM_SEL (T60)
    wire [15:0] fl_magic1, fl_magic2, fl_mask;
    wire [23:0] fl_id;
    wire [33:0] evt_word;
    wire [343:0] desc;
    wire [10:0] ee_addr;
    wire        ee_we;
    wire  [7:0] ee_wdata, ee_rdata;

    cart_slot_c64ref dut (
        .clk(clk), .cart_type(cart_type), .cart_kill_tgl(kill_tgl), .cart_force_tgl(force_tgl),
        .cart_active(cart_active), .desc(desc),
        .addr(addr), .din(din), .we(we), .roml_n(roml_n), .romh_n(romh_n), .io1_n(io1_n), .io2_n(io2_n),
        .phi2_p(phi2_p), .phi2_h(phi2_h), .phi2_n(phi2_n), .reset(reset),
        .exrom(exrom), .game(game), .dout(dout), .nmi(nmi), .irq(irq), .io2_we_valid(io2_we_valid),
        .wait_cpu(wait_cpu),
        .req_tgl(req_tgl), .req_we(req_we), .req_addr(req_addr), .req_data(req_data), .req_slot(req_slot),
        .req_cached(req_cached), .ack_tgl(ack_tgl),
        .fill_clk(ui_clk), .fill_be(fill_be), .fill_addr(fill_addr), .fill_data(fill_data),
        .fl_enable(fl_enable), .fl_magic1(fl_magic1), .fl_magic2(fl_magic2), .fl_mask(fl_mask), .fl_id(fl_id),
        .evt_tgl(evt_tgl), .evt_word(evt_word), .fl_ack_tgl(fl_ack_tgl), .inval_tgl(inval_tgl), .ee_tgl(ee_tgl),
        .ee_clk(sclk), .ee_addr(ee_addr), .ee_we(ee_we), .ee_wdata(ee_wdata), .ee_rdata(ee_rdata),
        .pk_addr(16'd0), .pk_cls(2'd0), .pk_hit(), .pk_mem(), .pk_maddr(), .pk_data()
    );

    // ---- DDR2 behind cart_fill: a MIG app port model ---------------------------------
    wire [26:0]  app_addr;
    wire [2:0]   app_cmd;
    wire         app_en, app_wdf_wren;
    wire [127:0] app_wdf_data;
    wire [15:0]  app_wdf_mask;
    reg          app_rdy = 1'b1, app_wdf_rdy = 1'b1, app_rd_data_valid = 1'b0;
    reg  [127:0] app_rd_data = 128'd0;
    reg          ui_rst = 1'b1;

    reg         pk_tgl = 1'b0;                  // the lens unit's look (dma_win)
    reg  [23:0] pk_addr = 24'd0;
    wire        pk_ack_tgl;
    wire  [7:0] pk_data;
    task peek(input [23:0] a, output [7:0] d);
        reg t0;
        begin
            t0 = pk_ack_tgl; pk_addr = a;
            @(negedge ui_clk); pk_tgl = ~pk_tgl;
            wait (pk_ack_tgl !== t0);
            @(negedge ui_clk); d = pk_data;
        end
    endtask
    cart_fill fill (
        .ui_clk(ui_clk), .ui_rst(ui_rst), .rom_sel(rom_sel),
        .req_tgl(req_tgl), .req_we(req_we), .req_addr(req_addr), .req_data(req_data),
        .req_slot(req_slot), .req_cached(req_cached), .ack_tgl(ack_tgl),
        .c_be(fill_be), .c_addr(fill_addr), .c_data(fill_data),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid),
        .rbw(1'b0), .ob_v(), .ob_addr(), .ob_old(),
        .pk_tgl(pk_tgl), .pk_addr(pk_addr), .pk_ack_tgl(pk_ack_tgl), .pk_data(pk_data)
    );

    reg [7:0] rom  [0:4194303];             // DDR2 0x3C00000 (ROM_SEL 0, Gideon's layout)
    reg [7:0] blk  [0:8388607];             // DDR2 0x1000000 (ROM_SEL 1, the expansion-port block, 8 MB since T51)
    reg [7:0] slt  [0:65535];               // DDR2 0xF00000 (ROM_SEL 2, the built-in cart slot)
    reg [7:0] cram [0:65535];               // DDR2 0xEF0000
    integer   stray = 0;                    // a write to ROM space the model does not hold
    function [7:0] mem_rd(input [31:0] b);
        if (b >= 32'h3C00000 && b < 32'h4000000) mem_rd = rom[b - 32'h3C00000];
        else if (b >= 32'h1000000 && b < 32'h1800000) mem_rd = blk[b - 32'h1000000];
        else if (b >= 32'hF00000 && b < 32'hF10000) mem_rd = slt[b - 32'hF00000];
        else if (b >= 32'hEF0000 && b < 32'hF00000) mem_rd = cram[b - 32'hEF0000];
        else mem_rd = 8'h00;
    endfunction
    task mem_wr(input [31:0] b, input [7:0] v);
        if (b >= 32'h3C00000 && b < 32'h4000000) rom[b - 32'h3C00000] = v;
        else if (b >= 32'h1000000 && b < 32'h1800000) blk[b - 32'h1000000] = v;
        else if (b >= 32'hF00000 && b < 32'hF10000) slt[b - 32'hF00000] = v;
        else if (b >= 32'hEF0000 && b < 32'hF00000) cram[b - 32'hEF0000] = v;
    endtask

    reg [26:0]  rdq_a [0:31];
    integer     rdq_due [0:31];
    integer     rdq_h = 0, rdq_t = 0;
    reg [26:0]  wcq [0:31];
    reg [127:0] wdq [0:31];
    reg [15:0]  wmq [0:31];
    integer     wc_h = 0, wc_t = 0, wd_h = 0, wd_t = 0;
    integer     ui_cyc = 0, mig_writes = 0, rq_count = 0;
    reg         req_tgl_d = 1'b0;
    always @(posedge clk) begin
        req_tgl_d <= req_tgl;
        if (req_tgl != req_tgl_d) rq_count = rq_count + 1;
    end
    integer     j;
    always @(posedge ui_clk) begin
        ui_cyc = ui_cyc + 1;
        if (app_rd_data_valid) rdq_h = rdq_h + 1;
        if (app_en && app_rdy) begin
            if (app_cmd == 3'b001) begin
                rdq_a[rdq_t % 32] = app_addr; rdq_due[rdq_t % 32] = ui_cyc + 20 + ($random & 7); rdq_t = rdq_t + 1;
            end else begin
                wcq[wc_t % 32] = app_addr; wc_t = wc_t + 1;
            end
        end
        if (app_wdf_wren && app_wdf_rdy) begin
            wdq[wd_t % 32] = app_wdf_data; wmq[wd_t % 32] = app_wdf_mask; wd_t = wd_t + 1;
        end
        while (wc_h < wc_t && wd_h < wd_t) begin
            for (j = 0; j < 16; j = j + 1)
                if (!wmq[wd_h % 32][j]) mem_wr({wcq[wc_h % 32], 1'b0} + j, wdq[wd_h % 32][8*j +: 8]);
            wc_h = wc_h + 1; wd_h = wd_h + 1; mig_writes = mig_writes + 1;
        end
        app_rdy     <= ($random & 7) != 0;
        app_wdf_rdy <= ($random & 7) != 0;
        if (rdq_h < rdq_t && rdq_due[rdq_h % 32] <= ui_cyc && ($random & 3) != 0) begin
            for (j = 0; j < 16; j = j + 1) app_rd_data[8*j +: 8] <= mem_rd({rdq_a[rdq_h % 32], 1'b0} + j);
            app_rd_data_valid <= 1'b1;
        end else
            app_rd_data_valid <= 1'b0;
    end

    // ---- the app's side: trx_io, and its interrupt handler ------------------------------
    reg         s_we = 1'b0, s_re = 1'b0;
    reg  [16:0] s_addr = 17'd0;
    reg   [7:0] s_wdata = 8'd0;
    wire  [7:0] s_rdata;
    wire        cw_irq;
    trx_io tio (
        .clk(sclk), .we(s_we), .re(s_re), .addr(s_addr), .wdata(s_wdata), .rdata(s_rdata), .irq(cw_irq),
        .ack(), .rq_tgl(), .rq_addr(), .rq_we(), .rq_wdata(), .rs_tgl(1'b0), .rs_rdata(8'd0),
        .stop_tgl(1'b0), .cap_tgl(1'b0), .eng_tgl(1'b0), .gap_tgl(1'b0),
        .fl_enable(fl_enable), .fl_magic1(fl_magic1), .fl_magic2(fl_magic2), .fl_mask(fl_mask), .fl_id(fl_id),
        .cart_desc(desc),
        .evt_tgl(evt_tgl), .evt_word(evt_word), .fl_ack_tgl(fl_ack_tgl), .inval_tgl(inval_tgl), .rom_sel(rom_sel), .ee_tgl(ee_tgl),
        .ee_addr(ee_addr), .ee_we(ee_we), .ee_wdata(ee_wdata), .ee_rdata(ee_rdata),
        .p6_we(), .p6_re(), .p6_rdata(8'd0), .ev_vsync(1'b0), .ev_flip(1'b0), .build(32'd0), .ddr_cal(64'd0)
    );
    localparam [16:0] P0 = 17'h00000, P4 = 17'h04000, EEW = 17'h10000;
    reg lock = 1'b0;                        // the bus between the main thread and the handler
    task automatic sys_wr(input [16:0] a, input [7:0] v);
        begin
            wait (!lock); lock = 1'b1;
            @(negedge sclk); s_addr = a; s_wdata = v; s_we = 1'b1;
            @(negedge sclk); s_we = 1'b0;
            lock = 1'b0;
        end
    endtask
    task automatic sys_rd(input [16:0] a, output [7:0] v);
        begin
            wait (!lock); lock = 1'b1;
            @(negedge sclk); s_addr = a; s_re = 1'b1;
            @(negedge sclk); s_re = 1'b0; v = s_rdata;
            lock = 1'b0;
        end
    endtask

    // flash part: 0 Am29F040 (512 KB, 64 KB sectors), 1 MX29F800CB (1 MB, bottom
    // boot), 2 M29F160FT (2 MB, top boot), 3 MX29F016 / the EasyFlashXl's 2 MB
    // chips (64 KB sectors); two chips (EasyFlash) or one, one chip linear
    // across ROML and ROMH (TwoMegabyter: chip address = image offset)
    integer part = 0;
    reg     two_chips = 1'b0, linear = 1'b0;
    task automatic flash_cfg(input [15:0] m1, input [15:0] m2, input [15:0] mask, input [23:0] id);
        begin
            sys_wr(P4 + 17'h004, m1[7:0]); sys_wr(P4 + 17'h005, m1[15:8]);
            sys_wr(P4 + 17'h008, m2[7:0]); sys_wr(P4 + 17'h009, m2[15:8]);
            sys_wr(P4 + 17'h00C, mask[7:0]); sys_wr(P4 + 17'h00D, mask[15:8]);
            sys_wr(P4 + 17'h010, id[7:0]); sys_wr(P4 + 17'h011, id[15:8]); sys_wr(P4 + 17'h012, id[23:16]);
            sys_wr(P4 + 17'h000, 8'h01);
        end
    endtask
    function [21:0] img_of(input [20:0] a, input chip);
        img_of = linear ? {1'b0, a} : two_chips ? {a[20:13], chip, a[12:0]} : {a[20:13], 1'b0, a[12:0]};
    endfunction
    integer flash_ops = 0, ee_irqs = 0;
    integer isr_delay = 0;                  // ns the handler takes before it applies
    reg [33:0] ev;
    reg  [7:0] iv, evh;
    reg [22:0] ca;
    integer    chip_erases = 0;
    // EasyFlash 3 (part 4): the chip's address {slot, bank_hi, ROMH, bank_lo, A12-A0} -> the image offset
    // {slot, bank_hi, bank_lo, ROMH, A12-A0} (what trx_cart.cc does)
    function [22:0] ef3_img(input [22:0] a);
        ef3_img = {a[22:17], a[15:13], a[16], a[12:0]};
    endfunction
    integer s0, sz, x;
    always begin
        wait (cw_irq);
        sys_rd(P0 + 17'h014, iv);
        if (iv[4]) begin
            sys_rd(P4 + 17'h020, ev[7:0]); sys_rd(P4 + 17'h021, ev[15:8]);
            sys_rd(P4 + 17'h022, ev[23:16]); sys_rd(P4 + 17'h023, ev[31:24]);
            sys_rd(P4 + 17'h024, evh); ev[33:32] = evh[1:0];
            sys_wr(P0 + 17'h014, 8'h10);
            ca = {ev[33:32], ev[28:8]};
            #(isr_delay);
            if (part == 4) begin
                if (ev[31:30] == 2'd1)
                    blk[ef3_img(ca)] = blk[ef3_img(ca)] & ev[7:0];
                else if (ev[31:30] == 2'd2) begin
                    if (ca < 23'h10000) begin s0 = ca & 23'h7FE000; sz = 32'h2000; end
                    else begin s0 = ca & 23'h7F0000; sz = 32'h10000; end
                    for (x = s0; x < s0 + sz; x = x + 1) blk[ef3_img(x)] = 8'hFF;
                end else begin
                    chip_erases = chip_erases + 1;      // a chip erase wipes the whole 8 MB image (T51 Q1)
                    for (x = 0; x < 32'h800000; x = x + 1) blk[x] = 8'hFF;
                end
            end else if (ev[31:30] == 2'd1)
                rom[img_of(ca, ev[29])] = rom[img_of(ca, ev[29])] & ev[7:0];
            else if (ev[31:30] != 2'd0) begin
                s0 = {ca[20:16], 16'd0}; sz = 32'h10000;
                if (part == 1 && ca[20:16] == 5'd0) begin
                    if (ca[15]) begin s0 = 32'h8000; sz = 32'h8000; end
                    else if (!ca[14]) begin s0 = 32'h0; sz = 32'h4000; end
                    else if (!ca[13]) begin s0 = 32'h4000; sz = 32'h2000; end
                    else begin s0 = 32'h6000; sz = 32'h2000; end
                end
                if (part == 2 && ca[20:16] == 5'd31) begin
                    if (!ca[15]) begin s0 = 32'h1F0000; sz = 32'h8000; end
                    else if (ca[14:13] == 2'b00) begin s0 = 32'h1F8000; sz = 32'h2000; end
                    else if (ca[14:13] == 2'b01) begin s0 = 32'h1FA000; sz = 32'h2000; end
                    else begin s0 = 32'h1FC000; sz = 32'h4000; end
                end
                if (ev[31:30] == 2'd3) begin
                    s0 = 0; sz = part >= 2 ? 32'h200000 : part == 1 ? 32'h100000 : 32'h80000;
                end
                for (x = s0; x < s0 + sz; x = x + 1) rom[img_of(x, ev[29])] = 8'hFF;
            end
            flash_ops = flash_ops + 1;
            sys_wr(P4 + 17'h028, 8'h03);    // INVAL | ACK
        end
        if (iv[5]) begin
            ee_irqs = ee_irqs + 1;
            sys_wr(P0 + 17'h014, 8'h20);
        end
    end

    // ---- C64 bus cycles ----------------------------------------------------------------
    integer cycles = 0, held = 0;
    reg     last_roml = 1'b0, last_romh = 1'b0;   // the PLA's selects in the last cycle
    task bus(input [15:0] a, input w, input [7:0] d, output [7:0] q);
        reg retry, umax;
        begin
            retry = 1'b1;
            while (retry) begin
                @(negedge clk);
                addr   = a; we = w; din = w ? d : 8'hFF;
                #1;                         // the lines may depend on R/W and A15 (GAMEW)
                umax   = exrom && !game;
                roml_n = !(a[15:13] == 3'b100 && (umax || (!exrom && !w)));
                romh_n = !((a[15:13] == 3'b101 && !exrom && !game && !w) || (a[15:13] == 3'b111 && umax));
                io1_n  = a[15:8] != 8'hDE;
                io2_n  = a[15:8] != 8'hDF;
                last_roml = !roml_n; last_romh = !romh_n;
                phi2_p = 1'b1;
                @(negedge clk); phi2_p = 1'b0;
                repeat (8) @(negedge clk);
                phi2_h = 1'b1;
                @(negedge clk); phi2_h = 1'b0;
                repeat (6) @(negedge clk);
                phi2_n = 1'b1;
                q = dout;
                retry = !w && wait_cpu;
                if (retry) held = held + 1;
                @(negedge clk); phi2_n = 1'b0;
                we = 1'b0; roml_n = 1'b1; romh_n = 1'b1; io1_n = 1'b1; io2_n = 1'b1; din = 8'hFF;
                repeat (14) @(negedge clk);     // the VIC's half: nothing for the cartridge
                cycles = cycles + 1;
            end
        end
    endtask
    reg [7:0] tmp;
    reg [343:0] desc_a;
    task wr(input [15:0] a, input [7:0] v); bus(a, 1'b1, v, tmp); endtask
    task rd(input [15:0] a, output [7:0] v); bus(a, 1'b0, 8'h00, v); endtask
    task expect_rd(input [15:0] a, input [7:0] want, input [8*48-1:0] what);
        reg [7:0] v;
        begin
            rd(a, v);
            if (v !== want) $display("  %0s: $%h = %h, want %h", what, a, v, want);
            check(v === want, what);
        end
    endtask

    // the app's descriptor writes (trx_cart.cc): 43 bytes to page 4 from 0x040
    task desc_wr(input [343:0] dv);
        integer n;
        begin
            for (n = 0; n < 43; n = n + 1) sys_wr(P4 + 17'h040 + n, dv[8*n +: 8]);
        end
    endtask
    // start a cartridge the way C64::start_cartridge does: reset, CART_TYPE and
    // the descriptor, unreset
    task start_desc(input [7:0] t, input [343:0] dv);
        begin
            reset = 1'b1;
            repeat (64) @(negedge clk);
            cart_type = t;
            desc_wr(dv);
            repeat (64) @(negedge clk);
            reset = 1'b0;
            repeat (64) @(negedge clk);
        end
    endtask
    task start(input [7:0] t);
        start_desc(t, cart_desc(t));
    endtask

    // AMD command cycles; poll until the part reads back `want' at `a'
    task amd(input [15:0] a1, input [15:0] a2, input [15:0] a, input [7:0] c);
        begin wr(a1, 8'hAA); wr(a2, 8'h55); wr(a, c); end
    endtask
    task poll(input [15:0] a, input [7:0] want, input [8*48-1:0] what);
        poll_m(a, 8'hFF, want, what);
    endtask
    task poll_m(input [15:0] a, input [7:0] m, input [7:0] want, input [8*48-1:0] what);
        integer n;
        reg [7:0] v;
        begin
            n = 0; v = ~want;
            while ((v & m) !== want && n < 400) begin rd(a, v); n = n + 1; end
            v = v & m;
            if (v !== want) $display("  %0s: $%h = %h after %0d polls, want %h", what, a, v, n, want);
            check(v === want, what);
        end
    endtask

    task wr_count(output [15:0] c);
        reg [7:0] v;
        begin sys_rd(P4 + 17'h02C, v); c[7:0] = v; sys_rd(P4 + 17'h02D, v); c[15:8] = v; end
    endtask

    // 93C86 bit-banging through the GMod2 register: CS b6, CLK b5, DI b4, DO b7
    task ee_bit(input b);
        begin wr(16'hDE00, {1'b0, 1'b1, 1'b0, b, 4'd0}); wr(16'hDE00, {1'b0, 1'b1, 1'b1, b, 4'd0}); end
    endtask
    task ee_cmd(input [2:0] sop, input [9:0] a);     // start bit, op, 10 address bits
        integer i;
        begin
            wr(16'hDE00, 8'h40);
            for (i = 2; i >= 0; i = i - 1) ee_bit(sop[i]);
            for (i = 9; i >= 0; i = i - 1) ee_bit(a[i]);
        end
    endtask
    task ee_write(input [9:0] a, input [15:0] w);
        integer i;
        begin
            ee_cmd(3'b101, a);
            for (i = 15; i >= 0; i = i - 1) ee_bit(w[i]);
            wr(16'hDE00, 8'h00);            // deselect: the write starts
            wr(16'hDE00, 8'h40);            // select: DO is ready/busy
            poll_m(16'hDE00, 8'h80, 8'h80, "EEPROM ready after a write");  // bits 6-0 open bus
            wr(16'hDE00, 8'h00);
        end
    endtask
    task ee_read(input [9:0] a, output [15:0] w);
        integer i;
        reg [7:0] v;
        begin
            ee_cmd(3'b110, a);
            for (i = 15; i >= 0; i = i - 1) begin
                wr(16'hDE00, 8'h40); wr(16'hDE00, 8'h60);
                rd(16'hDE00, v); w[i] = v[7];
            end
            wr(16'hDE00, 8'h00);
        end
    endtask

    integer i, k, base;
    reg [7:0] v;
    reg [15:0] c0, c1, w16;
    reg ok;

    initial begin
        for (i = 0; i < 4194304; i = i + 1) rom[i] = pat(i);
        for (i = 0; i < 4194304; i = i + 1) blk[i] = pat(i) ^ 8'h11;
        for (i = 4194304; i < 8388608; i = i + 1) blk[i] = pe(i);
        for (i = 0; i < 65536; i = i + 1) slt[i] = pat(i) ^ 8'h22;
        for (i = 0; i < 65536; i = i + 1) cram[i] = 8'h00;
        repeat (20) @(negedge ui_clk);
        ui_rst = 1'b0;
        sys_wr(P0 + 17'h010, 8'h30);            // interrupts: flash operation, EEPROM written

        // ---- Normal 16K ----
        $display("normal 16K");
        start(8'h01);
        check(cart_active && !exrom && !game, "normal 16K: lines");
        expect_rd(16'h8000, pat(22'h0000), "normal 16K ROML");
        expect_rd(16'h9234, pat(22'h1234), "normal 16K ROML");
        expect_rd(16'hB234, pat(22'h3234), "normal 16K ROMH");
        expect_rd(16'hBFFF, pat(22'h3FFF), "normal 16K ROMH end");
        kill_tgl = ~kill_tgl;
        repeat (16) @(negedge clk);
        check(!cart_active && exrom && game, "normal 16K: KILL");

        // ---- the app's boot cartridge: Normal 8K, $40 to $DFFF ----
        $display("normal 8K, switch-off");
        start(8'h41);
        check(cart_active && !exrom && game, "normal 8K: lines");
        expect_rd(16'h8100, pat(22'h0100), "normal 8K ROML");
        wr(16'hDFFF, 8'h40);
        check(!cart_active && exrom && game, "normal 8K: off after $40 to $DFFF");

        // ---- Ocean ----
        $display("ocean");
        start(8'h08);
        for (k = 0; k < 64; k = k + 7) begin
            wr(16'hDE00, 8'h80 | k);
            expect_rd(16'h9F00 + k, pat(k * 22'h4000 + 22'h1F00 + k), "ocean 8K bank");
        end
        start(8'h69);                           // Ocean with 16K banks: ROMH 8 KB above
        for (k = 0; k < 16; k = k + 5) begin
            wr(16'hDE00, 8'h80 | k);
            expect_rd(16'h8F10, pat(k * 22'h4000 + 22'h0F10), "ocean 16K ROML");
            expect_rd(16'hAF10, pat(k * 22'h4000 + 22'h2F10), "ocean 16K ROMH");
        end

        // ---- Magic Desk ----
        $display("magic desk");
        start(8'h28);
        for (k = 1; k < 128; k = k * 3) begin
            wr(16'hDE00, k);
            expect_rd(16'h8040, pat(k * 22'h4000 + 22'h0040), "magic desk bank");
        end
        wr(16'hDE00, 8'h80);
        check(exrom && game, "magic desk: bit 7 off");

        // ---- EasyFlash ----
        $display("easyflash");
        part = 0; two_chips = 1'b1;
        flash_cfg(16'h0555, 16'h02AA, 16'h07FF, 24'h01_A4_01);
        start(8'h11);
        check(cart_active && exrom && !game, "easyflash boots Ultimax");
        wr(16'hDE02, 8'h07);                    // 16K
        for (k = 0; k < 64; k = k + 9) begin
            wr(16'hDE00, k);
            expect_rd(16'h9E00, pat(k * 22'h4000 + 22'h1E00), "EF ROML");
            expect_rd(16'hBE01, pat(k * 22'h4000 + 22'h3E01), "EF ROMH");
        end
        wr(16'hDF00, 8'h5A); wr(16'hDF7F, 8'hA5);
        expect_rd(16'hDF00, 8'h5A, "EF RAM");
        expect_rd(16'hDF7F, 8'hA5, "EF RAM");
        check(cram[0] == 8'h5A && cram[16'h7F] == 8'hA5, "EF RAM in DDR2");

        wr_count(c0);
        wr(16'hDE02, 8'h05);                    // Ultimax: flash writes
        wr(16'hDE00, 8'd3);
        // autoselect, both chips, unlocks interleaved (EAPIInit)
        wr(16'h8555, 8'hAA); wr(16'hE555, 8'hAA); wr(16'h82AA, 8'h55); wr(16'hE2AA, 8'h55);
        wr(16'h8555, 8'h90);
        expect_rd(16'h8000, 8'h01, "EF autoselect manufacturer");
        expect_rd(16'h8001, 8'hA4, "EF autoselect device");
        expect_rd(16'hE100, pat(3 * 22'h4000 + 22'h2100), "EF ROMH array while ROML autoselects");
        wr(16'hE555, 8'h90);
        expect_rd(16'hE001, 8'hA4, "EF ROMH autoselect device");
        wr(16'h8000, 8'hF0); wr(16'hE000, 8'hF0);
        expect_rd(16'h8001, pat(3 * 22'h4000 + 22'h0001), "EF array after reset command");

        // program a byte whose line is cached
        expect_rd(16'h8123, pat(3 * 22'h4000 + 22'h0123), "EF before program");
        isr_delay = 20000;                      // a slow handler: the C64 sees the busy part
        amd(16'h8555, 16'h82AA, 16'h8555, 8'hA0);
        wr(16'h8123, 8'h5A);
        rd(16'h8123, v); rd(16'hE123, tmp);
        check(v[7] == 1'b1 && tmp == pat(3 * 22'h4000 + 22'h2123), "EF busy: DQ7 = NOT data on ROML, ROMH array");
        rd(16'h8123, tmp);
        check(tmp[7] == 1'b1 && tmp[6] != v[6], "EF busy: DQ6 toggles");
        isr_delay = 0;
        poll(16'h8123, pat(3 * 22'h4000 + 22'h0123) & 8'h5A, "EF program ROML");
        check(rom[3 * 32'h4000 + 32'h0123] == (pat(3 * 22'h4000 + 22'h0123) & 8'h5A), "EF program in DDR2");
        // program ROMH, line not cached
        amd(16'hE555, 16'hE2AA, 16'hE555, 8'hA0);
        wr(16'hF456, 8'h0F);
        poll(16'hF456, pat(3 * 22'h4000 + 22'h3456) & 8'h0F, "EF program ROMH");

        // sector erase: chip 0, sector 1 = banks 8-15; bank 9's line cached first
        wr(16'hDE00, 8'd9);
        expect_rd(16'h8040, pat(9 * 22'h4000 + 22'h0040), "EF bank 9 before erase");
        amd(16'h8555, 16'h82AA, 16'h8555, 8'h80);
        amd(16'h8555, 16'h82AA, 16'h8000, 8'h30);
        poll(16'h8040, 8'hFF, "EF sector erase done (the cached line dropped)");
        ok = 1'b1;
        for (k = 0; k < 64; k = k + 1)
            for (i = 0; i < 16384; i = i + 509) begin
                base = k * 16384 + i;
                if (k >= 8 && k < 16 && i < 8192) begin if (rom[base] !== 8'hFF) ok = 1'b0; end
                else if (rom[base] !== pat(base)) ok = 1'b0;
            end
        check(ok, "EF sector erase: banks 8-15 ROML $FF, the rest kept");
        wr(16'hDE00, 8'd15);
        expect_rd(16'h9FFF, 8'hFF, "EF bank 15 ROML erased");
        expect_rd(16'hFFFE, pat(15 * 22'h4000 + 22'h3FFE), "EF bank 15 ROMH kept");
        wr_count(c1);
        check(c1 - c0 == 3 && flash_ops == 3, "EF: three operations counted and handled");

        // chip erase of chip 1 (ROMH)
        amd(16'hE555, 16'hE2AA, 16'hE555, 8'h80);
        amd(16'hE555, 16'hE2AA, 16'hE555, 8'h10);
        poll(16'hE000, 8'hFF, "EF chip erase done");
        ok = 1'b1;
        for (k = 0; k < 64; k = k + 1)
            for (i = 8192; i < 16384; i = i + 1021) if (rom[k * 16384 + i] !== 8'hFF) ok = 1'b0;
        check(ok && rom[2 * 16384 + 8] == pat(2 * 16384 + 8), "EF chip erase: every ROMH $FF, ROML kept");
        expect_rd(16'hF000, 8'hFF, "EF ROMH reads $FF after the chip erase");

        // ---- MegaByter: bottom boot sector map ----
        $display("megabyter");
        part = 1; two_chips = 1'b0;
        flash_cfg(16'h0AAA, 16'h0555, 16'h0FFF, 24'h02_58_C2);
        start(8'h0F);
        wr(16'hDE02, 8'h03);                    // Ultimax
        wr(16'hDE00, 8'd0);
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h90);
        expect_rd(16'h8000, 8'hC2, "MegaByter autoselect manufacturer");
        expect_rd(16'h8002, 8'h58, "MegaByter autoselect device (byte 2)");
        wr(16'h8000, 8'hF0);
        wr(16'hDE00, 8'd2);                     // the 8 KB boot sector at $4000
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h80);
        amd(16'h8AAA, 16'h8555, 16'h8100, 8'h30);
        poll(16'h8100, 8'hFF, "MegaByter sector erase done");
        check(rom[2 * 16384] == 8'hFF && rom[2 * 16384 + 8191] == 8'hFF &&
              rom[1 * 16384 + 8191] == pat(1 * 16384 + 8191) && rom[3 * 16384] == pat(3 * 16384),
              "MegaByter: only the 8 KB boot sector");

        // ---- C64MegaCart: $DF00 mode, top boot ----
        $display("c64megacart");
        part = 2;
        flash_cfg(16'h0AAA, 16'h0555, 16'h0FFF, 24'h02_D2_01);
        start(8'h68);
        wr(16'hDE00, 8'd77);
        expect_rd(16'h8123, pat(77 * 22'h4000 + 22'h0123), "MegaCart 8K bank");
        wr(16'hDF00, 8'hC0);                    // writes go through $E000 (Ultimax in write cycles only)
        wr(16'hDE00, 8'd0);
        amd(16'hEAAA, 16'hE555, 16'hEAAA, 8'h90);
        wr(16'hDF00, 8'h00);                    // reads: 8K mode, ROML
        expect_rd(16'h8002, 8'hD2, "MegaCart autoselect device");
        wr(16'hDF00, 8'hC0);
        wr(16'hE000, 8'hF0);
        amd(16'hEAAA, 16'hE555, 16'hEAAA, 8'h80);
        wr(16'hEAAA, 8'hAA); wr(16'hE555, 8'h55);
        wr(16'hDE00, 8'hFD);                    // 0x1FA000: the second 8 KB boot sector
        wr(16'hE010, 8'h30);
        wr(16'hDF00, 8'h00);
        poll(16'h8010, 8'hFF, "MegaCart sector erase done");
        check(rom[32'hFD * 16384] == 8'hFF && rom[32'hFD * 16384 + 8191] == 8'hFF &&
              rom[32'hFC * 16384] == pat(32'hFC * 16384) && rom[32'hFE * 16384] == pat(32'hFE * 16384),
              "MegaCart: only the 8 KB boot sector at 0x1FA000");

        // ---- GMod2: 8 KB banks, flash through $E000, EEPROM ----
        $display("gmod2");
        part = 0;
        flash_cfg(16'h5555, 16'h2AAA, 16'h7FFF, 24'h01_A4_01);
        start(8'h48);
        check(!exrom && game, "GMod2 boots 8K");
        for (k = 0; k < 64; k = k + 13) begin
            wr(16'hDE00, k);
            expect_rd(16'h8A00, rom[k * 16384 + 16'h0A00], "GMod2 bank (as the image is now)");
        end
        // program: unlock at chip $5555 (bank 2 $1555) and $2AAA (bank 1 $0AAA), Ultimax
        wr(16'hDE00, 8'hC2); wr(16'hF555, 8'hAA);
        wr(16'hDE00, 8'hC1); wr(16'hEAAA, 8'h55);
        wr(16'hDE00, 8'hC2); wr(16'hF555, 8'hA0);
        wr(16'hDE00, 8'hC5); wr(16'hE077, 8'h81);
        wr(16'hDE00, 8'h05);
        poll(16'h8077, pat(5 * 22'h4000 + 22'h0077) & 8'h81, "GMod2 program, read at $8000");
        check(rom[5 * 16384 + 16'h77] == (pat(5 * 22'h4000 + 22'h0077) & 8'h81), "GMod2 program in DDR2");

        // EEPROM: the app writes word 5, the C64 reads it; the C64 writes word 9
        sys_wr(EEW + 17'h80A, 8'hBE); sys_wr(EEW + 17'h80B, 8'hEF);
        ee_read(10'd5, w16);
        if (w16 != 16'hBEEF) $display("  EEPROM read %h", w16);
        check(w16 == 16'hBEEF, "EEPROM: C64 reads what the app wrote");
        sys_wr(EEW, 8'h00);                     // Gideon's dirty byte: clear
        wr_count(c0);
        k = ee_irqs;
        ee_cmd(3'b100, 10'b11_0000_0000);       // EWEN
        wr(16'hDE00, 8'h00);
        ee_write(10'd9, 16'h1234);
        sys_rd(EEW + 17'h812, v); check(v == 8'h12, "EEPROM: the app reads the C64's write (high)");
        sys_rd(EEW + 17'h813, v); check(v == 8'h34, "EEPROM: the app reads the C64's write (low)");
        sys_rd(EEW, v); check(v != 8'h00, "EEPROM: Gideon's dirty byte set");
        ee_read(10'd9, w16);
        check(w16 == 16'h1234, "EEPROM: C64 reads its own write");
        wr_count(c1);
        check(c1 - c0 == 1 && ee_irqs == k + 1, "EEPROM write: counted, interrupt");

        // ---- T33: what the descriptors changed (T33 §2.3; the rest is equal, sim/cart_equiv) ----
        $display("descriptor changes");
        part = 0; two_chips = 1'b0; linear = 1'b0;
        sys_wr(P4 + 17'h000, 8'h00);            // no flash responder
        cram[0] = 8'h5A; cram[16'h100] = 8'hA5;
        start(8'h28);                           // Magic Desk
        k = rq_count;
        rd(16'hDF00, v); check(v == 8'hFF && rq_count == k, "MD: IO2 read is open bus (D6), no DDR2 read");
        rd(16'hDE00, v); check(v == 8'hFF, "MD: IO1 read is open bus (D1)");
        wr(16'hDE37, 8'd5);                     // every IO1 address is the register (D1)
        expect_rd(16'h8040, pat(5 * 22'h4000 + 22'h0040), "MD: $DE37 switches the bank");
        wr(16'hDEFF, 8'h80);
        check(exrom && game, "MD: $DEFF bit 7 off");
        start(8'h11);                           // EasyFlash: A1 alone decodes (D1)
        wr(16'hDE06, 8'h07);                    // = $DE02
        check(!exrom && !game, "EF: $DE06 is the mode register");
        wr(16'hDE04, 8'd20);                    // = $DE00
        expect_rd(16'h8040, rom[20 * 16384 + 16'h0040], "EF: $DE04 is the bank register");
        expect_rd(16'hDF00, cram[0], "EF: IO2 RAM stays");
        start(8'h48);                           // GMod2: DO only while CS (VICE gmod2.c)
        wr(16'hDE00, 8'h00);
        rd(16'hDE00, v); check(v == 8'hFF, "GMod2: $DE00 read with CS low is open bus");

        // ---- TwoMegabyter (T33 §4.1 item 2) ----
        $display("twomegabyter");
        part = 3; two_chips = 1'b0; linear = 1'b1;
        flash_cfg(16'h0555, 16'h02AA, 16'h07FF, 24'h01_AD_C2);
        start(8'h2F);
        check(cart_active && !exrom && !game, "TMB boots 16K");
        ok = 1'b1;
        for (k = 0; k < 128; k = k + 1) begin
            wr(16'hDE00, k);
            rd(16'h8000 + k, v); if (v !== rom[k * 16384 + k]) ok = 1'b0;
            rd(16'hA000 + 2 * k, v); if (v !== rom[k * 16384 + 8192 + 2 * k]) ok = 1'b0;
        end
        check(ok, "TMB: banks 0-127, ROML and ROMH");
        wr(16'hDE02, 8'h01); check(exrom && !game, "TMB $DE02 = 1: Ultimax");
        wr(16'hDE02, 8'h02); check(!exrom && game, "TMB $DE02 = 2: 8K");
        wr(16'hDE02, 8'h03); check(exrom && game, "TMB $DE02 = 3: off");
        wr(16'hDE03, 8'h00); check(!exrom && !game, "TMB $DE03 = $DE02 (A1)");
        wr(16'hDE01, 8'd17);
        expect_rd(16'h8017, rom[17 * 16384 + 16'h0017], "TMB $DE01 = $DE00 (A1)");
        // autoselect in Ultimax, ID read back in 16K mode (the API's way)
        wr(16'hDE02, 8'h01);
        amd(16'h8555, 16'h82AA, 16'h8555, 8'h90);
        wr(16'hDE02, 8'h00);
        expect_rd(16'h8000, 8'hC2, "TMB autoselect manufacturer");
        expect_rd(16'h8001, 8'hAD, "TMB autoselect device");
        wr(16'h8000, 8'hF0);
        // program ROMH of bank 125 through $F234, poll in 16K mode at $B234
        wr(16'hDE00, 8'd125);
        wr(16'hDE02, 8'h01);
        amd(16'h8555, 16'h82AA, 16'h8555, 8'hA0);
        wr(16'hF234, 8'h3C);
        wr(16'hDE02, 8'h00);
        poll(16'hB234, pat(125 * 22'h4000 + 22'h3234) & 8'h3C, "TMB program, polled in 16K mode");
        check(rom[125 * 16384 + 16'h3234] == (pat(125 * 22'h4000 + 22'h3234) & 8'h3C), "TMB program in DDR2 (ROMH half)");
        // sector erase SA31 ($1F0000 = banks 124-127) the API's way: command at $8555, $30 at the address
        wr(16'hDE00, 8'd124);
        wr(16'hDE02, 8'h01);
        amd(16'h8555, 16'h82AA, 16'h8555, 8'h80);
        amd(16'h8555, 16'h82AA, 16'h8000, 8'h30);
        wr(16'hDE02, 8'h00);
        poll(16'h8000, 8'hFF, "TMB sector erase done");
        ok = 1'b1;
        for (i = 124 * 16384; i < 128 * 16384; i = i + 251) if (rom[i] !== 8'hFF) ok = 1'b0;
        check(ok && rom[123 * 16384 + 16383] == pat(123 * 16384 + 16383), "TMB: banks 124-127 erased, 123 kept");
        wr(16'hDE00, 8'd127);
        expect_rd(16'hBFFF, 8'hFF, "TMB bank 127 ROMH erased");

        // ---- C64MegaCart (T33 §4.1 item 2a) ----
        $display("c64megacart modes");
        part = 2; two_chips = 1'b0; linear = 1'b0;
        flash_cfg(16'h0AAA, 16'h0555, 16'h0FFF, 24'h02_D2_01);
        start(8'h68);
        ok = 1'b1;
        for (k = 0; k < 256; k = k + 1) begin
            wr(16'hDE00 + (k & 8'h3F), k);      // any IO1 address
            rd(16'h8000 + k, v); if (v !== rom[k * 16384 + k]) ok = 1'b0;
        end
        check(ok, "MegaCart: 256 banks, any $DExx address");
        wr(16'hDF00, 8'h00); check(!exrom && game, "MegaCart 00: 8K");
        wr(16'hDF00, 8'h40); check(!exrom && game, "MegaCart 01 after 00: holds 8K");
        wr(16'hDF80, 8'h80); check(exrom && game, "MegaCart 10 ($DF80): off");
        wr(16'hDF00, 8'h40); check(exrom && game, "MegaCart 01 after 10: holds off");
        wr(16'hDFC3, 8'hC0); check(exrom && game, "MegaCart 11 ($DFC3): reads see no cartridge");
        rd(16'hE123, v); check(!last_romh, "MegaCart 11: a read of $E123 is the C64's own");
        wr(16'hDF00, 8'h40); check(exrom && game, "MegaCart 01 after 11: holds");
        k = flash_ops;
        amd(16'hEAAA, 16'hE555, 16'hEAAA, 8'hA0);   // held 11: writes still reach the flash
        wr(16'hE456, 8'h0F);
        check(last_romh, "MegaCart held 11: the write to $E456 is Ultimax");
        wr(16'hDF00, 8'h00);
        poll(16'h8456, pat(32'hFF * 16384 + 16'h0456) & 8'h0F, "MegaCart program through $E000, read at $8000");
        wr(16'hDF00, 8'hC0);
        amd(16'hEAAA, 16'hE555, 16'hEAAA, 8'hA0);
        wr(16'h9333, 8'h00);                    // ROML write: not the flash
        check(last_roml, "MegaCart 11: a write to $9333 is ROML (Ultimax)");
        repeat (200) @(negedge clk);
        check(flash_ops == k + 1 && rom[32'hFF * 16384 + 16'h1333] == pat(32'hFF * 16384 + 16'h1333),
              "MegaCart: the ROML write did not program");
        wr(16'hE000, 8'hFF);                    // the program waits for its data: $FF changes nothing
        wr(16'hDF00, 8'h00);
        poll(16'h8000, pat(32'hFF * 16384), "MegaCart program $FF done");
        k = rq_count;
        rd(16'hDE00, v); check(v == 8'hFF, "MegaCart: IO1 read open bus");
        rd(16'hDF00, v); check(v == 8'hFF && rq_count == k, "MegaCart: IO2 read open bus, no RAM");

        // ---- EasyFlashXl: 256 banks, 2 x 2 MB (PROP-002 §8.3) ----
        $display("easyflash xl");
        part = 3; two_chips = 1'b1; linear = 1'b0;
        flash_cfg(16'h0555, 16'h02AA, 16'h07FF, 24'h01_A4_01);
        start(8'h31);
        check(cart_active && exrom && !game, "EFXL boots Ultimax");
        wr(16'hDE02, 8'h07);
        ok = 1'b1;
        for (k = 0; k < 256; k = k + 3) begin
            wr(16'hDE00, k);
            rd(16'h9E00, v); if (v !== rom[k * 16384 + 16'h1E00]) ok = 1'b0;
            rd(16'hBE01, v); if (v !== rom[k * 16384 + 16'h3E01]) ok = 1'b0;
        end
        check(ok, "EFXL: banks 0-255, ROML and ROMH");
        wr(16'hDE02, 8'h05);
        wr(16'hDE00, 8'd200);
        amd(16'hE555, 16'hE2AA, 16'hE555, 8'h80);  // sector erase, chip 1, sector of bank 200 (200-207)
        amd(16'hE555, 16'hE2AA, 16'hE000, 8'h30);
        poll(16'hE000, 8'hFF, "EFXL sector erase done");
        ok = 1'b1;
        for (k = 200; k < 208; k = k + 1) if (rom[k * 16384 + 8192 + 77] !== 8'hFF) ok = 1'b0;
        check(ok && rom[199 * 16384 + 8192 + 77] == pat(199 * 16384 + 8192 + 77) &&
              rom[200 * 16384 + 77] == pat(200 * 16384 + 77), "EFXL: banks 200-207 ROMH erased, 199 and ROML kept");

        // ---- the descriptor's own rules (T33 §4.1 items 3, 5) ----
        $display("descriptor rules");
        sys_wr(P4 + 17'h000, 8'h00);
        part = 0; two_chips = 1'b0; linear = 1'b0;
        start(8'h08);                           // Ocean
        wr(16'hDE00, 8'd3);
        desc_wr(cart_desc(8'h28));              // a new descriptor without force or reset: nothing changes
        repeat (64) @(negedge clk);
        wr(16'hDE00, 8'h83);                    // Ocean: bank 3 (6 bits); Magic Desk would switch off
        check(!exrom && game, "a written descriptor waits for the force");
        force_tgl = ~force_tgl;
        repeat (16) @(negedge clk);
        check(!cart_active, "force: the cartridge starts off");
        start(8'h28);
        check(cart_active && !exrom && game, "reset takes the new descriptor");
        start_desc(8'h28, 344'd0);              // all zero: not valid
        check(!cart_active && exrom && game, "an empty descriptor: no cartridge");
        rd(16'h8000, v); check(v == 8'hFF, "an empty descriptor: open bus");
        ok = 1'b1;
        for (k = 0; k < 6; k = k + 1) begin     // random bytes, valid: lines and data never X
            start_desc(8'h28, {8'h01, {9{$urandom}}});
            for (i = 0; i < 40; i = i + 1) begin
                bus({$urandom} & 16'hFFFF, $urandom & 1, $urandom, v);
                if (^{exrom, game, cart_active} === 1'bx || (!we && ^v === 1'bx)) ok = 1'b0;
            end
        end
        check(ok, "random descriptors: no X on the lines or the data");

        // ---- T60: where the ROM image is read from (trx_io ROM_SEL) ----
        $display("ROM_SEL: legacy, expansion-port block, built-in slot");
        sys_rd(P4 + 17'h030, v); check(v == 8'h00, "ROM_SEL resets to 0 (Gideon's 0x3C00000)");
        for (i = 0; i < 65536; i = i + 1) rom[i] = pat(i);      // the flash tests erased parts of it
        start(8'h01);
        expect_rd(16'h8123, pat(22'h0123), "ROM_SEL 0: ROML from 0x3C00000");
        expect_rd(16'hB234, pat(22'h3234), "ROM_SEL 0: ROMH from 0x3C00000");
        sys_wr(P4 + 17'h030, 8'hFD); sys_rd(P4 + 17'h030, v);
        check(v == 8'h01, "ROM_SEL keeps bits 1:0 only, reads back as written");
        sys_wr(P4 + 17'h028, 8'h02);            // INVAL: drop the lines of the old image
        start(8'h01);
        expect_rd(16'h8123, pat(22'h0123) ^ 8'h11, "ROM_SEL 1: ROML from 0x1000000");
        expect_rd(16'h9ABC, pat(22'h1ABC) ^ 8'h11, "ROM_SEL 1: ROML bank 0 deeper");
        expect_rd(16'hB234, pat(22'h3234) ^ 8'h11, "ROM_SEL 1: ROMH from 0x1000000");
        sys_wr(P4 + 17'h030, 8'h02);
        sys_wr(P4 + 17'h028, 8'h02);
        start(8'h01);
        expect_rd(16'h8123, pat(22'h0123) ^ 8'h22, "ROM_SEL 2: ROML from 0xF00000");
        expect_rd(16'hB234, pat(22'h3234) ^ 8'h22, "ROM_SEL 2: ROMH from 0xF00000");
        sys_wr(P4 + 17'h030, 8'h03);            // 3 = the legacy base again (reserved value)
        sys_wr(P4 + 17'h028, 8'h02);
        start(8'h01);
        expect_rd(16'h8123, pat(22'h0123), "ROM_SEL 3: legacy base");
        sys_wr(P4 + 17'h030, 8'h00);
        // the lens unit's look follows the same base
        sys_wr(P4 + 17'h030, 8'h00); peek(23'h000123, v); check(v == pat(22'h0123), "look, ROM_SEL 0");
        sys_wr(P4 + 17'h030, 8'h01); peek(23'h3FFFFF, v); check(v == (pat(22'h3FFFFF) ^ 8'h11), "look, ROM_SEL 1, last byte of 4 MB");
        sys_wr(P4 + 17'h030, 8'h02); peek(23'h000123, v); check(v == (pat(22'h0123) ^ 8'h22), "look, ROM_SEL 2");
        sys_wr(P4 + 17'h030, 8'h00);


        // ---- T51: EasyFlash 3 (banking descriptor, 8 MiB, MX29LV640EBT) ----
        $display("easyflash 3");
        sys_wr(P4 + 17'h030, 8'h01);            // ROM_SEL 1: the expansion-port block, 8 MB
        sys_wr(P4 + 17'h028, 8'h02);
        part = 4; two_chips = 1'b0; linear = 1'b0;
        flash_cfg(16'h0AAA, 16'h0555, 16'h0FFF, 24'h02_CB_C2);
        start(8'h12);
        check(cart_active && exrom && !game, "EF3 boots Ultimax (slot 0)");
        expect_rd(16'hE010, pe(23'h002010), "EF3 slot 0 bank 0 ROMH");
        expect_rd(16'h8123, pe(23'h000123), "EF3 slot 0 bank 0 ROML");
        rd(16'hDE01, v); check(v == 8'h00, "EF3 $DE01 reads the slot");
        rd(16'hDE08, v); check(v == 8'hFF, "EF3 $DE08 is open bus (no CPLD version register here)");
        rd(16'hDE00, v); check(v == 8'hFF, "EF3 $DE00 is not readable (open bus)");
        wr(16'hDE01, 8'h03);
        rd(16'hDE01, v); check(v == 8'h03, "EF3 slot 3 reads back");
        wr(16'hDE00, 8'h2A);                    // bank_hi 5, bank_lo 2
        wr(16'hDE02, 8'h07);                    // 16K
        check(!exrom && !game, "EF3 $DE02 = 7: 16K lines");
        expect_rd(16'h8123, pe({3'd3, 3'd5, 3'd2, 1'b0, 13'h0123}), "EF3 slot 3 bank $2A ROML");
        expect_rd(16'hA045, pe({3'd3, 3'd5, 3'd2, 1'b1, 13'h0045}), "EF3 slot 3 bank $2A ROMH");
        wr(16'hDE02, 8'h04);                    // M = 1, GAME 0, EXROM 0: off
        check(exrom && game, "EF3 $DE02 = 4: lines off");
        wr(16'hDE02, 8'h07);
        wr(16'hDF10, 8'h77);                    // IO2 RAM
        rd(16'hDF10, v); check(v == 8'h77, "EF3 IO2 RAM");
        // autoselect, the EAPI way: unlock at slot 0 bank 0 in Ultimax
        wr(16'hDE02, 8'h05); wr(16'hDE01, 8'h00); wr(16'hDE00, 8'h00);
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h90);
        rd(16'h8000, v); check(v == 8'hC2, "EF3 autoselect manufacturer $C2");
        rd(16'h8002, v); check(v == 8'hCB, "EF3 autoselect device $CB");
        wr(16'h8000, 8'hF0);
        expect_rd(16'h8123, pe(23'h000123), "EF3 reset command leaves autoselect");
        // program: unlock in slot 0 bank 0, then slot 2 bank $13 and the data cycle
        blk[{3'd2, 3'd2, 3'd3, 1'b0, 13'h0123}] = 8'hFF;
        blk[{3'd2, 3'd2, 3'd3, 1'b1, 13'h0123}] = 8'hFF;
        sys_wr(P4 + 17'h028, 8'h02);
        k = flash_ops;
        wr(16'h8AAA, 8'hAA); wr(16'h8555, 8'h55); wr(16'h8AAA, 8'hA0);
        wr(16'hDE01, 8'h02); wr(16'hDE00, 8'h13);
        wr(16'h8123, 8'h5A);
        poll(16'h8123, 8'h5A, "EF3 program ROML done");
        check(flash_ops == k + 1 && blk[{3'd2, 3'd2, 3'd3, 1'b0, 13'h0123}] == 8'h5A, "EF3 program lands at {slot, hi, lo, ROML}");
        wr(16'hDE01, 8'h00); wr(16'hDE00, 8'h00);
        wr(16'h8AAA, 8'hAA); wr(16'h8555, 8'h55); wr(16'h8AAA, 8'hA0);
        wr(16'hDE01, 8'h02); wr(16'hDE00, 8'h13);
        wr(16'hE123, 8'hA5);
        poll(16'hE123, 8'hA5, "EF3 program ROMH done");
        check(blk[{3'd2, 3'd2, 3'd3, 1'b1, 13'h0123}] == 8'hA5, "EF3 program lands at {slot, hi, lo, ROMH}");
        // 64 KiB sector erase of slot 1, bank_hi 1, ROML (banks 8-15 of the slot, ROML halves)
        wr(16'hDE01, 8'h00); wr(16'hDE00, 8'h00);
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h80);
        wr(16'h8AAA, 8'hAA); wr(16'h8555, 8'h55);
        wr(16'hDE01, 8'h01); wr(16'hDE00, 8'h08);
        wr(16'h8000, 8'h30);
        poll(16'h8000, 8'hFF, "EF3 sector erase done");
        ok = 1'b1;
        for (k = 0; k < 8; k = k + 1) begin
            if (blk[{3'd1, 3'd1, k[2:0], 1'b0, 13'h0077}] !== 8'hFF || blk[{3'd1, 3'd1, k[2:0], 1'b0, 13'h1FFF}] !== 8'hFF) ok = 1'b0;
            if (blk[{3'd1, 3'd1, k[2:0], 1'b1, 13'h0077}] !== pe({3'd1, 3'd1, k[2:0], 1'b1, 13'h0077})) ok = 1'b0;
        end
        check(ok, "EF3 64K erase: 8 ROML banks erased, their ROMH kept");
        check(blk[{3'd1, 3'd0, 3'd7, 1'b0, 13'h0077}] === pe({3'd1, 3'd0, 3'd7, 1'b0, 13'h0077}) &&
              blk[{3'd1, 3'd2, 3'd0, 1'b0, 13'h0077}] === pe({3'd1, 3'd2, 3'd0, 1'b0, 13'h0077}) &&
              blk[{3'd0, 3'd1, 3'd0, 1'b0, 13'h0077}] === pe({3'd0, 3'd1, 3'd0, 1'b0, 13'h0077}), "EF3 64K erase: the neighbours are kept");
        // 8 KiB boot sector: slot 0, bank 3, ROML only
        wr(16'hDE01, 8'h00); wr(16'hDE00, 8'h00);
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h80);
        wr(16'h8AAA, 8'hAA); wr(16'h8555, 8'h55);
        wr(16'hDE00, 8'h03);
        wr(16'h8000, 8'h30);
        poll(16'h8000, 8'hFF, "EF3 boot sector erase done");
        check(blk[{3'd0, 3'd0, 3'd3, 1'b0, 13'h0077}] === 8'hFF && blk[{3'd0, 3'd0, 3'd3, 1'b0, 13'h1FFF}] === 8'hFF, "EF3 boot sector: bank 3 ROML erased");
        check(blk[{3'd0, 3'd0, 3'd2, 1'b0, 13'h0077}] === pe({3'd0, 3'd0, 3'd2, 1'b0, 13'h0077}) &&
              blk[{3'd0, 3'd0, 3'd4, 1'b0, 13'h0077}] === pe({3'd0, 3'd0, 3'd4, 1'b0, 13'h0077}) &&
              blk[{3'd0, 3'd0, 3'd3, 1'b1, 13'h0077}] === pe({3'd0, 3'd0, 3'd3, 1'b1, 13'h0077}), "EF3 boot sector: 8 KiB only");
        // chip erase: decoded, handed to the app, which erases the whole 8 MB image
        wr(16'hDE00, 8'h00);
        k = chip_erases;
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h80);
        amd(16'h8AAA, 16'h8555, 16'h8AAA, 8'h10);
        poll(16'h8000, 8'hFF, "EF3 chip erase done");
        check(chip_erases == k + 1, "EF3 chip erase reached the app");
        check(blk[{3'd2, 3'd2, 3'd3, 1'b0, 13'h0123}] === 8'hFF, "EF3 chip erase: the programmed byte of slot 2 is erased");
        check(blk[{3'd0, 3'd0, 3'd0, 1'b0, 13'h0000}] === 8'hFF && blk[{3'd0, 3'd1, 3'd0, 1'b1, 13'h1000}] === 8'hFF, "EF3 chip erase: the system area is erased");
        check(blk[{3'd7, 3'd7, 3'd7, 1'b1, 13'h1FFF}] === 8'hFF && blk[{3'd5, 3'd4, 3'd1, 1'b0, 13'h0777}] === 8'hFF, "EF3 chip erase: other slots erased too");
        expect_rd(16'h8000, 8'hFF, "EF3 reads $FF after the chip erase");
        wr(16'hDE01, 8'h05); wr(16'hDE00, 8'h1B);
        expect_rd(16'h8123, 8'hFF, "EF3 slot 5 reads $FF after the chip erase");
        expect_rd(16'hA123, 8'hFF, "EF3 slot 5 ROMH reads $FF after the chip erase");
        for (i = 4194304; i < 8388608; i = i + 1) blk[i] = pe(i);   // put the image back for the rest (the chip erase wiped it)
        for (i = 0; i < 4194304; i = i + 1) blk[i] = pat(i) ^ 8'h11;
        // the lens look at the upper half of the image
        peek(24'h800123, v); check(v == pe(23'h400123), "look: image offset bit 22 (b23 of the request)");
        // no mode register, no KERNAL mode, no reset request: $DE0E/$DE0F do nothing, the bank stays
        start(8'h12);
        wr(16'hDE01, 8'h03); wr(16'hDE00, 8'h05);
        wr(16'hDE0F, 8'h02); wr(16'hDE0E, 8'h07);
        repeat (40) @(negedge clk);
        rd(16'hDE01, v); check(v == 8'h03, "EF3: $DE0F is no register");
        expect_rd(16'hE077, pe({3'd3, 3'd0, 3'd5, 1'b1, 13'h0077}), "EF3: $DE0E/$DE0F changed nothing");
        // the slot after a reset is the descriptor's R2 reset value (slot 0 in the app's descriptor)
        start(8'h12);
        rd(16'hDE01, v); check(v == 8'h00, "EF3 reset: slot 0");
        wr(16'hDE01, 8'h04);
        start(8'h12);
        rd(16'hDE01, v); check(v == 8'h00, "EF3 reset: slot 0 again");
        desc_a = cart_desc(8'h12);
        desc_a[38*8 +: 8] = 8'h05;              // R2_INIT = 5: the app's choice of the start slot
        start_desc(8'h12, desc_a);
        rd(16'hDE01, v); check(v == 8'h05, "EF3 R2_INIT: slot 5 after a reset");
        expect_rd(16'hE010, pe({3'd5, 3'd0, 3'd0, 1'b1, 13'h0010}), "EF3 R2_INIT: boots slot 5 bank 0 ROMH");
        sys_wr(P4 + 17'h000, 8'h00);
        sys_wr(P4 + 17'h030, 8'h00);
        sys_wr(P4 + 17'h028, 8'h02);
        part = 0;

        sys_rd(P0, v); check(v == "T", "ID");
        sys_rd(P0 + 17'h014, v); check(v == 8'h00 && !cw_irq, "no cause left");
        $display("%0d C64 cycles, %0d held for the cache, %0d MIG writes, %0d flash operations",
                 cycles, held, mig_writes, flash_ops);
        $display("%0d checks, %0d failed", checks, fails);
        if (fails) $display("FAILED"); else $display("PASS");
        $finish;
    end

    initial begin
        #400_000_000;
        $display("TIMEOUT"); $display("FAILED");
        $finish;
    end
endmodule
