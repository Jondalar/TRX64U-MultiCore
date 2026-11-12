`timescale 1ns/1ps
// T103: the same two stream senders, once wired directly to the RMII arbiter as top.v does (reference, clk50), once behind
// the generic packet port (core on cpu_clk -> crossing FIFO -> arbiter). Both see the same pixels, samples, template and
// MAC traffic; every frame that reaches the transmitter is compared byte for byte, kind by kind (video 822, audio 812,
// MAC < 100 bytes). Controls (-DNEG_CORRUPT, -DNEG_NOREADY) must end in FAIL.

// the arbiter of top.v (lines 744-760), a MAC frame source, a transmitter that takes a byte per four clocks, a recorder
module tx_side (
    input  wire       clk50, rst,
    input  wire       st_req,
    input  wire [7:0] st_data,
    input  wire       st_valid, st_eof,
    output reg        own_st,
    output wire       x_ready
);
    // MAC source
    reg [15:0] tmr = 0, k = 0, i = 0;
    reg        mac_tx_valid = 0;
    wire [15:0] len = 16'd64 + (k % 32);
    wire [7:0] mac_tx_data = k * 7 + i;
    wire       mac_tx_eof = (i == len - 1);
    wire       mac_tx_ready;
    always @(posedge clk50)
        if (rst) begin tmr <= 0; k <= 0; i <= 0; mac_tx_valid <= 0; end
        else if (!mac_tx_valid) begin
            tmr <= tmr + 1;
            if (tmr == 16'd2500) begin tmr <= 0; mac_tx_valid <= 1; i <= 0; end
        end else if (mac_tx_ready) begin
            i <= i + 1;
            if (mac_tx_eof) begin mac_tx_valid <= 0; k <= k + 1; end
        end
    // transmitter
    reg [1:0] c = 0; reg [5:0] gap = 0;
    assign x_ready = (c == 2'd3) && gap == 0;
    reg own_mac = 0;
    wire [7:0] x_data  = own_st ? st_data : mac_tx_data;
    wire       x_eof   = own_st ? st_eof  : mac_tx_eof;
    wire       x_valid = own_st ? st_valid : own_mac & mac_tx_valid;
    assign mac_tx_ready = own_mac & x_ready;
    always @(posedge clk50)
        if (rst) begin own_mac <= 0; own_st <= 0; end
        else if (!own_mac && !own_st) begin
            if (mac_tx_valid) own_mac <= 1'b1;
            else if (st_req)  own_st  <= 1'b1;
        end else if (own_mac && mac_tx_valid && mac_tx_eof && x_ready)
            own_mac <= 1'b0;
        else if (own_st && st_valid && st_eof && x_ready)
            own_st <= 1'b0;
    // recorder
    reg [7:0]  fr [0:400*1024-1];
    integer    fl [0:399];
    integer    nf = 0, pos = 0;
    always @(posedge clk50) begin
        c <= c + 1;
        if (gap != 0) gap <= gap - 1;
        if (x_valid && x_ready) begin
            fr[nf*1024 + pos] <= x_data; pos = pos + 1;
            if (x_eof) begin fl[nf] = pos; nf = nf + 1; pos = 0; gap <= 6'd30; end
        end
    end
endmodule

module tb_pktport;
    reg clk50 = 0, cpu_clk = 0, vclk = 0, mclk = 0;
    always #10        clk50   = ~clk50;
    always #5         cpu_clk = ~cpu_clk;
    always #15.875    vclk    = ~vclk;
    always #40.686    mclk    = ~mclk;
    reg rst50 = 1, rstcpu = 1;
    initial begin #200 rst50 = 0; rstcpu = 0; end

    // the C64's side: pixels (4 vclk each, 504 x 312), audio frames (every 256 mclk), template, enable
    reg [9:0] px = 0; reg [8:0] py = 0; reg [1:0] pd = 0; reg [7:0] fno = 0;
    reg [3:0] pix;
    always @(posedge vclk) begin
        pd <= pd + 1;
        if (pd == 3) begin
            if (px == 503) begin px <= 0; if (py == 311) begin py <= 0; fno <= fno + 1; end else py <= py + 1; end
            else px <= px + 1;
        end
        pix <= (px * 3 + py * 5 + fno) ^ (px >> 4);
    end
    reg [7:0] sc = 0; reg snd_en = 0; reg signed [23:0] in_l = 24'h123456, in_r = -24'sh0abcde;
    always @(posedge mclk) begin
        sc <= sc + 1; snd_en <= (sc == 255);
        if (snd_en) begin in_l <= in_l + 24'h010305; in_r <= in_r - 24'h000f07; end
    end
    reg udp_we = 0; reg [7:0] udp_addr = 0, udp_wdata = 0; reg [7:0] stream_ena = 0;
    integer j;
    initial begin
        #1000;
        for (j = 0; j < 42; j = j + 1) begin
            @(posedge cpu_clk); udp_we <= 1; udp_addr <= j; udp_wdata <= (j == 24) ? 8'h12 : (j == 25) ? 8'h34 : j * 13 + 5;
            @(posedge cpu_clk); udp_addr <= 8'h40 + j; udp_wdata <= (j == 24) ? 8'h56 : (j == 25) ? 8'h78 : j * 11 + 9;
        end
        @(posedge cpu_clk); udp_we <= 0;
        #100 stream_ena = 8'h03;
    end

    // ---- reference: straight into the arbiter on clk50
    wire [7:0] rv_data, ra_data, r_data; wire r_req, r_valid, r_eof;
    wire rv_req, rv_grant, rv_valid, rv_eof, rv_ready, ra_req, ra_grant, ra_valid, ra_eof, ra_ready;
    wire own_a, xr_a; wire [15:0] fa, pa;
    udp_stream ref_v (.vclk(vclk), .pix(pix), .px(px), .py(py), .v60(1'b0),
        .cpu_clk(cpu_clk), .tpl_we(udp_we && udp_addr[7:6] == 2'd0), .tpl_addr(udp_addr[5:0]), .tpl_wdata(udp_wdata),
        .eth_clk(clk50), .eth_rst(rst50), .enable(stream_ena[0]),
        .tx_req(rv_req), .tx_grant(rv_grant), .tx_data(rv_data), .tx_valid(rv_valid), .tx_eof(rv_eof), .tx_ready(rv_ready), .frames_sent(fa));
    udp_audio ref_a (.snd_clk(mclk), .snd_en(snd_en), .in_l(in_l), .in_r(in_r),
        .cpu_clk(cpu_clk), .tpl_we(udp_we && udp_addr[7:6] == 2'd1), .tpl_addr(udp_addr[5:0]), .tpl_wdata(udp_wdata),
        .eth_clk(clk50), .eth_rst(rst50), .enable(stream_ena[1]),
        .tx_req(ra_req), .tx_grant(ra_grant), .tx_data(ra_data), .tx_valid(ra_valid), .tx_eof(ra_eof), .tx_ready(ra_ready), .pkts_sent(pa));
    stream_mux ref_m (.clk(clk50), .rst(rst50),
        .a_req(rv_req), .b_req(ra_req), .a_grant(rv_grant), .b_grant(ra_grant),
        .a_data(rv_data), .b_data(ra_data), .a_valid(rv_valid), .b_valid(ra_valid),
        .a_eof(rv_eof), .b_eof(ra_eof), .a_ready(rv_ready), .b_ready(ra_ready),
        .req(r_req), .grant(own_a), .data(r_data), .valid(r_valid), .eof(r_eof), .ready(own_a & xr_a));
    tx_side txa (.clk50(clk50), .rst(rst50), .st_req(r_req), .st_data(r_data), .st_valid(r_valid), .st_eof(r_eof),
        .own_st(own_a), .x_ready(xr_a));

    // ---- the packet port: core on cpu_clk, FIFO in the host
    wire pp_req, pp_grant, pp_valid, pp_eof, pp_ready; wire [7:0] pp_data;
    pktport_core core (.cpu_clk(cpu_clk), .cpu_rst(rstcpu), .vclk(vclk), .pix(pix), .px(px), .py(py), .v60(1'b0),
        .snd_clk(mclk), .snd_en(snd_en), .in_l(in_l), .in_r(in_r),
        .udp_we(udp_we), .udp_addr(udp_addr), .udp_wdata(udp_wdata), .stream_ena(stream_ena),
        .pp_req(pp_req), .pp_grant(pp_grant), .pp_data(pp_data), .pp_valid(pp_valid), .pp_eof(pp_eof), .pp_ready(pp_ready));
    integer npush = 0;
    always @(posedge cpu_clk) if (pp_valid && pp_ready) npush <= npush + 1;
`ifdef NEG_CORRUPT
    wire [7:0] pk_data_h = pp_data ^ ((npush == 1000) ? 8'h01 : 8'h00);   // one bit in one byte of the port
