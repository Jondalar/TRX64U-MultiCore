// T257 S4/S5/S6 "the VDC read-back": the C128's register page (c128_regs.v, cpu_clk 100 MHz) and the VDC (c128_x -DVDC_WHOLE -DVDC64, sys_clk 31.5278 MHz) joined
// as in layer2_c128.v (vk_req / vk_ans). The CPU side of the VDC is driven like the core does (vdc_bus.svh); the page is read like the application does (a read
// is held until ack). Checks:
//   - every register R0-R15, R18-R30, R32-R37 read at 100 + n equals what the CPU reads at $D601 (the program of tb_c128_pixw's phase 1)
//   - 126 (the status byte) agrees with the CPU's status read in the bits that do not move (7, 3-0), 127 is the selected register
//   - the VRAM bytes written through R31 (R18/R19 = $0100, four bytes) read back through 140-142, in order, the address register 140/141 counts up; a byte above
//     $8000 too (the 64 KiB shuffle); and the CPU's update address R18/R19 and a following R31 read are exactly what they were: the read-back moves nothing
//   - every access is acknowledged within 200 cpu clocks (a read with no answer ends in 0xFF and 143 = 1: tb_regs.v covers that against a dead model)
`timescale 1ns / 1ps
module tb_c128_vkpeek;
    reg clk = 1'b0, cclk = 1'b0;
    always #15.859 clk = ~clk;                        // sys_clk 31.5278 MHz
    always #5 cclk = ~cclk;                           // cpu_clk 100 MHz
    reg rst = 1'b1;
    wire [7:0] dout;
    wire [3:0] rgbi;
    wire ce, hb, vb;
    wire [1:0] vdc_il;
    wire [17:0] vk_req;
    wire  [8:0] vk_ans;
    c128_x dut (.clk(clk), .rst(rst), .bus_en(bus_en), .vdc_cs(cs), .vdc_we(we), .vdc_rs(rs), .vdc_din(din), .vdc_dout(dout),
        .vicie_cs(1'b0), .vicie_we(1'b0), .vicie_a0(1'b0), .vicie_din(8'd0), .vicie_dout(), .k_sel(), .fast(),
        .vdc_rgbi(rgbi), .vdc_ce(ce), .vdc_hb(hb), .vdc_vb(vb), .vdc_il(vdc_il),
        .vd_clk(1'b0), .vd_addr(16'd0), .vd_we(1'b0), .vd_wdata(8'd0), .vd_rdata(), .vk_req(vk_req), .vk_ans(vk_ans));
`include "vdc_bus.svh"
    reg         pwe = 0, pre = 0;
    reg  [16:0] paddr = 0;
    reg   [7:0] pwdata = 0;
    wire  [7:0] prdata;
    wire        pack, pirq;
    c128_regs regs (.clk(cclk), .we(pwe), .re(pre), .addr(paddr), .wdata(pwdata), .rdata(prdata), .ack(pack), .irq(pirq),
        .sp_caps(1'b0), .sp_4080(1'b0), .sp_mon(1'b0), .x_usb(), .mon_sel(), .d4080(), .capslk(), .boot64(), .joy_swap(), .vdc_en(), .rom_flush_tgl(),
        .cold_tgl(), .cold_busy(1'b0), .fb_base(), .fb_stride(), .chr_we(), .chr_a(), .chr_d(), .chr_q(8'd0), .dbg_clr_tgl(),
        .rom_waits(32'd0), .rom_fill_max(16'd0), .fr_min(20'd0), .fr_max(20'd0), .ram_cnt(80'd0), .act(40'd0),
        .status(8'd0), .rom_busy(1'b0), .rom_hits(32'd0), .rom_misses(32'd0), .fb_latest(8'd0), .fb_seq(8'd0), .fb_over(16'd0), .ev_frame(1'b0),
        .vk_req(vk_req), .vk_ans(vk_ans));
    // the application's read: the address and a one-clock re, then wait for ack; v = the byte at the ack
    integer nbad = 0, waits = 0;
    task automatic prd(input [11:0] off, output [7:0] v);
        integer k;
        begin
            @(posedge cclk); paddr <= {1'b0, 4'hE, off}; pre <= 1'b1;
            @(posedge cclk); pre <= 1'b0;
            k = 0; while (!pack && k < 2000) begin @(posedge cclk); k = k + 1; end
            if (k >= 2000) begin $display("FAIL: no ack for %03x", off); nbad = nbad + 1; end
            waits = (k > waits) ? k : waits;
            v = prdata;
            @(posedge cclk);
        end
    endtask
    task automatic pwr(input [11:0] off, input [7:0] d);
        begin
            @(posedge cclk); paddr <= {1'b0, 4'hE, off}; pwdata <= d; pwe <= 1'b1;
            @(posedge cclk); pwe <= 1'b0;
            @(posedge cclk);
        end
    endtask
    task automatic cpu_rd(input [7:0] n, output [7:0] v);     // select register n, read $D601
        begin wr_sel(n); bus(1'b1, 1'b0, 8'd0, v); end
    endtask
    task automatic expect8(input [255:0] name, input [7:0] got, input [7:0] want);
        begin
            if (got !== want) begin $display("FAIL: %0s = %02x, expected %02x", name, got, want); nbad = nbad + 1; end
            else $display("ok   %0s = %02x", name, got);
        end
    endtask
    reg [7:0] r [0:63];
    integer ci;
    reg [7:0] a, b, c;
    initial begin
        for (ci = 0; ci < 64; ci = ci + 1) r[ci] = 8'd0;
        r[0]=126; r[1]=80; r[2]=102; r[3]=8'h49; r[4]=32; r[5]=0; r[6]=25; r[7]=29;
        r[8]=0; r[9]=7; r[10]=8'h20; r[11]=7; r[12]=0; r[13]=0; r[14]=8'h12; r[15]=8'h34;
        r[20]=8; r[21]=0; r[22]=8'h78; r[23]=8; r[24]=8'h20; r[25]=8'h47; r[26]=8'hF4;
        r[27]=0; r[28]=8'h30; r[29]=7; r[34]=125; r[35]=100; r[36]=5;
        repeat (16) @(posedge clk);
        rst <= 1'b0;
        repeat (70000) @(posedge clk);                      // the VDC clears its RAM after the reset
        for (ci = 0; ci < 37; ci = ci + 1)
            if ((ci < 16) || (ci >= 20 && ci <= 29) || (ci >= 34)) wr_reg(ci[7:0], r[ci]);
        // S4: registers
        for (ci = 0; ci < 38; ci = ci + 1)
            if (ci != 16 && ci != 17 && ci != 18 && ci != 19 && ci != 30 && ci != 31) begin
                cpu_rd(ci[7:0], a);
                prd(12'h100 + ci[11:0], b);
                if (a !== b) begin $display("FAIL: R%0d: CPU reads %02x, read-back %02x", ci, a, b); nbad = nbad + 1; end
            end
        $display("ok   R0-R37 (but R16-R19, R30, R31): the read-back equals the CPU's read");
        prd(12'h104, a); expect8("R4 through the page", a, 8'd32);
        prd(12'h106, a); expect8("R6 through the page", a, 8'd25);
        prd(12'h109, a); expect8("R9 through the page", a, 8'd7);
        prd(12'h119, a); expect8("R25 through the page", a, 8'h47);
        wr_sel(8'd36);
        prd(12'h127, a); expect8("127 = the selected register", a, 8'd36);
        wait_ready;
        bus(1'b0, 1'b0, 8'd0, a); prd(12'h126, b);
        expect8("126 = status bit 7 (ready)", b & 8'h80, a & 8'h80);
        expect8("126 = status bits 3-0", b & 8'h0F, a & 8'h0F);
        // S5: the VRAM, and nothing moved
        wr_reg(8'd18, 8'h01); wr_reg(8'd19, 8'h00);
        for (ci = 0; ci < 4; ci = ci + 1) wr_reg(8'd31, 8'h11 + ci[7:0]);
        wr_reg(8'd18, 8'h80); wr_reg(8'd19, 8'h01); wr_reg(8'd31, 8'hC3);        // $8001
        cpu_rd(8'd18, a); cpu_rd(8'd19, b);
        expect8("R18 before the read-back", a, 8'h80); expect8("R19 before the read-back", b, 8'h02);
        pwr(12'h140, 8'h00); pwr(12'h141, 8'h01);
        for (ci = 0; ci < 4; ci = ci + 1) begin prd(12'h142, a); expect8("VRAM $0100 + n", a, 8'h11 + ci[7:0]); end
        prd(12'h140, a); prd(12'h141, b);
        expect8("140 after four reads", a, 8'h04); expect8("141 after four reads", b, 8'h01);
        pwr(12'h140, 8'h01); pwr(12'h141, 8'h80);
        prd(12'h142, a); expect8("VRAM $8001", a, 8'hC3);
        cpu_rd(8'd18, a); cpu_rd(8'd19, b);
        expect8("R18 after the read-back", a, 8'h80); expect8("R19 after the read-back", b, 8'h02);
        // the CPU's read of R31 still returns the byte at its own address ($8002 was never written: the fill value of the power-on RAM or 0), the pointer moves by one
        wr_reg(8'd18, 8'h01); wr_reg(8'd19, 8'h00);
        cpu_rd(8'd31, a); expect8("CPU R31 read at $0100 after the read-back", a, 8'h11);
        wait_ready;
        cpu_rd(8'd31, a); expect8("CPU R31 read at $0101", a, 8'h12);
        $display("     longest wait for an ack: %0d cpu clocks", waits);
        check_gt;
        if (nbad == 0) $display("PASS tb_c128_vkpeek"); else $display("FAIL tb_c128_vkpeek: %0d wrong", nbad);
        $finish;
    end
    task automatic check_gt;
        begin
            if (waits >= 200) begin $display("FAIL: an access took %0d cpu clocks (> 200)", waits); nbad = nbad + 1; end
            else $display("ok   every access acknowledged within 200 cpu clocks");
        end
    endtask
    always @(posedge clk) if (cyc > 80000000) begin $display("FAIL tb_c128_vkpeek: timeout"); $finish; end
endmodule
