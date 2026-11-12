// T90: the small pieces of the C264 core that need no TED: the clock enable (S3), the key translation (S8), the sound
// averager (S7), the DMA window and memories (S4, S10), the register block (S10, S11). One bench, many checks; PASS or FAIL.
`timescale 1ns / 1ps
module tb_c264_glue;
    reg sys_clk = 0, cpu_clk = 0, mclk = 0;
    always #15.859 sys_clk = ~sys_clk;
    always #5 cpu_clk = ~cpu_clk;
    always #40.69 mclk = ~mclk;
    integer fails = 0;
    task check(input ok, input [8*60-1:0] what);
        begin
            if (ok) $display("ok   %0s", what);
            else begin $display("FAIL %0s", what); fails = fails + 1; end
        end
    endtask

    // ------------------------------------------------------------ S3: the enable
    reg hold = 0;
    wire en;
    c264_nco nco (.clk(sys_clk), .hold(hold), .en(en));
    integer en_cnt = 0, clk_cnt = 0, run = 0;
    always @(posedge sys_clk) if (run) begin clk_cnt <= clk_cnt + 1; if (en) en_cnt <= en_cnt + 1; end

    // ------------------------------------------------------------ S8: the key translation
    reg  [63:0] keys = 0;
    wire [63:0] k264;
    c264_keys keyx (.keys(keys), .k264(k264));
    reg [127:0] vec [0:255];

    // ------------------------------------------------------------ S7: the sound
    reg signed [15:0] a_in = 0;
    reg sq_run = 0;
    always begin #500000; if (sq_run) a_in = (a_in > 0) ? -16'sd4000 : 16'sd4000; end
    reg mute = 0;
    wire signed [23:0] al, ar;
    c264_audio aud (.clk(sys_clk), .a_in(a_in), .mute(mute), .mclk(mclk), .audio_l(al), .audio_r(ar));

    // ------------------------------------------------------------ S4/S10: the window and the memories
    reg dma_req_tgl = 0, dma_we = 0, dma_rom = 0, dbg_busy = 0;
    reg [15:0] dma_addr = 0;
    reg [7:0] dma_wdata = 0;
    wire dma_done_tgl;
    wire [7:0] dma_rdata;
    reg [2:0] rom_sel = 0;
    reg size64 = 1;
    wire port_use, port_rom, port_we, w_wr, w_hit, w_rom;
    wire [15:0] port_addr, w_ai;
    wire [7:0] port_wdata, w_di, w_do;
    wire [2:0] w_sel;
    reg  j_req = 0, j_rom = 0, j_we = 0;
    reg  [15:0] j_addr = 0;
    reg  [7:0] j_wd = 0;
    wire p_req = port_use | j_req;
    wire [15:0] p_addr = j_req ? j_addr : port_addr;
    wire p_rom = j_req ? j_rom : port_rom;
    wire p_we = j_req ? j_we : port_we;
    wire [7:0] p_wd = j_req ? j_wd : port_wdata;
    c264_win win (.clk(sys_clk), .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata),
        .dma_rom(dma_rom), .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata), .dbg_busy(dbg_busy | j_req), .rom_sel(rom_sel),
        .port_use(port_use), .port_addr(port_addr), .port_rom(port_rom), .port_we(port_we), .port_wdata(port_wdata),
        .port_rdata(w_do), .p_req(p_req), .p_addr(p_addr), .p_rom(p_rom), .p_we(p_we), .p_wdata(p_wd),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di));
    reg [15:0] m_addr = 0;
    reg [7:0] m_wdata = 0;
    reg m_we = 0;
    wire [7:0] ram_q, kern_q, basic_q, flo_q, fhi_q, clo_q, chi_q;
    c264_mem mem (.clk(sys_clk), .m_addr(m_addr), .m_wdata(m_wdata), .m_we(m_we), .size64(size64),
        .ram_q(ram_q), .kern_q(kern_q), .basic_q(basic_q), .flo_q(flo_q), .fhi_q(fhi_q), .clo_q(clo_q), .chi_q(chi_q),
        .w_wr(w_wr), .w_hit(w_hit), .w_rom(w_rom), .w_sel(w_sel), .w_ai(w_ai), .w_di(w_di), .w_do(w_do));

    task dma(input rom, input we, input [15:0] a, input [7:0] d, output [7:0] r);
        reg t;
        begin
            t = dma_done_tgl;
            @(posedge sys_clk);
            dma_rom <= rom; dma_we <= we; dma_addr <= a; dma_wdata <= d;
            dma_req_tgl <= ~dma_req_tgl;
            wait (dma_done_tgl != t);
            @(posedge sys_clk);
            r = dma_rdata;
        end
    endtask
    task jtag(input rom, input we, input [15:0] a, input [7:0] d, output [7:0] r);
        begin
            @(posedge sys_clk);
            j_req <= 1; j_rom <= rom; j_we <= we; j_addr <= a; j_wd <= d;
            @(posedge sys_clk);
            j_req <= 0; j_we <= 0;
            @(posedge sys_clk);
            r = w_do;
        end
    endtask

    // ------------------------------------------------------------ S10/S11: the registers
    reg rg_we = 0, rg_re = 0;
    reg [16:0] rg_addr = 0;
    reg [7:0] rg_wd = 0;
    wire [7:0] rg_rd;
    wire rg_ack, rg_irq, px_we, px_re;
    reg ev_vsync = 0, ev_flip = 0, ev_frame = 0;
    wire [7:0] ctrl;
    wire model, ram64, jswap;
    wire [2:0] rsel;
    wire [1:0] cart;
    wire [31:0] fbb, fbs;
    c264_regs regs (.clk(cpu_clk), .we(rg_we), .re(rg_re), .addr(rg_addr), .wdata(rg_wd), .rdata(rg_rd), .ack(rg_ack), .irq(rg_irq),
        .build(32'h12345678), .ddr_cal(64'h0123456789ABCDEF), .px_we(px_we), .px_re(px_re), .px_rdata(8'h5A),
        .ev_vsync(ev_vsync), .ev_flip(ev_flip), .ev_frame(ev_frame),
        .ctrl(ctrl), .model(model), .ram64(ram64), .joy_swap(jswap), .rom_sel(rsel), .cart(cart), .fb_base(fbb), .fb_stride(fbs),
        .status(8'h02), .frames(16'h1234), .cpuaddr(16'hE000), .fb_latest(8'h82), .fb_seq(8'h07), .fb_over(16'h0003));
    task rwr(input [16:0] a, input [7:0] d);
        begin @(posedge cpu_clk); rg_addr <= a; rg_wd <= d; rg_we <= 1; @(posedge cpu_clk); rg_we <= 0; end
    endtask
    task rrd(input [16:0] a, output [7:0] d);
        begin @(posedge cpu_clk); rg_addr <= a; rg_re <= 1; @(posedge cpu_clk); rg_re <= 0; #1 d = rg_rd; end
    endtask

    integer i, n, ok_all, f;
    reg [7:0] r, r2;
    reg [63:0] vin, vexp;
    integer zc, last, t0, t1, rise;
    integer samples, nz, maxabs;
    reg signed [23:0] prev;
    initial begin
        // ---- S3
        @(negedge sys_clk); run = 1;
        repeat (632320) @(negedge sys_clk);
        run = 0;
        check(clk_cnt == 632320, "S3 one PAL frame is 632,320 sys_clk");
        check(en_cnt == 569088, "S3 the TED sees 569,088 clocks in that frame (456 x 4 x 312)");
        // the pause freezes the phase: the count of enables after a hold equals the count without
        hold = 1; repeat (37) @(posedge sys_clk);
        check(en == 0, "S6 no enable while held");
        hold = 0;
        en_cnt = 0; clk_cnt = 0; @(negedge sys_clk); run = 1; repeat (1000) @(negedge sys_clk); run = 0;
        check(en_cnt == 900, "S3 nine in ten again after the hold");

        // ---- S8: every key of the translation, by name (keymap.py vectors), and the cases with SHIFT and the cursors
        $readmemh("keyvec.hex", vec);
        ok_all = 1; n = 0;
        for (i = 0; i < 193; i = i + 1) begin
            vin = vec[i][127:64]; vexp = vec[i][63:0];
            keys = vin; #1;
            if (k264 !== vexp) begin
                ok_all = 0;
                $display("     key vector %0d: keys %h gives %h, expected %h", i, vin, k264, vexp);
            end
            n = n + 1;
        end
        check(ok_all == 1 && n == 193, "S8 all 193 key vectors translate as the table says");
        keys = 0;

        // ---- S7: a square wave of 1 kHz (+-4000) comes out at 1 kHz with the right level, a constant level goes to 0, mute is 0
        a_in = 0;
        repeat (60000) @(posedge sys_clk);
        check(al == 0, "S7 silence is exactly 0");
        a_in = 16'sd4000;
        sq_run = 1;
        #3000000;                                  // settle (3 ms)
        zc = 0; maxabs = 0; prev = al; t0 = $time;
        while ($time - t0 < 20000000) begin       // 20 ms
            @(posedge mclk);
            if ((al > 0) != (prev > 0) && al != prev) zc = zc + 1;
            if (al > maxabs) maxabs = al;
            if (-al > maxabs) maxabs = -al;
            prev = al;
        end
        sq_run = 0;
        $display("     zero crossings in 20 ms: %0d, peak %0d", zc, maxabs);
        check(zc >= 38 && zc <= 42, "S7 the tone is at 1 kHz (40 crossings in 20 ms)");
        check(maxabs > 3500000 && maxabs < 5000000, "S7 the level is +-4000 x 1024 (peak 4.1 M of 8.4 M)");
        a_in = 16'sd5000;                          // a constant level: the DC tracker takes it out
        #150000000;
        check(al > -2000 && al < 2000, "S7 a constant level decays to 0");
        mute = 1; a_in = 16'sd8000;
        repeat (3000) @(posedge sys_clk);
        check(al == 0 && ar == 0, "S7 muted is exactly 0");
        mute = 0; a_in = 0;
        repeat (20) begin repeat (700) @(posedge sys_clk); end

        // ---- S4/S10: window and memories
        size64 = 1;
        dma(0, 1, 16'h1001, 8'hA5, r);
        dma(0, 0, 16'h1001, 8'h00, r);
        check(r == 8'hA5, "S4 RAM byte written and read back through the app window");
        @(posedge sys_clk); m_addr <= 16'h1001; repeat (3) @(posedge sys_clk);
        check(ram_q == 8'hA5, "S4 the machine port sees the byte the window wrote");
        @(posedge sys_clk); m_addr <= 16'h2000; m_wdata <= 8'h3C; m_we <= 1; @(posedge sys_clk); m_we <= 0; repeat (2) @(posedge sys_clk);
        dma(0, 0, 16'h2000, 8'h00, r);
        check(r == 8'h3C, "S4 the byte the machine wrote is seen by the window");
        // 16 KB mirrored
        size64 = 0;
        @(posedge sys_clk); m_addr <= 16'h0001; m_wdata <= 8'h77; m_we <= 1; @(posedge sys_clk); m_we <= 0; repeat (2) @(posedge sys_clk);
        m_addr <= 16'h8001; repeat (3) @(posedge sys_clk);
        check(ram_q == 8'h77, "S4 16 KB RAM mirrors through the 64 KB space ($8001 = $0001)");
        dma(0, 0, 16'hC001, 8'h00, r);
        check(r == 8'h77, "S4 the window sees the same mirror");
        size64 = 1;
        // the six ROM arrays through the one 16 KB aperture: dma_addr 0x4000 + o (o < 0x2000), 0x2000 + (o - 0x2000)
        for (i = 0; i < 6; i = i + 1) begin
            rom_sel = i;
            repeat (4) @(posedge sys_clk);
            dma(1, 1, 16'h4000 + 16'h0010, 8'h10 + i, r);                 // aperture offset 0x0010
            dma(1, 1, 16'h2000 + 16'h1FF0, 8'h20 + i, r);                 // aperture offset 0x3FF0
        end
        ok_all = 1;
        for (i = 0; i < 6; i = i + 1) begin
            rom_sel = i;
            repeat (4) @(posedge sys_clk);
            dma(1, 0, 16'h4010, 8'h00, r);  if (r != 8'h10 + i) ok_all = 0;
            dma(1, 0, 16'h3FF0, 8'h00, r);  if (r != 8'h20 + i) ok_all = 0;
        end
        check(ok_all == 1, "S4 the six ROM arrays hold their own bytes behind the one aperture");
        @(posedge sys_clk); m_addr <= 16'h0010; repeat (3) @(posedge sys_clk);
        check(kern_q == 8'h10 && basic_q == 8'h11 && flo_q == 8'h12 && fhi_q == 8'h13 && clo_q == 8'h14 && chi_q == 8'h15,
              "S4 the machine reads each ROM array at its address");
        @(posedge sys_clk); m_addr <= 16'h3FF0; repeat (3) @(posedge sys_clk);
        check(kern_q == 8'h20 && chi_q == 8'h25, "S4 the top of each array (offset 0x3FF0)");
        // absent apertures: the host's char (0x0000) and colour RAM (0x6000) areas, and an array that does not exist
        rom_sel = 0;
        dma(1, 1, 16'h0010, 8'hEE, r);
        dma(1, 0, 16'h0010, 8'h00, r);
        check(r == 8'h00, "S10 the char aperture reads 0 and takes nothing");
        dma(1, 1, 16'h6010, 8'hEE, r);
        dma(1, 0, 16'h6010, 8'h00, r);
        check(r == 8'h00, "S10 the colour RAM aperture reads 0 and takes nothing");
        rom_sel = 7;
        repeat (4) @(posedge sys_clk);
        dma(1, 1, 16'h4010, 8'hEE, r);
        dma(1, 0, 16'h4010, 8'h00, r);
        check(r == 8'h00, "S10 an array that is not there (rom_sel 7) reads 0");
        rom_sel = 0; repeat (4) @(posedge sys_clk);
        dma(1, 0, 16'h4010, 8'h00, r);
        check(r == 8'h10, "S4 and the KERNAL byte is still there");
        // the JTAG port reaches the same memory
        jtag(0, 1, 16'h3000, 8'h99, r);
        jtag(0, 0, 16'h3000, 8'h00, r);
        check(r == 8'h99, "S10 the JTAG port writes and reads the RAM");
        jtag(1, 0, 16'h4010, 8'h00, r);
        check(r == 8'h10, "S10 the JTAG port reads the ROM aperture");

        // ---- registers
        rrd(17'h000, r); rrd(17'h001, r2);
        check(r == "C" && r2 == "2", "S11 the identity block reads C2..");
        rrd(17'h002, r); rrd(17'h003, r2);
        check(r == "6" && r2 == "4", "S11 ..64");
        rrd(17'h004, r); check(r == 8'hC0, "S11 CAPS");
        rrd(17'h008, r); rrd(17'h00B, r2); check(r == 8'h78 && r2 == 8'h12, "S11 BUILD");
        rwr(17'h0A1, 8'h01); rrd(17'h0A1, r); check(r == 8'h01 && model == 1, "S11 MODEL");
        rwr(17'h0A2, 8'h01); rrd(17'h0A2, r); check(r == 8'h01 && ram64 == 1, "S11 RAM_SIZE");
        rwr(17'h0B0, 8'h05); rrd(17'h0B0, r); check(r == 8'h05 && rsel == 5, "S11 ROM_SEL");
        rwr(17'h0B1, 8'h03); @(posedge cpu_clk); #1 check(cart == 3, "S11 CART");
        rwr(17'h0C4, 8'h00); rwr(17'h0C5, 8'h00); rwr(17'h0C6, 8'hD0); rwr(17'h0C7, 8'h01);
        check(fbb == 32'h01D00000, "S11 FB_BASE");
        rrd(17'h0C0, r); check(r == 8'h82, "S11 FB_LATEST");
        rrd(17'h0C1, r); check(r == 8'h07, "S11 FRAME_SEQ");
        rrd(17'h0A8, r); rrd(17'h0A9, r2); check(r == 8'h34 && r2 == 8'h12, "S11 FRAMES");
        rrd(17'h0A4, r); check(r == 8'h02, "S11 STATUS");
        rrd(17'h6084, r); check(r == 8'h5A && 1, "S10 page 6 is forwarded to the pixel layer");
        rrd(17'h0F0, r); check(r == 8'h00, "S10 an unknown register reads 0");
        rrd(17'h1_0000, r); check(r == 8'h00, "S10 the EEPROM window reads 0");
        rwr(17'h010, 8'h20);
        @(posedge cpu_clk); ev_frame <= 1; @(posedge cpu_clk); ev_frame <= 0; repeat (2) @(posedge cpu_clk);
        check(rg_irq == 1, "S11 a complete frame raises the interrupt");
        rwr(17'h014, 8'h20); repeat (2) @(posedge cpu_clk);
        check(rg_irq == 0, "S11 and the cause clears");

        if (fails == 0) $display("PASS tb_c264_glue"); else $display("FAILED tb_c264_glue: %0d", fails);
        $finish;
    end
endmodule