`else
    wire [7:0] pk_data_h = pp_data;
`endif
    wire [7:0] b_data; wire b_req, b_valid, b_eof, own_b, xr_b;
    pktport_host host (.wclk(cpu_clk), .wrst(rstcpu), .dc(1'b0), .pp_req(pp_req), .pp_grant(pp_grant), .pp_data(pk_data_h),
        .pp_valid(pp_valid), .pp_eof(pp_eof), .pp_ready(pp_ready),
        .rclk(clk50), .rrst(rst50), .st_req(b_req), .st_data(b_data), .st_valid(b_valid), .st_eof(b_eof), .pop(own_b & xr_b));
    tx_side txb (.clk50(clk50), .rst(rst50), .st_req(b_req), .st_data(b_data), .st_valid(b_valid), .st_eof(b_eof),
        .own_st(own_b), .x_ready(xr_b));

    // ---- compare
    integer kind, ia, ib, na, nb, ca, cb, bad, t, lenk, errs, shown;
    task cmp_kind(input integer want);
        begin
            ca = 0; cb = 0; bad = 0; ib = 0;
            for (ia = 0; ia < txa.nf; ia = ia + 1)
                if ((want == 0 && txa.fl[ia] < 100) || (want != 0 && txa.fl[ia] == want)) begin
                    ca = ca + 1;
                    // k-th frame of the same kind in B
                    while (ib < txb.nf && !((want == 0 && txb.fl[ib] < 100) || (want != 0 && txb.fl[ib] == want))) ib = ib + 1;
                    if (ib < txb.nf) begin
                        cb = cb + 1;
                        if (txa.fl[ia] != txb.fl[ib]) bad = bad + 1;
                        else for (t = 0; t < txa.fl[ia]; t = t + 1)
                            if (txa.fr[ia*1024+t] !== txb.fr[ib*1024+t]) begin
                                if (shown < 3) $display("  kind %0d frame %0d byte %0d: ref %02x port %02x", want, ca-1, t,
                                                         txa.fr[ia*1024+t], txb.fr[ib*1024+t]);
                                shown = shown + 1; bad = bad + 1; t = 99999;
                            end
                        ib = ib + 1;
                    end
                end
            while (ib < txb.nf) begin
                if ((want == 0 && txb.fl[ib] < 100) || (want != 0 && txb.fl[ib] == want)) cb = cb + 1;
                ib = ib + 1;
            end
            $display("kind %0d: ref %0d frames, port %0d frames, %0d differ", want, ca, cb, bad);
            if (bad != 0) errs = errs + 1;
            if (ca < ((want == 822) ? 60 : (want == 812) ? 4 : 5) || cb < ((want == 822) ? 60 : (want == 812) ? 4 : 5)) begin
                $display("  too few frames"); errs = errs + 1; end
            if (ca - cb > 1 || cb - ca > 1) begin $display("  counts differ by more than one"); errs = errs + 1; end
        end
    endtask
    initial begin
        errs = 0; shown = 0;
        #26_000_000;
        $display("frames on the wire: ref %0d, port %0d", txa.nf, txb.nf);
        cmp_kind(822); cmp_kind(812); cmp_kind(0);
        if (errs == 0) $display("PASS pktport: video, audio and MAC frames identical through the packet port");
        else           $display("FAIL pktport: %0d checks failed", errs);
        $finish;
    end
endmodule
