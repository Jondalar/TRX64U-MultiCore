// VIC video as UDP packets, in the format of the U64's video stream (as the
// Ultimate app and the 1541ultimate e2e decoder, tests/e2e/lib/streams.py,
// expect it):
//   Ethernet + IP + UDP header: the 42 bytes the app writes as a template
//     (data_streamer.cc, 0x10190000), with the IP total length, its checksum
//     and the UDP length filled in here;
//   12 bytes: seq u16, frame u16, line u16 (bit 15 = last packet of the
//     frame), width 384 u16, lines per packet 4 u8, bits per pixel 4 u8,
//     encoding 0 u16, all little-endian;
//   768 bytes: four lines of 384 VIC colour indices, two per byte, the left
//     pixel in the low nibble.
// A PAL frame is 272 lines (68 packets), raster lines Y_S.., pixels X_S..
// (the 320 x 200 text area with 32 pixels of border left and right, 36 above
// and below, as on the U64). A 60 Hz frame (T40, v60: NTSC and PAL-60, 263
// lines) is 240 lines (60 packets) from line Y_S_60: 20 border lines above and
// below the text area, so it runs from 31 through 262 into 0-7 of the next
// VIC frame; lines below Y_S_60 count as 263 more.
//
// Capture (vclk, the VIC's dot4x): every clock writes the current colour at
// its raster x into a buffer of 8 lines (two packets); the last value written
// at an x is the pixel (as vic_scaler does). When the fourth line of a packet
// is done, a toggle hands the packet to the sender.
// Sender (eth_clk, 50 MHz): builds the frame byte by byte for the RMII
// transmitter (rmii_transceiver: data held from valid until ready, no gaps);
// tx_req asks the arbiter in top.v for the transmitter between two frames of
// the MAC. A packet that finds the sender still busy is dropped.
module udp_stream_c64u #(
    parameter [9:0] X_S = 10'd104,
    parameter [8:0] Y_S = 9'd15,
    parameter [8:0] LINES = 9'd272,
    parameter [8:0] Y_S_60 = 9'd31,
    parameter [8:0] LINES_60 = 9'd240
) (
    input  wire        vclk,
    input  wire [3:0]  pix,
    input  wire [9:0]  px,
    input  wire [8:0]  py,
    input  wire        v60,             // 65 x 263 VIC (any clock, quasi-static)

    // template RAM, written by the RISC-V (its clock)
    input  wire        cpu_clk,
    input  wire        tpl_we,
    input  wire [5:0]  tpl_addr,
    input  wire [7:0]  tpl_wdata,

    input  wire        eth_clk,
    input  wire        eth_rst,
    input  wire        enable,          // U64_ETHSTREAM_ENA bit 0 (any clock)
    output reg         tx_req,          // a frame is waiting (eth_clk)
    input  wire        tx_grant,        // the transmitter is ours until the eof byte went
    output reg  [7:0]  tx_data,
    output reg         tx_valid,
    output reg         tx_eof,
    input  wire        tx_ready,
    output reg  [15:0] frames_sent      // eth_clk, for the debug port
);
    localparam HDR = 42, SHDR = 12, PAY = 768;
    localparam LAST = HDR + SHDR + PAY - 1;           // 821
    localparam [15:0] IP_LEN  = 16'd20 + 16'd8 + SHDR + PAY;   // 808
    localparam [15:0] UDP_LEN = 16'd8 + SHDR + PAY;            // 788

    // ---- capture (vclk) --------------------------------------------------------
    // even and odd pixels in two RAMs, so that the sender reads a byte (two
    // pixels) per address: {slot[2:0], x[8:1]}
    (* ram_style = "block" *) reg [3:0] even [0:2047];
    (* ram_style = "block" *) reg [3:0] odd  [0:2047];
    (* ASYNC_REG = "TRUE" *) reg [1:0] v60_s = 2'b00;
    always @(posedge vclk) v60_s <= {v60_s[0], v60};
    wire       w60  = v60_s[1];
    wire [8:0] ys   = w60 ? Y_S_60 : Y_S;
    wire [8:0] nl   = w60 ? LINES_60 : LINES;
    wire [9:0] vy   = (w60 && py < Y_S_60) ? {1'b0, py} + 10'd263 : {1'b0, py};   // virtual line
    wire [9:0] xr = px - X_S;
    wire [9:0] yr = vy - {1'b0, ys};
    wire       in_win = (px >= X_S) && (xr < 10'd384) && (vy >= {1'b0, ys}) && (yr < {1'b0, nl});
    wire [10:0] waddr = {yr[2:0], xr[8:1]};
    always @(posedge vclk)
        if (in_win) begin
            if (!xr[0]) even[waddr] <= pix;
            else        odd[waddr]  <= pix;
        end

    reg  [9:0] py_d;
    reg        pkt_tgl = 1'b0;
    reg  [8:0] pkt_line;                // first line of the packet
    reg        pkt_slot;
    reg        pkt_last;
    wire [9:0] yd = py_d - {1'b0, ys};
    always @(posedge vclk) begin
        py_d <= vy;
        if (vy != py_d && py_d >= {1'b0, ys} && yd < {1'b0, nl} && yd[1:0] == 2'd3) begin
            pkt_line <= {yd[8:2], 2'b00};
            pkt_slot <= yd[2];
            pkt_last <= (yd == {1'b0, nl} - 10'd1);
            pkt_tgl  <= ~pkt_tgl;
        end
    end

    // ---- template (cpu_clk write, eth_clk read; changed only while disabled) ----
    reg [7:0] tpl [0:63];
    always @(posedge cpu_clk)
        if (tpl_we) tpl[tpl_addr] <= tpl_wdata;

    // ---- sender (eth_clk) --------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] tgl_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ena_s = 2'b00;
    reg        busy = 1'b0;
    reg  [9:0] n;                       // byte being offered
    reg  [8:0] line;
    reg        slot, last;
    reg [15:0] seq = 16'd0, frame = 16'd0;
    reg [15:0] csum;                    // IP checksum with the real length

    // one's complement: the app's checksum was made with length 28
    // one read port on the template (a distributed RAM: constant-index reads would have made it 512 flip-flops, T103 LUT budget):
    // two clocks after a packet starts it reads bytes 24 and 25 for the checksum, later the byte being offered
    reg  [1:0] prep = 2'd0;
    reg  [7:0] ck_hi;
    wire [5:0] ta = prep == 2'd1 ? 6'd24 : prep == 2'd2 ? 6'd25 : tx_valid ? n[5:0] + 6'd1 : 6'd0;
    wire [7:0] tv = tpl[ta];
    wire [15:0] c0 = ~{ck_hi, tv};
    wire [16:0] c1  = {1'b0, c0} + {1'b0, IP_LEN - 16'd28};
    wire [15:0] c2  = c1[15:0] + {15'd0, c1[16]};

    // payload: the next byte to offer sits at {slot, pl, pb} (192 bytes a line)
    reg  [1:0] pl;                      // line in the packet
    reg  [7:0] pb;                      // byte in that line
    reg  [3:0] even_q, odd_q;
    always @(posedge eth_clk) begin
        even_q <= even[{slot, pl, pb}];
        odd_q  <= odd[{slot, pl, pb}];
    end

    function [7:0] hdr_byte(input [9:0] k, input [7:0] tb);
        case (k)
            10'd16: hdr_byte = IP_LEN[15:8];
            10'd17: hdr_byte = IP_LEN[7:0];
            10'd24: hdr_byte = ~csum[15:8];
            10'd25: hdr_byte = ~csum[7:0];
            10'd38: hdr_byte = UDP_LEN[15:8];
            10'd39: hdr_byte = UDP_LEN[7:0];
            10'd42: hdr_byte = seq[7:0];
            10'd43: hdr_byte = seq[15:8];
            10'd44: hdr_byte = frame[7:0];
            10'd45: hdr_byte = frame[15:8];
            10'd46: hdr_byte = line[7:0];
            10'd47: hdr_byte = {last, 6'd0, line[8]};
            10'd48: hdr_byte = 8'h80;       // width 384
            10'd49: hdr_byte = 8'h01;
            10'd50: hdr_byte = 8'd4;        // lines per packet
            10'd51: hdr_byte = 8'd4;        // bits per pixel
            10'd52: hdr_byte = 8'd0;        // encoding
            10'd53: hdr_byte = 8'd0;
            default: hdr_byte = tb;
        endcase
    endfunction

    always @(posedge eth_clk) begin
        tgl_s <= {tgl_s[1:0], pkt_tgl};
        ena_s <= {ena_s[0], enable};
        if (prep == 2'd1) begin ck_hi <= tv; prep <= 2'd2; end
        if (prep == 2'd2) begin csum <= c2; prep <= 2'd0; end
        if (eth_rst) begin
            prep <= 2'd0; busy <= 1'b0; tx_req <= 1'b0; tx_valid <= 1'b0; tx_eof <= 1'b0;
            frames_sent <= 16'd0;
        end else begin
            if (tgl_s[2] != tgl_s[1] && ena_s[1] && !busy) begin
                // a packet is complete in the buffer: its info is stable now
                busy   <= 1'b1;
                tx_req <= 1'b1;
                line   <= pkt_line;
                slot   <= pkt_slot;
                last   <= pkt_last;
                prep   <= 2'd1;
                n      <= 10'd0;
                pl     <= 2'd0;
                pb     <= 8'd0;
            end
            if (busy && tx_grant && !tx_valid && n == 10'd0 && prep == 2'd0) begin
                tx_data  <= tv;
                tx_valid <= 1'b1;
                tx_eof   <= 1'b0;
            end else if (tx_valid && tx_ready) begin
                if (n == LAST) begin
                    tx_valid <= 1'b0;
                    tx_eof   <= 1'b0;
                    busy     <= 1'b0;
                    tx_req   <= 1'b0;
                    seq      <= seq + 16'd1;
                    if (last) begin
                        frame <= frame + 16'd1;
                        frames_sent <= frames_sent + 16'd1;
                    end
                end else begin
                    n <= n + 10'd1;
                    tx_eof <= (n + 10'd1 == LAST);
                    if (n + 10'd1 < HDR + SHDR)
                        tx_data <= hdr_byte(n + 10'd1, tv);
                    else
                        tx_data <= {odd_q, even_q};
                    // advance the payload read address once the byte at it is taken
                    if (n + 10'd1 >= HDR + SHDR) begin
                        if (pb == 8'd191) begin pb <= 8'd0; pl <= pl + 2'd1; end
                        else pb <= pb + 8'd1;
                    end
                end
            end
        end
    end
endmodule
