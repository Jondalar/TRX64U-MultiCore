// udp_audio and udp_stream on one transmitter through stream_mux, against a
// receiver that takes a byte every 4 clocks (as rmii_transceiver) and decodes
// every frame: Ethernet/IP/UDP header (template, lengths, IP checksum), then
// for audio (source port 0xD400) the U64 format of tests/e2e/lib/streams.py:
// 770-byte UDP payload, u16le sequence, 192 frames of s16le L/R. The test
// samples count up (L = n, R = ~n), so every packet must continue the last.
// Audio payloads go to sim_audio.hex for the Python check.
`timescale 1ns/1ps
module tb;
    reg eclk = 0, vclk = 0, mclk = 0, cclk = 0;
    always #10     eclk = ~eclk;        // 50 MHz
    always #15.859 vclk = ~vclk;        // 31.5278 MHz
    always #40.69  mclk = ~mclk;        // 12.288 MHz
    always #5      cclk = ~cclk;        // 100 MHz
    reg rst = 1;

    // ---- sources -------------------------------------------------------------
    reg [7:0] mdiv = 0; reg snd_en = 0; reg [15:0] n_snd = 0;
    always @(posedge mclk) begin
        mdiv <= mdiv + 1; snd_en <= (mdiv == 8'd255);
        if (snd_en) n_snd <= n_snd + 1;
    end
    wire signed [23:0] al = {n_snd, 8'h7F}, ar = {~n_snd, 8'h00};

    reg [1:0] vd = 0; reg [9:0] px = 0; reg [8:0] py = 0;
    always @(posedge vclk) begin
        vd <= vd + 1;
        if (vd == 3) begin
            if (px == 503) begin px <= 0; py <= (py == 311) ? 0 : py + 1; end
            else px <= px + 1;
        end
    end

    reg tpl_we = 0; reg [7:0] tpl_addr = 0, tpl_wdata = 0;
    reg [7:0] ena = 0;

    wire [7:0] vd_d, ad_d, sd;
    wire vreq, vgr, vval, veof, vrdy, areq, agr, aval, aeof, ardy, sreq, sval, seof;
    wire [15:0] vframes, apkts;
    reg  sgrant = 0; wire sready;
    udp_stream vs (.vclk(vclk), .v60(1'b0), .pix(px[3:0]), .px(px), .py(py),
        .cpu_clk(cclk), .tpl_we(tpl_we && tpl_addr[7:6] == 0), .tpl_addr(tpl_addr[5:0]), .tpl_wdata(tpl_wdata),
        .eth_clk(eclk), .eth_rst(rst), .enable(ena[0]),
        .tx_req(vreq), .tx_grant(vgr), .tx_data(vd_d), .tx_valid(vval), .tx_eof(veof), .tx_ready(vrdy),
        .frames_sent(vframes));
    udp_audio au (.snd_clk(mclk), .snd_en(snd_en), .in_l(al), .in_r(ar),
        .cpu_clk(cclk), .tpl_we(tpl_we && tpl_addr[7:6] == 1), .tpl_addr(tpl_addr[5:0]), .tpl_wdata(tpl_wdata),
        .eth_clk(eclk), .eth_rst(rst), .enable(ena[1]),
        .tx_req(areq), .tx_grant(agr), .tx_data(ad_d), .tx_valid(aval), .tx_eof(aeof), .tx_ready(ardy),
        .pkts_sent(apkts));
    stream_mux mx (.clk(eclk), .rst(rst),
        .a_req(vreq), .b_req(areq), .a_grant(vgr), .b_grant(agr),
        .a_data(vd_d), .b_data(ad_d), .a_valid(vval), .b_valid(aval), .a_eof(veof), .b_eof(aeof),
        .a_ready(vrdy), .b_ready(ardy),
        .req(sreq), .grant(sgrant), .data(sd), .valid(sval), .eof(seof), .ready(sready));

    // ---- transmitter model: arbiter as top.v (no MAC here), a byte every 4 clocks,
    // 12 bytes idle between frames ----
    reg [1:0] ph = 0; reg [5:0] gap = 0;
    assign sready = sgrant && sval && ph == 3;
    always @(posedge eclk) begin
        if (!sgrant) begin
            if (gap) gap <= gap - 1;
            else if (sreq) sgrant <= 1;
            ph <= 0;
        end else begin
            if (sval) ph <= ph + 1;
            if (sready && seof) begin sgrant <= 0; gap <= 48; end
        end
    end

    // ---- receiver / decoder ----
    reg [7:0] fr [0:2047]; integer len = 0, errors = 0, apk = 0, vpk = 0, i, f, fd;
    reg [31:0] sum; reg [15:0] last_seq, last_s; reg have_last = 0, vhave = 0; reg [15:0] vlast;
    task fail(input [8*64-1:0] msg); begin errors = errors + 1; $display("ERROR %0s (len %0d)", msg, len); end endtask
    task check_frame;
        reg [15:0] s, l, r, seqv, udpl, iplen;
        begin
            sum = 0;
            for (i = 14; i < 34; i = i + 2) sum = sum + {fr[i], fr[i+1]};
            sum = (sum & 32'hFFFF) + (sum >> 16); sum = (sum & 32'hFFFF) + (sum >> 16);
            if (sum[15:0] != 16'hFFFF) fail("IP checksum");
            for (i = 0; i < 42; i = i + 1)
                if (!(i == 16 || i == 17 || i == 24 || i == 25 || i == 38 || i == 39 || i == 34 || i == 35 || i == 37)
                    && fr[i] != tmpl(i)) fail("header byte differs from the template");
            iplen = {fr[16], fr[17]}; udpl = {fr[38], fr[39]};
            if (iplen != len - 14 || udpl != len - 34) fail("IP/UDP length");
            if ({fr[34], fr[35]} == 16'hD400) begin            // audio
                if (len != 42 + 770 || fr[37] != 8'hF9) fail("audio frame size / port");
                seqv = {fr[43], fr[42]};
                if (have_last && seqv != last_seq + 1) fail("audio sequence");
                for (f = 0; f < 192; f = f + 1) begin
                    l = {fr[44 + 4*f + 1], fr[44 + 4*f]};
                    r = {fr[44 + 4*f + 3], fr[44 + 4*f + 2]};
                    if (r != ~l) fail("R != ~L");
                    if ((have_last || f > 0) && l != last_s + 1) begin fail("sample gap"); $display("  got %h after %h", l, last_s); end
                    last_s = l;
                end
                for (i = 42; i < len; i = i + 1) $fwrite(fd, "%02x", fr[i]);
                $fwrite(fd, "\n");
                last_seq = seqv; have_last = 1; apk = apk + 1;
            end else if ({fr[34], fr[35]} == 16'hD000) begin   // video
                if (len != 42 + 780 || fr[37] != 8'hF8) fail("video frame size / port");
                if (vhave && {fr[43], fr[42]} != vlast + 1) fail("video sequence");
                vlast = {fr[43], fr[42]}; vhave = 1; vpk = vpk + 1;
            end else fail("unknown source port");
        end
    endtask
    always @(posedge eclk)
        if (sready) begin
            fr[len] = sd; len = len + 1;
            if (seof) begin check_frame; len = 0; end
        end

    // The 42 bytes the app writes for the audio stream (calculate_udp_headers,
    // as its console dumps them in ue2emu for audio:start?ip=10.0.2.2): IP
    // length 28 and the checksum over it, UDP length 8. The video template
    // differs only in the source port (0xD000) and its low destination port
    // byte (0xF8), both outside the IP checksum.
    function [7:0] tmpl(input integer k);
        reg [8*42-1:0] h;
        begin
            h = {48'h52550A000202, 48'h021541716742, 16'h0800,
                 160'h4500001C_BBBB4000_40116705_0A00020F_0A000202,
                 64'hD4002AF900080000};
            tmpl = h[8*(41-k) +: 8];
        end
    endfunction
    task write_tpl(input integer id);
        integer k; reg [7:0] b;
        begin
            for (k = 0; k < 42; k = k + 1) begin
                b = tmpl(k);
                if (id == 0 && k == 34) b = 8'hD0;
                if (id == 0 && k == 37) b = 8'hF8;
                @(posedge cclk) begin tpl_we <= 1; tpl_addr <= id*64 + k; tpl_wdata <= b; end
            end
            @(posedge cclk) tpl_we <= 0;
        end
    endtask

    // The app's sequence: start = template, then U64_ETHSTREAM_ENA |= 1 << id
    // (read-modify-write of one register); stop = ENA &= ~(1 << id).
    integer stop_pkts;
    initial begin
        fd = $fopen("sim_audio.hex", "w");
        #200 rst = 0;
        write_tpl(1); ena = ena | 8'h02;        // audio:start alone first
        #9_000_000;
        write_tpl(0); ena = ena | 8'h01;        // then video:start beside it
        #22_000_000;
        ena = ena & ~8'h02;                     // audio:stop, video goes on
        #50_000 stop_pkts = apk;
        #8_000_000;
        if (apk != stop_pkts) fail("audio after stop");
        $fclose(fd);
        $display("audio packets %0d (sent %0d), video packets %0d, video frames %0d, errors %0d",
                 apk, apkts, vpk, vframes, errors);
        if (errors == 0 && apk >= 6 && vpk >= 90 && apkts == apk) $display("PASS"); else $display("FAIL");
        $finish;
    end
endmodule
