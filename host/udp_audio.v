// The C64's audio as UDP packets, in the format of the U64's audio stream
// (stream 1 of data_streamer.cc; 1541ultimate tests/e2e/lib/streams.py decodes
// it):
//   Ethernet + IP + UDP header: the 42 bytes the app writes as a template
//     (0x10190040), IP total length, its checksum and the UDP length filled in;
//   2 bytes: sequence number, u16 little-endian;
//   768 bytes: 192 stereo frames, s16 little-endian, left then right.
// The samples are the top 16 bits of the 24-bit audio_l/audio_r at the I2S
// frame rate (48.003 kHz here; the U64's PAL stream runs at 47.983 kHz).
//
// Capture (snd_clk): one 32-bit word per frame into a ping-pong buffer of two
// packets; a full half hands its number to the sender with a toggle.
// Sender (eth_clk): as udp_stream, a frame byte by byte for the shared RMII
// transmitter; a packet that finds the sender busy is dropped.
module udp_audio (
    input  wire        snd_clk,
    input  wire        snd_en,          // one frame (snd_clk)
    input  wire signed [23:0] in_l, in_r,

    input  wire        cpu_clk,
    input  wire        tpl_we,
    input  wire [5:0]  tpl_addr,
    input  wire [7:0]  tpl_wdata,

    input  wire        eth_clk,
    input  wire        eth_rst,
    input  wire        enable,          // U64_ETHSTREAM_ENA bit 1 (any clock)
    output reg         tx_req,
    input  wire        tx_grant,
    output reg  [7:0]  tx_data,
    output reg         tx_valid,
    output reg         tx_eof,
    input  wire        tx_ready,
    output reg  [15:0] pkts_sent
);
    localparam HDR = 42, SHDR = 2, PAY = 768;
    localparam LAST = HDR + SHDR + PAY - 1;                     // 811
    localparam [15:0] IP_LEN  = 16'd20 + 16'd8 + SHDR + PAY;    // 798
    localparam [15:0] UDP_LEN = 16'd8 + SHDR + PAY;             // 778

    // ---- capture (snd_clk) -----------------------------------------------------
    (* ram_style = "block" *) reg [31:0] buf_ram [0:511];
    reg  [8:0] wa = 9'd0;                   // {half, frame}
    reg        full_tgl = 1'b0, full_half = 1'b0;
    always @(posedge snd_clk)
        if (snd_en) begin
            buf_ram[wa] <= {in_r[23:8], in_l[23:8]};
            if (wa[7:0] == 8'd191) begin
                wa        <= {~wa[8], 8'd0};
                full_half <= wa[8];
                full_tgl  <= ~full_tgl;
            end else
                wa <= wa + 9'd1;
        end

    // ---- template ----------------------------------------------------------------
    reg [7:0] tpl [0:63];
    always @(posedge cpu_clk)
        if (tpl_we) tpl[tpl_addr] <= tpl_wdata;

    // ---- sender (eth_clk) ----------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] tgl_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ena_s = 2'b00;
    reg        busy = 1'b0;
    reg  [9:0] n;
    reg        half;
    reg  [9:0] pi;                          // payload byte to offer next
    reg [15:0] seq = 16'd0;
    reg [15:0] csum;
    reg [31:0] q;
    always @(posedge eth_clk) q <= buf_ram[{half, pi[9:2]}];

    wire [15:0] c0 = ~{tpl[24], tpl[25]};
    wire [16:0] c1 = {1'b0, c0} + {1'b0, IP_LEN - 16'd28};
    wire [15:0] c2 = c1[15:0] + {15'd0, c1[16]};

    function [7:0] hdr_byte(input [9:0] k);
        case (k)
            10'd16: hdr_byte = IP_LEN[15:8];
            10'd17: hdr_byte = IP_LEN[7:0];
            10'd24: hdr_byte = ~csum[15:8];
            10'd25: hdr_byte = ~csum[7:0];
            10'd38: hdr_byte = UDP_LEN[15:8];
            10'd39: hdr_byte = UDP_LEN[7:0];
            10'd42: hdr_byte = seq[7:0];
            10'd43: hdr_byte = seq[15:8];
            default: hdr_byte = tpl[k[5:0]];
        endcase
    endfunction

    always @(posedge eth_clk) begin
        tgl_s <= {tgl_s[1:0], full_tgl};
        ena_s <= {ena_s[0], enable};
        if (eth_rst) begin
            busy <= 1'b0; tx_req <= 1'b0; tx_valid <= 1'b0; tx_eof <= 1'b0;
            pkts_sent <= 16'd0;
        end else begin
            if (tgl_s[2] != tgl_s[1] && ena_s[1] && !busy) begin
                busy   <= 1'b1;
                tx_req <= 1'b1;
                half   <= full_half;
                csum   <= c2;
                n      <= 10'd0;
                pi     <= 10'd0;
            end
            if (busy && tx_grant && !tx_valid && n == 10'd0) begin
                tx_data  <= tpl[0];
                tx_valid <= 1'b1;
                tx_eof   <= 1'b0;
            end else if (tx_valid && tx_ready) begin
                if (n == LAST) begin
                    tx_valid  <= 1'b0;
                    tx_eof    <= 1'b0;
                    busy      <= 1'b0;
                    tx_req    <= 1'b0;
                    seq       <= seq + 16'd1;
                    pkts_sent <= pkts_sent + 16'd1;
                end else begin
                    n <= n + 10'd1;
                    tx_eof <= (n + 10'd1 == LAST);
                    if (n + 10'd1 < HDR + SHDR)
                        tx_data <= hdr_byte(n + 10'd1);
                    else begin
                        tx_data <= q[{pi[1:0], 3'b000} +: 8];
                        pi <= pi + 10'd1;
                    end
                end
            end
        end
    end
endmodule
