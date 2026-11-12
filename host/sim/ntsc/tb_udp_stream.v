// udp_stream (T40) against a synthetic VIC: packets per frame, their line
// numbers and last flags, and the pixels of the first and last line, PAL
// (504 x 312: 68 packets, lines 15-286) and -DV60 (520 x 263: 60 packets,
// lines 31-262 and 0-7 of the next frame).
//   iverilog -g2012 [-DV60] -o u tb_udp_stream.v ../../udp_stream.v && vvp u
`timescale 1ns/1ps
module tb;
    reg vclk = 0, eclk = 0;
    always #15 vclk = ~vclk;
    always #10 eclk = ~eclk;                // 50 MHz; the RMII transmitter takes a byte every 4 clocks
    reg [1:0] rc = 0; always @(posedge eclk) rc <= rc + 1;
    wire ready = rc == 2'd3;
`ifdef V60
    localparam XMAX = 519, YMAX = 262, V60 = 1, NPKT = 60, Y0 = 31, NL = 240;
`else
    localparam XMAX = 503, YMAX = 311, V60 = 0, NPKT = 68, Y0 = 15, NL = 272;
`endif
    reg [1:0] ph = 0; reg [9:0] x = 0; reg [8:0] y = 0;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            if (x == XMAX) begin x <= 0; y <= (y == YMAX) ? 0 : y + 1; end
            else x <= x + 1;
        end
    end
    wire [3:0] pix = x[3:0] ^ y[3:0];
    reg rst = 1;
    wire tx_req, tx_valid, tx_eof; wire [7:0] tx_data; wire [15:0] fs;
    udp_stream dut (.vclk(vclk), .pix(pix), .px(x), .py(y), .v60(V60[0]),
        .cpu_clk(vclk), .tpl_we(1'b0), .tpl_addr(6'd0), .tpl_wdata(8'd0),
        .eth_clk(eclk), .eth_rst(rst), .enable(1'b1), .tx_req(tx_req), .tx_grant(1'b1),
        .tx_data(tx_data), .tx_valid(tx_valid), .tx_eof(tx_eof), .tx_ready(ready), .frames_sent(fs));

    // receive: header bytes 46/47 = line | last << 15; payload from byte 54
    integer n = 0, line, last, pkts = 0, errors = 0, frames = 0, expect_line = 0, k;
    reg [7:0] b46;
    reg [7:0] pay [0:767];
    function integer vic_line(input integer l);    // stream line -> raster line
        vic_line = (Y0 + l) > YMAX ? Y0 + l - YMAX - 1 : Y0 + l;
    endfunction
    always @(posedge eclk) if (tx_valid && ready) begin
        if (n == 46) b46 = tx_data;
        if (n == 47) begin line = {tx_data[0], b46}; last = tx_data[7]; end
        if (n >= 54) pay[n - 54] = tx_data;
        n = tx_eof ? 0 : n + 1;
        if (tx_eof && frames > 0) begin
            if (line != expect_line) begin errors = errors + 1; $display("line %0d, expected %0d", line, expect_line); end
            // first pixel pair of each of the four lines: X_S = 104, 105
            for (k = 0; k < 4; k = k + 1)
                if (pay[k * 192] !== {4'(105 ^ vic_line(line + k)), 4'(104 ^ vic_line(line + k))}) begin
                    errors = errors + 1;
                    if (errors < 5) $display("pixels of line %0d: %h want %h", line + k, pay[k * 192], {4'(105 ^ vic_line(line + k)), 4'(104 ^ vic_line(line + k))});
                end
            pkts = pkts + 1;
            expect_line = expect_line + 4;
            if (last) begin
                if (pkts != NPKT || line != NL - 4) begin errors = errors + 1; $display("frame %0d: %0d packets, last line %0d", frames, pkts, line); end
                else $display("frame %0d: %0d packets, lines 0-%0d", frames, pkts, line + 3);
                pkts = 0; expect_line = 0; frames = frames + 1;
                if (frames == 4) begin $display("errors %0d: %s", errors, errors == 0 ? "PASS" : "FAIL"); $finish; end
            end
        end else if (tx_eof && last) begin frames = 1; pkts = 0; expect_line = 0; end
    end
    initial begin #200 rst = 0; #200_000_000 $display("TIMEOUT FAIL"); $finish; end
endmodule
