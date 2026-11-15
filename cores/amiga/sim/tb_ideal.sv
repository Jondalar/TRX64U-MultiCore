// T85: Minimig + fx68k with an ideal (zero latency) memory, under a clock enable ce28 that is either always 1
// or a sparse pattern. Both runs must show the same machine: same frame hash, same CPU counter. This is the
// proof that threading ce28 through the sources changed nothing (thread_ce28.py), and the first picture.
`timescale 1ns/1ps
module tb_ideal;
    parameter SPARSE = 0;            // 1: ce28 follows a 9/10 pattern with extra random gaps
    parameter FRAMES = 8;
    reg clk = 0;
    always #16 clk = ~clk;
    reg por = 1, reset = 1;
    integer cyc = 0;
    reg ce28 = 1;
    reg [31:0] lfsr = 32'h1234567;
    always @(posedge clk) begin
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        cyc <= cyc + 1;
        if (cyc == 20) por <= 0;
        if (cyc == 60) reset <= 0;
    end
    reg [3:0] n10 = 0;
    always @(posedge clk) begin
        n10 <= (n10 == 9) ? 0 : n10 + 1;
        if (SPARSE) ce28 <= (n10 != 3) && (lfsr[3:0] != 4'd5) && (lfsr[9:7] != 3'd2);
        else ce28 <= 1'b1;
    end

    wire clk7_en, clk7n_en, cpu_nrst, pwr_led, fdd_led, hdd_led, hs, vs;
    wire [3:0] r, g, b;
    wire hblank_o, vblank_o, lace_o, field1_o; wire [1:0] hires_o;
    wire [14:0] al, ar;
    wire [15:0] ram_data; wire [23:1] ram_address; wire n_bhe, n_ble, n_we, n_oe, refresh;
    wire fastram_sel; wire [23:1] fastram_addr; wire fastram_lds, fastram_uds, fastram_wr; wire [15:0] fastram_din;
    wire [7:0] sdc_rd, sdc_wr; wire [31:0] sdc_sector; wire [7:0] sdc_byte_out;
    wire uart_tx;

    reg [15:0] mem [0:(1<<22)-1];
    integer i;
    initial begin
        for (i = 0; i < (1<<22); i = i + 1) mem[i] = 16'h0000;
        $readmemh("rom.hex", mem, 22'h3C0000);            // 512 KB Kickstart slot at byte 0x780000
    end
    wire [15:0] rd_w = mem[ram_address[22:1]];
    always @(posedge clk) if (ce28 && !n_we) begin
        if (!n_bhe) mem[ram_address[22:1]][15:8] <= ram_data[15:8];
        if (!n_ble) mem[ram_address[22:1]][7:0]  <= ram_data[7:0];
    end

    nanomig dut (
        .ce28(ce28), .clk_sys(clk), .reset(reset), .por(por),
        .clk7_en(clk7_en), .clk7n_en(clk7n_en), .cpu_nrst_out(cpu_nrst),
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
        .ram_data(ram_data), .ramdata_in(rd_w), .chip48(48'd0), .refresh(refresh), .ram_address(ram_address),
        ._ram_bhe(n_bhe), ._ram_ble(n_ble), ._ram_we(n_we), ._ram_oe(n_oe),
        .fastram_sel(fastram_sel), .fastram_addr(fastram_addr), .fastram_lds(fastram_lds), .fastram_uds(fastram_uds),
        .fastram_dout(16'h0000), .fastram_dout48(48'd0), .fastram_din(fastram_din), .fastram_wr(fastram_wr),
        .fastram_ready(1'b0)
    );

    // frame hash over the visible pixels, counted per ce28 tick (so the SPARSE run sees the same stream)
    reg [31:0] h = 0; integer frames = 0; reg vb_d = 1; integer npix = 0;
    reg [31:0] counter_at_frame [0:15];
    always @(posedge clk) if (ce28) begin
        vb_d <= vblank_o;
        if (!hblank_o && !vblank_o) begin h <= {h[30:0], h[31]} ^ {r, g, b, 8'd0, 4'd0} ^ npix; npix <= npix + 1; end
        if (vb_d && !vblank_o) begin
            if (frames > 0) $display("frame %0d hash %08x pix %0d cpu_counter %04x", frames, h, npix, mem[22'h80]);
            if (frames == FRAMES) begin
                $display("PASS tb_ideal done sparse=%0d", SPARSE);
                $finish;
            end
            frames <= frames + 1; h <= 0; npix <= 0;
        end
    end
    integer nr = 0;
    always @(posedge clk) if (ce28 && !n_oe && nr < 30 && clk7_en && ram_address != 0) begin nr <= nr + 1; $display("read %06x -> %04x t=%0d ovl=%b", {ram_address,1'b0}, rd_w, cyc, dut.ovl); end
    always @(posedge clk) if (cyc % 100000 == 0) $display("t=%0d ovl=%b cpu_rst=%b as=%b dtack=%b ipl=%b chip_addr=%06x", cyc, dut.ovl, dut.cpu_rst, dut.chip_as, dut.chip_dtack, dut.chip_ipl, {dut.chip_addr,1'b0});
    // ctr trace
    integer ncw = 0;
    always @(posedge clk) if (ce28 && clk7_en && ram_address == 23'h80 && (!n_we || !n_oe) && ncw < 100000) begin
        ncw <= ncw + 1; $display("ctr %s %04x", !n_we ? "W" : "R", !n_we ? ram_data : rd_w); end
    integer nw = 0;
    always @(posedge clk) if (ce28 && !n_we && nw < 12 && clk7_en) begin nw <= nw + 1; $display("write %06x = %04x be %b%b", {ram_address,1'b0}, ram_data, n_bhe, n_ble); end
    always @(posedge clk) if (frames == 2 && !hblank_o && !vblank_o && npix == 5000) $display("sample px rgb %x %x %x at 5000", r, g, b);
    initial begin #400000000; $display("FAIL timeout, frames %0d", frames); $finish; end
endmodule
