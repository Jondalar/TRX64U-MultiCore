// T103 prototype, core side of the generic packet port: the three blocks that leave the host, unchanged
// (../../udp_stream.v, udp_audio.v, stream_mux.v), their "eth_clk" is the core's cpu_clk. Bench only.
module pktport_core (
    input  wire        cpu_clk, cpu_rst,
    input  wire        vclk,                      // sys_clk
    input  wire [3:0]  pix, input wire [9:0] px, input wire [8:0] py, input wire v60,
    input  wire        snd_clk, snd_en,           // mclk, audio_sample_en
    input  wire signed [23:0] in_l, in_r,
    input  wire        udp_we, input wire [7:0] udp_addr, input wire [7:0] udp_wdata,
    input  wire [7:0]  stream_ena,
    // the packet port (cpu_clk)
    output wire        pp_req,
    input  wire        pp_grant,
    output wire [7:0]  pp_data,
    output wire        pp_valid,
    output wire        pp_eof,
    input  wire        pp_ready
);
    wire [7:0] vs_data, as_data;
    wire       vs_req, vs_grant, vs_valid, vs_eof, vs_ready;
    wire       as_req, as_grant, as_valid, as_eof, as_ready;
    wire [15:0] frames, pkts;
    udp_stream vstream (
        .vclk(vclk), .pix(pix), .px(px), .py(py), .v60(v60),
        .cpu_clk(cpu_clk), .tpl_we(udp_we && udp_addr[7:6] == 2'd0), .tpl_addr(udp_addr[5:0]), .tpl_wdata(udp_wdata),
        .eth_clk(cpu_clk), .eth_rst(cpu_rst), .enable(stream_ena[0]),
        .tx_req(vs_req), .tx_grant(vs_grant), .tx_data(vs_data), .tx_valid(vs_valid),
        .tx_eof(vs_eof), .tx_ready(vs_ready), .frames_sent(frames));
    udp_audio astream (
        .snd_clk(snd_clk), .snd_en(snd_en), .in_l(in_l), .in_r(in_r),
        .cpu_clk(cpu_clk), .tpl_we(udp_we && udp_addr[7:6] == 2'd1), .tpl_addr(udp_addr[5:0]), .tpl_wdata(udp_wdata),
        .eth_clk(cpu_clk), .eth_rst(cpu_rst), .enable(stream_ena[1]),
        .tx_req(as_req), .tx_grant(as_grant), .tx_data(as_data), .tx_valid(as_valid),
        .tx_eof(as_eof), .tx_ready(as_ready), .pkts_sent(pkts));
    stream_mux smux (
        .clk(cpu_clk), .rst(cpu_rst),
        .a_req(vs_req), .b_req(as_req), .a_grant(vs_grant), .b_grant(as_grant),
        .a_data(vs_data), .b_data(as_data), .a_valid(vs_valid), .b_valid(as_valid),
        .a_eof(vs_eof), .b_eof(as_eof), .a_ready(vs_ready), .b_ready(as_ready),
        .req(pp_req), .grant(pp_grant), .data(pp_data), .valid(pp_valid), .eof(pp_eof), .ready(pp_ready));
endmodule
