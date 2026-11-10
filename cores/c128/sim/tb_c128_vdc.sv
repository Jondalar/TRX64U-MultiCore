// T113 S8 "the VDC is a VDC": the C128's VDC (cores/c128/c128_x.v with -DVDC_WHOLE [-DVDC64]) driven from the CPU side by a stimulus
// file made by vdc_model.py, which also holds the expected values (written from the 8563 programmer's reference, not from this RTL).
// This bench only replays bus operations and records: the values read, the busy measurements, the VRAM and the captured frame.
//   +stim=<hex file> +out=<prefix>   [+maxcyc=N]
`timescale 1ns / 1ps
module tb_c128_vdc;
    reg clk = 1'b0;
    always #15.859 clk = ~clk;                        // sys_clk 31.5278 MHz
    reg rst = 1'b1;
    wire [7:0] dout;
    wire [3:0] rgbi;
    wire ce, hb, vb;
    c128_x dut (.clk(clk), .rst(rst), .bus_en(bus_en), .vdc_cs(cs), .vdc_we(we), .vdc_rs(rs), .vdc_din(din), .vdc_dout(dout),
        .vicie_cs(1'b0), .vicie_we(1'b0), .vicie_a0(1'b0), .vicie_din(8'd0), .vicie_dout(), .k_sel(), .fast(),
        .vdc_rgbi(rgbi), .vdc_ce(ce), .vdc_hb(hb), .vdc_vb(vb),
        .vd_clk(1'b0), .vd_addr(16'd0), .vd_we(1'b0), .vd_wdata(8'd0), .vd_rdata());
`include "vdc_bus.svh"
`ifdef VDC64
    localparam integer NV = 65536;
`else
    localparam integer NV = 16384;
`endif

    // ---- frame capture from the VDC's own strobes: pixel_ce says when rgbi / active_h / active_v describe a new dot
    wire av = ~vb, ah = ~hb;
    reg av_prev = 1'b0, ah_prev = 1'b0;
    integer cap_st = 0;                               // 0 idle, 1 armed, 2 capturing, 3 done
    integer skipn = 0, ln = 0, xx = 0, nl = 0;
    reg [3:0] fr [0:1024*512-1];
    integer linew [0:511];
    always @(posedge clk) if (ce) begin
        if (skipn > 0 && av_prev && !av) skipn = skipn - 1;
        if (cap_st == 1 && av && !av_prev) begin cap_st = 2; ln = -1; xx = 0; end
        else if (cap_st == 2) begin
            if (!av) begin cap_st = 3; nl = ln + 1; end
            else begin
                if (ah && !ah_prev) begin ln = ln + 1; xx = 0; end
                if (ah && ln >= 0 && ln < 512) begin
                    if (xx < 1024) fr[ln * 1024 + xx] = rgbi;
                    xx = xx + 1;
                    linew[ln] = xx;
                end
            end
        end
        av_prev <= av; ah_prev <= ah;
    end

    reg [15:0] mem [0:262143];
    string stimf, outp;
    integer fd_r, fd_b, fd_v, fd_f, pc, i, j, n, maxcyc;
    reg [15:0] w;
    reg [7:0] q;
    longint t0;
    initial begin
        if (!$value$plusargs("stim=%s", stimf)) begin $display("FAIL tb: +stim missing"); $finish; end
        if (!$value$plusargs("out=%s", outp)) begin $display("FAIL tb: +out missing"); $finish; end
        if (!$value$plusargs("maxcyc=%d", maxcyc)) maxcyc = 60000000;
        for (i = 0; i < 262144; i = i + 1) mem[i] = 16'hFF00;
        $readmemh(stimf, mem);
        fd_r = $fopen({outp, ".reads"}, "w");
        fd_b = $fopen({outp, ".busy"}, "w");
        repeat (16) @(posedge clk);
        rst <= 1'b0;
        repeat (70000) @(posedge clk);                // the RAM erase after reset (initRam) is over
        pc = 0;
        w = mem[0];
        while (w[15:8] != 8'hFF) begin
            case (w[15:8])
                8'h00: wr_sel(w[7:0]);
                8'h01: wr_dat(w[7:0]);
                8'h02: begin bus(1'b0, 1'b0, 8'd0, q); $fwrite(fd_r, "%02x\n", q); end
                8'h03: begin bus(1'b1, 1'b0, 8'd0, q); $fwrite(fd_r, "%02x\n", q); end
                8'h04: wait_ready;
                8'h05: repeat (w[7:0] * 256) @(posedge clk);
                8'h06: begin
                    t0 = t_pulse;
                    bus(1'b0, 1'b0, 8'd0, q);
                    $fwrite(fd_b, "%02x ", q);
                    n = 0;
                    while (!q[7] && n < 200000) begin bus(1'b0, 1'b0, 8'd0, q); n = n + 1; end
                    $fwrite(fd_b, "%0d\n", cyc - t0);
                end
                8'h07: begin
                    skipn = w[7:0]; n = 0;
                    while (skipn > 0 && n < 8000000) begin @(posedge clk); n = n + 1; end
                    if (skipn > 0) $display("tb: skip frames timed out");
                end
                8'h08: begin
                    cap_st = 1; n = 0;
                    while (cap_st != 3 && n < 8000000) begin @(posedge clk); n = n + 1; end
                    if (cap_st != 3) $display("tb: frame capture timed out");
                    fd_f = $fopen({outp, ".frame"}, "w");
                    for (i = 0; i < nl; i = i + 1) begin
                        for (j = 0; j < linew[i]; j = j + 1) $fwrite(fd_f, "%x", fr[i * 1024 + j]);
                        $fwrite(fd_f, "\n");
                    end
                    $fclose(fd_f);
                end
                default: $display("tb: bad op %04x at %0d", w, pc);
            endcase
            pc = pc + 1;
            w = mem[pc];
        end
        repeat (200) @(posedge clk);
        fd_v = $fopen({outp, ".vram"}, "w");
        for (i = 0; i < NV; i = i + 1) $fwrite(fd_v, "%02x\n", dut.vdc.ram.ram.m[i]);
        $fclose(fd_v); $fclose(fd_r); $fclose(fd_b);
        $display("tb_c128_vdc done: %0d ops, %0d clocks", pc, cyc);
        $finish;
    end
    always @(posedge clk) if (cyc > maxcyc) begin $display("FAIL tb_c128_vdc: timeout after %0d clocks", cyc); $finish; end
endmodule
