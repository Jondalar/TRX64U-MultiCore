// T113 measurement skeleton: the VIC-IIe's two extra registers ($D02F, $D030) and the VDC at $D600/$D601.
//   `VDC_WHOLE : the whole MiSTer VDC (vdc/vdc_top.sv with its patches, PROVENANCE) incl. drawing -> pixel stream (variant 1)
//   `VDC_SPLIT : vdc/vdc_cpu.v, the CPU-visible part only; the application draws (variant 2)
//   neither    : no VDC (the baseline)
`timescale 1ns / 1ps
module c128_x (
    input  wire        clk, rst, warm, bus_en,
    input  wire        vdc_cs, vdc_we, vdc_rs,
    input  wire  [7:0] vdc_din,
    output wire  [7:0] vdc_dout,
    input  wire        vicie_cs, vicie_we, vicie_a0,
    input  wire  [7:0] vicie_din,
    output wire  [7:0] vicie_dout,
    output wire  [2:0] k_sel,
    output wire        fast,
    output wire  [3:0] vdc_rgbi,
    output wire        vdc_ce, vdc_hb, vdc_vb,
    input  wire        vd_clk,
    input  wire [15:0] vd_addr,
    input  wire        vd_we,
    input  wire  [7:0] vd_wdata,
    output wire  [7:0] vd_rdata
);
    // $D02F: bits 2:0 select the extra keyboard lines K0-K2 (low = selected), reads back 1 in bits 7:3 .
    // $D030: bit 0 = 2 MHz, bit 1 = test; reads back 1 in bits 7:2. The 2 MHz clock itself (the 8502's enable at twice the
    // rate, the VIC blanked while it is on) is not in this skeleton: `fast` is the request.
    reg [2:0] d02f = 3'b111;
    reg [1:0] d030 = 2'b00;
    always @(posedge clk) begin
        if (rst) begin d02f <= 3'b111; d030 <= 2'b00; end
        else if (vicie_cs && vicie_we && bus_en) begin
            if (vicie_a0) d02f <= vicie_din[2:0]; else d030 <= vicie_din[1:0];       // $D02F is the odd address, $D030 the even one
        end
    end
    assign vicie_dout = vicie_a0 ? {5'b11111, d02f} : {6'b111111, d030};
    assign k_sel = d02f;
    assign fast  = d030[0];

`ifdef VDC64
 `define VCPU_BITS 16
`else
 `define VCPU_BITS 14
`endif
`ifdef VDC_WHOLE
    wire act_h, act_v;
`ifdef VDC64
    localparam VBITS = 16, V64 = 1'b1;     // 64 KiB of VRAM (the real chip's maximum)
`else
    localparam VBITS = 14, V64 = 1'b0;     // 16 KiB
`endif
    vdc_top #(.RAM_ADDR_BITS(VBITS)) vdc (
        .version(1'b0), .ram64k(V64), .initRam(1'b1), .debug(1'b0),
        .clk(clk), .enableBus(bus_en), .reset(rst), .ctl_reset(warm), .init(1'b0),
        .cs(vdc_cs), .rs(vdc_rs), .we(vdc_we), .db_in(vdc_din), .db_out(vdc_dout),
        .lp_n(1'b1), .vsync(), .hsync(), .pixel_ce(vdc_ce), .active_h(act_h), .active_v(act_v), .rgbi(vdc_rgbi));
    assign vdc_hb = !act_h;
    assign vdc_vb = !act_v;
    assign vd_rdata = 8'd0;
`elsif VDC_SPLIT
    vdc_cpu #(.RAM_BITS(`VCPU_BITS)) vdc (
        .clk(clk), .rst(rst), .bus_en(bus_en), .cs(vdc_cs), .rs(vdc_rs), .we(vdc_we), .db_in(vdc_din), .db_out(vdc_dout),
        .vd_clk(vd_clk), .vd_addr(vd_addr), .vd_we(vd_we), .vd_wdata(vd_wdata), .vd_rdata(vd_rdata));
    assign vdc_rgbi = 4'd0; assign vdc_ce = 1'b0; assign vdc_hb = 1'b1; assign vdc_vb = 1'b1;
`else
    assign vdc_dout = 8'hFF; assign vd_rdata = 8'd0;
    assign vdc_rgbi = 4'd0; assign vdc_ce = 1'b0; assign vdc_hb = 1'b1; assign vdc_vb = 1'b1;
`endif
endmodule
