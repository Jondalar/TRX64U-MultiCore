// T123 S1: the null core (layer2_null.v with null_regs.v) driven through its I/O window the way the host boot app uses it:
// hostapp.cc (ID, CAPS, CAPS2, BUILD), trx_io.cc (the shared interrupt: IRQ_EN, IRQ_CAUSE write-1-to-clear), trx_lv_port.cc (the pixel
// layer, page 6) - and the host's pix_layer, which here is a stub answering page 6 one clock after the address, as pix_layer.v does.
// The window's protocol (host/ultimate_sys.vhd, trx_io.v): re/we for one cpu_clk cycle with the address, the answer (ack, rdata) in the
// next cycle. Prints one "PASS null_regs" line when every check held, else FAIL lines.
`timescale 1ns / 1ps
module tb_null;
    reg cpu_clk = 0, sys_clk = 0, ui_clk = 0, mclk = 0, c64_gclk = 0, sid2x_clk = 0, adc_bclk = 0;
    always #5.0 cpu_clk = ~cpu_clk;
    always #3.9 sys_clk = ~sys_clk;
    always #6.5 ui_clk = ~ui_clk;
    always #8.5 mclk = ~mclk;
    always #9.5 c64_gclk = ~c64_gclk;
    always #5.5 sid2x_clk = ~sid2x_clk;
    always #11.5 adc_bclk = ~adc_bclk;

    reg         cw_we = 0, cw_re = 0;
    reg  [16:0] cw_addr = 0;
    reg   [7:0] cw_wdata = 0;
    wire  [7:0] cw_rdata;
    wire        cw_ack, cw_irq;
    reg  [31:0] usr_access = 32'h08872197;
    wire        px_we, px_re;
    reg   [7:0] px_rdata = 0;
    reg         px_ev_flip = 0, px_ev_vsync = 0;
    reg         drv_io_rd = 0, drv_io_wr = 0, gcr_io_rd = 0, gcr_io_wr = 0;
    wire        drv_io_ack, gcr_io_ack;
    wire  [3:0] pix_color;
    wire  [9:0] pix_x;
    wire  [8:0] pix_y;
    wire        c64_ce, c64_halted, trx_j_rvalid, c_en, e_en, ap_req, gate_frz, slot_io1, slot_io2, slot_romh, slot_roml, slot_rw;
    wire        drv_io_nc, gcr_io_nc, win_port_use, sid_sock1_n;
    wire [23:0] audio_l;

    layer2_null dut (
        .cpu_clk(cpu_clk), .sys_clk(sys_clk), .ui_clk(ui_clk), .mclk(mclk), .c64_gclk(c64_gclk), .sid2x_clk(sid2x_clk), .adc_bclk(adc_bclk),
        .cw_we(cw_we), .cw_re(cw_re), .cw_addr(cw_addr), .cw_wdata(cw_wdata), .cw_rdata(cw_rdata), .cw_ack(cw_ack), .cw_irq(cw_irq),
        .usr_access(usr_access), .px_we(px_we), .px_re(px_re), .px_rdata(px_rdata), .px_ev_flip(px_ev_flip), .px_ev_vsync(px_ev_vsync),
        .drv_io_rd(drv_io_rd), .drv_io_wr(drv_io_wr), .drv_io_ack(drv_io_ack),
        .gcr_io_rd(gcr_io_rd), .gcr_io_wr(gcr_io_wr), .gcr_io_ack(gcr_io_ack),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y),
        .c64_ce(c64_ce), .c64_halted(c64_halted), .trx_j_rvalid(trx_j_rvalid), .c_en(c_en), .e_en(e_en), .ap_req(ap_req),
        .gate_frz(gate_frz), .SLOT_IO1n(slot_io1), .SLOT_IO2n(slot_io2), .SLOT_ROMHn(slot_romh), .SLOT_ROMLn(slot_roml), .slot_rw(slot_rw),
        .drv_io_nc(drv_io_nc), .gcr_io_nc(gcr_io_nc), .win_port_use(win_port_use), .sid_sock1_n(sid_sock1_n), .audio_l(audio_l)
    );

    // the host's pixel layer: page 6 read data one clock after the address
    always @(posedge cpu_clk) px_rdata <= px_re ? (cw_addr[7:0] ^ 8'hA5) : 8'h00;

    integer errors = 0, checks = 0;
    task chk(input cond, input [8*48-1:0] what);
        begin
            checks = checks + 1;
            if (!cond) begin
                errors = errors + 1;
                $display("FAIL %0s (t=%0t)", what, $time);
            end
        end
    endtask

    reg        got_ack, got_ack2;
    reg  [7:0] got;
    reg        px_re_seen, px_we_seen;
    // one read as the window does it: re for one cycle, the answer in the next
    task rd(input [16:0] a);
        begin
            @(negedge cpu_clk); cw_addr = a; cw_re = 1;
            #1 px_re_seen = px_re; px_we_seen = px_we;
            @(negedge cpu_clk); cw_re = 0;
            got_ack = cw_ack; got = cw_rdata;
            @(negedge cpu_clk);
            got_ack2 = cw_ack;
        end
    endtask
    task wr(input [16:0] a, input [7:0] d);
        begin
            @(negedge cpu_clk); cw_addr = a; cw_wdata = d; cw_we = 1;
            #1 px_re_seen = px_re; px_we_seen = px_we;
            @(negedge cpu_clk); cw_we = 0;
            got_ack = cw_ack;
            @(negedge cpu_clk);
            got_ack2 = cw_ack;
        end
    endtask

    integer i, k;
    reg [31:0] bld;
    reg [16:0] a;
    integer t1;
    integer maxx, maxy;
    reg [3:0] anycol;
    integer tg_ui, tg_mclk, tg_gclk, tg_sid2, tg_adc;
    reg q_ui, q_mclk, q_gclk, q_sid2, q_adc;

    // the register loads of the clocks of the partition really toggle
    initial begin tg_ui = 0; tg_mclk = 0; tg_gclk = 0; tg_sid2 = 0; tg_adc = 0; end
    always @(dut.ld_ui.t)   tg_ui   = tg_ui + 1;
    always @(dut.ld_mclk.t) tg_mclk = tg_mclk + 1;
    always @(dut.ld_gclk.t) tg_gclk = tg_gclk + 1;
    always @(dut.ld_sid2.t) tg_sid2 = tg_sid2 + 1;
    always @(dut.ld_adc.t)  tg_adc  = tg_adc + 1;

    initial begin
        repeat (4) @(negedge cpu_clk);

        // ---- the identity the boot app reads: trx_io_caps() and running_host_key() (hostapp.cc)
        rd(17'h00000); chk(got == "T" && got_ack, "ID byte 0");
        rd(17'h00001); chk(got == "R" && got_ack, "ID byte 1");
        rd(17'h00002); chk(got == "X" && got_ack, "ID byte 2");
        rd(17'h00003); chk(got == "2" && got_ack, "ID byte 3");
        rd(17'h00004); chk((got & 8'hC0) == 8'hC0 && got_ack, "CAPS has VIDEO and PX565 (trx_lv_available)");
        rd(17'h00005); chk((got & 8'h02) == 8'h02 && got_ack, "CAPS2 bit 1: BUILD is there");
        rd(17'h00006); chk(got == 8'h00 && got_ack, "CAPS3 reads 0");
        bld = 0;
        for (i = 0; i < 4; i = i + 1) begin
            rd(17'h00008 + i); bld = bld | (got << (8 * i));
            chk(got_ack, "BUILD byte acknowledged");
        end
        chk(bld == 32'h08872197, "BUILD = the usr_access word (host key)");
        usr_access = 32'hDEADBEEF; rd(17'h00008); chk(got == 8'hEF, "BUILD follows usr_access");

        // ---- the answer is one cycle after the request, for one cycle
        chk(got_ack2 == 1'b0, "ack lasts one cycle");
        @(negedge cpu_clk); @(negedge cpu_clk);
        chk(cw_ack == 1'b0, "no ack without a request");

        // ---- every other address of the window is acknowledged and reads 0 (no bus timeout): all 16 pages but 6 at three offsets
        for (i = 0; i < 16; i = i + 1)
            for (k = 0; k < 3; k = k + 1) begin
                a = {1'b0, i[3:0], (k == 0 ? 12'h100 : k == 1 ? 12'h7F0 : 12'hFFF)};
                if (i != 6) begin
                    rd(a);
                    chk(got_ack, "unmapped read acknowledged");
                    chk(got == 8'h00, "unmapped read is 0");
                    wr(a, 8'h5A);
                    chk(got_ack, "unmapped write acknowledged");
                end
            end
        rd(17'h10000); chk(got_ack && got == 8'h00, "EEPROM window read acknowledged, 0");
        rd(17'h10800); chk(got_ack && got == 8'h00, "EEPROM array read acknowledged, 0");
        wr(17'h10800, 8'hFF); chk(got_ack, "EEPROM array write acknowledged");
        rd(17'h10800); chk(got == 8'h00, "EEPROM array stays 0");
        rd(17'h00020); chk(got_ack, "DDR_CAL (not served) acknowledged");

        // ---- page 6 goes to the host's pixel layer (trx_lv_port.cc: PX_CTRL .. PX_KEY)
        wr(17'h06080, 8'h23);
        chk(px_we_seen && !px_re_seen, "page 6 write: px_we in the cycle of the request");
        chk(got_ack, "page 6 write acknowledged");
        rd(17'h06094);
        chk(px_re_seen && !px_we_seen, "page 6 read: px_re in the cycle of the request");
        chk(got_ack && got == (8'h94 ^ 8'hA5), "page 6 read returns px_rdata with the ack");
        rd(17'h060A0);
        chk(got == (8'hA0 ^ 8'hA5), "page 6 read, second address");
        wr(17'h00080, 8'h01);
        chk(!px_we_seen, "page 0 write does not reach the pixel layer");
        wr(17'h16080, 8'h01);
        chk(!px_we_seen, "EEPROM window address with page bits 6 does not reach the pixel layer");
        rd(17'h05080);
        chk(!px_re_seen, "page 5 read does not reach the pixel layer");

        // ---- the interrupt: trx_irq_attach(TRX_IRQ_FLIP), the handler writes IRQ_CAUSE with the active bits
        chk(cw_irq == 1'b0, "no interrupt at rest");
        wr(17'h00010, 8'h80);
        rd(17'h00010); chk(got == 8'h80, "IRQ_EN reads back");
        @(negedge cpu_clk); px_ev_flip = 1; @(negedge cpu_clk); px_ev_flip = 0; @(negedge cpu_clk);
        chk(cw_irq == 1'b1, "flip event raises the interrupt");
        rd(17'h00014); chk(got == 8'h80, "IRQ_CAUSE shows the flip (bit 7)");
        wr(17'h00014, 8'h80);
        @(negedge cpu_clk);
        chk(cw_irq == 1'b0, "write 1 to IRQ_CAUSE clears the interrupt");
        rd(17'h00014); chk(got == 8'h00, "IRQ_CAUSE reads 0 after the clear");
        // the vsync cause is set but does not interrupt while its enable is off
        @(negedge cpu_clk); px_ev_vsync = 1; @(negedge cpu_clk); px_ev_vsync = 0; @(negedge cpu_clk);
        chk(cw_irq == 1'b0, "vsync does not interrupt with its enable off");
        rd(17'h00014); chk(got == 8'h40, "IRQ_CAUSE shows the vsync (bit 6)");
        wr(17'h00010, 8'hC0);
        @(negedge cpu_clk);
        chk(cw_irq == 1'b1, "enabling the vsync cause raises the interrupt");
        wr(17'h00014, 8'h40);
        @(negedge cpu_clk);
        chk(cw_irq == 1'b0, "vsync cleared");
        // an event in the very cycle of the clearing write is not lost
        @(negedge cpu_clk); cw_addr = 17'h00014; cw_wdata = 8'h80; cw_we = 1; px_ev_flip = 1;
        @(negedge cpu_clk); cw_we = 0; px_ev_flip = 0; @(negedge cpu_clk);
        chk(cw_irq == 1'b1, "flip in the cycle of the clear write stays pending");
        wr(17'h00014, 8'hFF);
        @(negedge cpu_clk);
        chk(cw_irq == 1'b0, "cleared again");
        wr(17'h00010, 8'h00);

        // ---- the drive and GCR windows answer
        @(negedge cpu_clk); drv_io_rd = 1; @(negedge cpu_clk); drv_io_rd = 0; chk(drv_io_ack, "drive window read acknowledged");
        @(negedge cpu_clk); chk(!drv_io_ack, "drive ack lasts one cycle");
        @(negedge cpu_clk); gcr_io_wr = 1; @(negedge cpu_clk); gcr_io_wr = 0; chk(gcr_io_ack, "GCR window write acknowledged");
        @(negedge cpu_clk); chk(!gcr_io_ack, "GCR ack lasts one cycle");

        // ---- idle values of the outputs that matter to the host
        chk(c64_ce === 1'b1, "c64_ce = 1 (it gates the host's c64_gclk)");
        chk(trx_j_rvalid === 1'b1, "trx_j_rvalid = 1");
        chk(c64_halted === 1'b0 && gate_frz === 1'b0, "c64_halted, gate_frz idle");
        chk(c_en === 1'b0 && e_en === 1'b0 && ap_req === 1'b0 && win_port_use === 1'b0, "memory ports and app port idle");
        chk(slot_io1 === 1'b1 && slot_io2 === 1'b1 && slot_romh === 1'b1 && slot_roml === 1'b1 && slot_rw === 1'b1, "cartridge port released");
        chk(drv_io_nc === 1'b0 && gcr_io_nc === 1'b0 && sid_sock1_n === 1'b1 && audio_l === 24'd0, "nc flags, SID socket, audio idle");

        // ---- the raster: 504 dots of 4 sys_clk, 312 lines, colour 0, never stops
        maxx = 0; maxy = 0; anycol = 0;
        @(posedge sys_clk);
        while (!(pix_y == 311 && pix_x == 503)) @(posedge sys_clk);     // the last dot of a frame
        while (!(pix_y == 0 && pix_x == 0)) @(posedge sys_clk);         // the first clock of the next frame
        t1 = 0;
        begin : frame
            forever begin
                @(posedge sys_clk);
                t1 = t1 + 1;
                if (pix_x > maxx) maxx = pix_x;
                if (pix_y > maxy) maxy = pix_y;
                anycol = anycol | pix_color;
                if (pix_x == 0 && pix_y == 0 && t1 > 10) disable frame;
            end
        end
        chk(t1 == 504 * 4 * 312, "raster frame is 504 x 4 x 312 sys_clk");
        chk(maxx == 503 && maxy == 311, "pix_x runs to 503, pix_y to 311");
        chk(anycol == 4'd0, "pix_color stays 0");

        // ---- every clock of the partition has its register load, and it runs
        chk(tg_ui > 10 && tg_mclk > 10 && tg_gclk > 10 && tg_sid2 > 10 && tg_adc > 10, "a register load toggles on every clock");

        if (errors == 0) $display("PASS null_regs (%0d checks)", checks);
        else $display("FAIL null_regs: %0d of %0d checks", errors, checks);
        $finish;
    end

    initial begin
        #60_000_000;
        $display("FAIL null_regs: TIMEOUT");
        $finish;
    end
endmodule
