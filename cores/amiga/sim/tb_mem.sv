// T85: the Minimig core behind amiga_mem (line cache + elastic stall) and a behavioural DDR2 (mig_ui_model),
// the same test ROM as tb_ideal. Proof of the elastic clock: the frame hash per ce28 tick equals the ideal
// run's (same machine, only slower), the CPU counter keeps counting. Reports the stall rate (S4).
`timescale 1ns/1ps
module tb_mem;
    parameter FRAMES = 8;
    parameter RD_LAT = 8;            // ddr2_lean: about 8 ui_clk cycles + queue; the MIG model's default is 20
    reg clk = 0, ui_clk = 0;
    always #15.86 clk = ~clk;
    always #6.667 ui_clk = ~ui_clk;
    reg por = 1, reset = 1, flush = 1;
    integer cyc = 0;
    always @(posedge clk) begin
        cyc <= cyc + 1;
        if (cyc == 20) por <= 0;
        if (cyc == 900) flush <= 0;
        if (cyc == 1000) reset <= 0;
    end

    wire ce28, clk7_en, clk7n_en; wire [1:0] phase;
    wire cpu_nrst, pwr_led, fdd_led, hdd_led, hs, vs; wire [3:0] r, g, b;
    wire hblank_o, vblank_o, lace_o, field1_o; wire [1:0] hires_o;
    wire [14:0] al, ar;
    wire [15:0] ram_data; wire [23:1] ram_address; wire n_bhe, n_ble, n_we, n_oe, refresh;
    wire fastram_sel; wire [23:1] fastram_addr; wire fastram_lds, fastram_uds, fastram_wr; wire [15:0] fastram_din;
    wire [7:0] sdc_rd, sdc_wr; wire [31:0] sdc_sector; wire [7:0] sdc_byte_out; wire uart_tx;
    wire [15:0] m_dout, p2_dout; wire p2_ready, m_pend;

    // the DDR2 side
    wire ui_rst, calib;
    wire [26:0] c_addr; wire [2:0] c_cmd; wire c_en; wire [127:0] c_wdf_data; wire [15:0] c_wdf_mask; wire c_wdf_wren;
    wire [127:0] app_rd_data; wire app_rd_valid, app_rdy, app_wdf_rdy;
    mig_ui_model #(.MEM_WORDS(1<<20), .RD_LAT(RD_LAT), .MISS(0), .STALL_PCT(0), .CALIB(100)) ddr (
        .ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(c_addr), .app_cmd(c_cmd), .app_en(c_en & (c_cmd == 3'b001 | app_wdf_rdy)), .app_rdy(app_rdy),
        .app_wdf_data(c_wdf_data), .app_wdf_mask(c_wdf_mask), .app_wdf_wren(c_wdf_wren & app_rdy),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_valid));
    wire c_rdy = app_rdy & ((c_cmd == 3'b001) | app_wdf_rdy);

    
    parameter DBG = 0;
    parameter MAXDEF_PPM = 3000;     // the rate bench: the machine may be at most this many ppm behind real time
    amiga_ce ce (.clk(clk), .phase(phase), .pend(m_pend), .pause(1'b0), .ce28(ce28));

    amiga_mem #(.BASE(27'd0)) mem (
        .sys_clk(clk), .sys_rst(por), .flush(flush), .ce28(ce28), .clk7_en(clk7_en),
        .cs(~n_oe | ~n_we), .we(~n_we), .addr(ram_address[22:1]), .be(~{n_bhe, n_ble}), .din(ram_data),
        .dout(m_dout), .pend(m_pend),
        .p2_sel(fastram_sel), .p2_wr(fastram_wr), .p2_addr(fastram_addr[22:1]), .p2_be(~{fastram_uds, fastram_lds}),
        .p2_din(fastram_din), .p2_dout(p2_dout), .p2_ready(p2_ready),
        .ui_clk(ui_clk), .ui_rst(ui_rst | ~calib), .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data), .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(app_wdf_rdy),
        .c_rd_data(app_rd_data), .c_rd_valid(app_rd_valid));

    nanomig dut (
        .ce28(ce28), .clk_sys(clk), .reset(reset), .por(por),
        .clk7_en(clk7_en), .clk7n_en(clk7n_en), .clk7_phase(phase), .cpu_nrst_out(cpu_nrst),
        .pwr_led(pwr_led), .fdd_led(fdd_led), .hdd_led(hdd_led),
        .hs(hs), .vs(vs), .r(r), .g(g), .b(b),
        .hblank_o(hblank_o), .vblank_o(vblank_o), .lace_o(lace_o), .field1_o(field1_o), .hires_o(hires_o),
        .memory_config(8'h01), .fastram_config(3'b000), .cpu_config(2'b00), .chipset_config(6'b000000),
        .floppy_config(4'b0000), .video_config(4'b0000), .turbo_config(3'b000), .ide_config(6'b100000),
        .rtc_en(1'b0), .rtc_ld(9'd0), .audio_left(al), .audio_right(ar),
        .mouse_buttons(3'b000), .kbd_mouse_level(1'b0), .kbd_mouse_type(2'b00), .kbd_mouse_data(8'h00),
        .joystick0(11'h000), .cd32pad2_en(1'b0), .joystick1(8'h00), .uart_tx(uart_tx), .uart_rx(1'b1),
        .sdc_img_mounted(8'h00), .sdc_img_size(64'd0), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr), .sdc_sector(sdc_sector),
        .sdc_busy(1'b0), .sdc_done(1'b0), .sdc_byte_in_strobe(1'b0), .sdc_byte_addr(9'd0), .sdc_byte_in_data(8'h00),
        .sdc_byte_out_data(sdc_byte_out),
        .ram_data(ram_data), .ramdata_in(m_dout), .chip48(48'd0), .refresh(refresh), .ram_address(ram_address),
        ._ram_bhe(n_bhe), ._ram_ble(n_ble), ._ram_we(n_we), ._ram_oe(n_oe),
        .fastram_sel(fastram_sel), .fastram_addr(fastram_addr), .fastram_lds(fastram_lds), .fastram_uds(fastram_uds),
        .fastram_dout(p2_dout), .fastram_dout48(48'd0), .fastram_din(fastram_din), .fastram_wr(fastram_wr),
        .fastram_ready(p2_ready)
    );

    // preload the Kickstart slot (word address 0x3C0000..) into the DDR2 model, byte order of the DDR2
    reg [15:0] rom [0:262143];
    integer i;
    initial begin
        $readmemh("rom.hex", rom);
        for (i = 0; i < 262144; i = i + 1)
            ddr.mem[(22'h3C0000 + i) >> 3][16*((22'h3C0000 + i) & 7) +: 16] = {rom[i][7:0], rom[i][15:8]};
    end

    // the same hash as tb_ideal, counted per ce28 tick
    reg [31:0] h = 0; integer frames = 0; reg vb_d = 1; integer npix = 0;
    integer ticks = 0, clks = 0;
    longint want, behind;
    integer clks_w = 0, ticks_w = 0;
    parameter WIN_FROM = 5;
    always @(posedge clk) begin
        clks <= clks + 1;
        if (ce28) ticks <= ticks + 1;
    end
    // where the held clocks come from (T85): by the state of the request machine while the chipset is held
    integer hb_relook = 0, hb_wait_pf = 0, hb_wait_pf_d = 0, hb_wrfull = 0, hb_idle = 0, hb_other = 0, hb_total = 0;
    always @(posedge clk) if (!ce28 && m_pend && ce.block) begin
        hb_total <= hb_total + 1;
        if (mem.st == 3'd4) hb_relook <= hb_relook + 1;
        else if (mem.st == 3'd1 && mem.r_we) hb_wrfull <= hb_wrfull + 1;
        else if (mem.st == 3'd1 && mem.fill_inflight) hb_wait_pf <= hb_wait_pf + 1;
        else if (mem.st == 3'd0) hb_idle <= hb_idle + 1;
        else hb_other <= hb_other + 1;
    end
    always @(posedge clk) if (ce28) begin
        vb_d <= vblank_o;
        if (!hblank_o && !vblank_o) begin h <= {h[30:0], h[31]} ^ {r, g, b, 8'd0, 4'd0} ^ npix; npix <= npix + 1; end
        if (vb_d && !vblank_o) begin
            if (frames > 0 && DBG) $display("dbg frame %0d clks %0d ticks %0d debt %0d lost %0d hold %0d", frames, clks, ticks, ce.debt, ce.lost, ce.st_hold);
            if (frames > 0) $display("frame %0d hash %08x pix %0d cpu_counter %04x", frames, h, npix,
                                     {ddr.mem[22'h10][7:0], ddr.mem[22'h10][15:8]});
            if (frames == FRAMES) begin
                $display("speed: %0d ticks in %0d clocks = %0d.%02d of the 0.90 ideal; ce28 duty %0d%%", ticks, clks,
                    (ticks * 100 / clks) / 100, (ticks * 100 / clks) % 100, ticks * 100 / clks);
                $display("ce hold clocks %0d", ce.st_hold);
                // the window is the loaded part of the run (from the end of frame WIN_FROM): the test ROM first fills its bitplanes
                want = ((clks - clks_w) * 9) / 10;
                behind = (want > ticks - ticks_w) ? ((want - (ticks - ticks_w)) * 1000000) / want : 0;
                $display("rate: loaded window %0d clocks: %0d ticks, real time wants %0d, behind by %0d ppm; debt now %0d, deepest %0d, lost %0d",
                    clks - clks_w, ticks - ticks_w, want, behind, ce.debt, ce.debt_max, ce.lost);
                if (behind > MAXDEF_PPM) $display("FAIL rate: the machine runs slower than real time");
                else $display("PASS rate");
                $display("hold by: own-miss-fill %0d, wait for a fill in flight %0d, write FIFO full %0d, request machine idle/queued %0d, other %0d",
                    hb_relook, hb_wait_pf, hb_wrfull, hb_idle, hb_other);
                $display("mem: reads %0d miss %0d writes %0d prefetch %0d cycles %0d", mem.st_reads, mem.st_miss,
                    mem.st_writes, mem.st_pf, mem.st_cycles);
                for (i = 0; i < 6; i = i + 1) $display("w[%04x] = %04x", 22'h80 + i, {ddr.mem[22'h10][16*i +: 8], ddr.mem[22'h10][16*i + 8 +: 8]});
                for (i = 0; i < 6; i = i + 1) $display("cop[%04x] = %04x", 22'hC00 + i, {ddr.mem[22'h180][16*i +: 8], ddr.mem[22'h180][16*i + 8 +: 8]});
                $display("ovl=%b cpu_rst=%b", dut.ovl, dut.cpu_rst);
                $display("PASS tb_mem done");
                $finish;
            end
            if (frames == WIN_FROM) begin clks_w = clks; ticks_w = ticks; end
            frames <= frames + 1; h <= 0; npix <= 0;
        end
    end
    // ctr trace
    integer ncw = 0;
    always @(posedge clk) if (ce28 && clk7_en && ram_address == 23'h80 && (!n_we || !n_oe) && ncw < 100000) begin
        ncw <= ncw + 1; $display("ctr %s %04x", !n_we ? "W" : "R", !n_we ? ram_data : m_dout); end
    integer nw = 0;
    always @(posedge clk) if (ce28 && !n_we && clk7_en && nw < 40 && ram_address > 23'h0 && ram_address < 23'h8000) begin nw <= nw + 1; $display("write %06x = %04x t=%0d", {ram_address,1'b0}, ram_data, cyc); end
    integer nx = 0;
    always @(posedge clk) if (ce28 && !n_oe && clk7_en && nx < 30 && ram_address >= 23'h3C00A3) begin nx <= nx + 1; $display("rom-high read %06x -> %04x t=%0d", {ram_address,1'b0}, m_dout, cyc); end
    integer nr = 0;
    always @(posedge clk) if (ce28 && !n_oe && nr < 24 && clk7_en && ram_address != 0) begin nr <= nr + 1; $display("read %06x -> %04x t=%0d pend=%b", {ram_address,1'b0}, m_dout, cyc, m_pend); end
    initial begin #900000000; $display("FAIL timeout, frames %0d", frames); $finish; end
endmodule
